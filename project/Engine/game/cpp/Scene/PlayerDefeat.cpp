/**
 * @file PlayerDefeat.cpp
 * @brief プレイヤーのやられ演出（PlayerDefeat）の段階ごとの処理と、player_defeat.json の読み込み
 */
#include "PlayerDefeat.h"
#include "PlayerDefeatTuning.h"
#include "AudioBridge.h"
#include "Collision.h"
#include "CrashBreadcrumbs.h"
#include "Easing.h"
#include "FontRenderer.h"
#include "GameConstants.h"
#include "HitEffectLibrary.h"
#include "Input.h"
#include "JsonFieldTable.h"
#include "JsonHelper.h"
#include "Logger.h"
#include "Player.h"
#include "StringUtility.h"
#include "UILayout.h"
#include "Vector2.h"
#include "Vector4.h"
#include <algorithm>
#include <cmath>
#include <iterator>
#include <string>
using namespace engine::graphics;

namespace engine::game {
namespace {

// 文字の既定の配置（F2のエディタで動かした値は Resources/Config/UI/gameplay.json の defeat グループに保存される）
// 選択肢はこの文字の下に出す（位置は defeat_menu グループで動かせる）
constexpr const char* kLayoutName = "gameplay";
constexpr float kTitleY = 170.0f;
constexpr float kTitleScale = 4.0f;
constexpr Vector4 kTitleColor = { 1.0f, 0.25f, 0.25f, 1.0f };
constexpr float kTextFadeInRatio = 0.5f; // 文字が落ちてくる長さのうち、濃くなり切るまでの割合

// 選択肢の既定の配置（F2のエディタで動かした値は gameplay.json の defeat_menu グループに保存される）
constexpr Vector2 kMenuPosition = { 440.0f, 330.0f };
constexpr Vector2 kMenuItemSize = { 400.0f, 60.0f };
constexpr float kMenuLabelScale = 2.0f;

/** @brief 読み込み済みの調整値（player_defeat.json、PlayerDefeatTuning.cpp で読む） */
const DefeatTuning& Tune() {
    return DefeatTuningStore::Get();
}

/** @brief 倒れた時ののけぞり（ラジアン） */
float LiePitch() {
    return Tune().lieAngleDegrees * gameConstants::kDegToRad;
}

/** @brief from から to へ、頂点の高さ height の放物線を描く途中の位置（横は等速） */
Vector3 ArcPosition(const Vector3& from, const Vector3& to, float height, float t) {
    constexpr float kParabolaPeak = 4.0f; // t(1-t) の最大値 0.25 を 1 にする倍率
    return {
        from.x + (to.x - from.x) * t,
        from.y + (to.y - from.y) * t + height * kParabolaPeak * t * (1.0f - t),
        from.z + (to.z - from.z) * t
    };
}

/** @brief startPitch から後ろへ turns 回まわり、最後に仰向けで落ち着くまでの途中ののけぞり（勢いよく回り始めて減速する） */
float SpinPitch(float startPitch, float turns, float t) {
    const float endPitch = LiePitch() - turns * gameConstants::kTwoPi;
    return startPitch + (endPitch - startPitch) * easing::EaseOutQuad(t);
}

/** @brief 倒れた体を position に置くと埋まってしまう敵を返す（いなければ nullptr） */
const Vector3* FindBlockingEnemy(const Vector3& position, const std::vector<Vector3>& enemies) {
    const DefeatTuning& tune = Tune();
    const auto blocking = std::find_if(enemies.begin(), enemies.end(), [&](const Vector3& enemy) {
        return std::abs(enemy.x - position.x) < tune.enemyClearance
            && std::abs(enemy.y - position.y) < tune.enemyTopOffset;
    });
    return blocking != enemies.end() ? &*blocking : nullptr;
}
} // namespace

// ══════════════════════════════════════════════════════
// 段階
// ══════════════════════════════════════════════════════

/** @brief 演出の1段階（状態はPlayerDefeat側が持ち、段階は時間の進め方と見せ方だけを決める） */
class PlayerDefeat::IPhase {
public:
    /** @brief 派生クラスを基底クラスから正しく破棄できるようにする仮想デストラクタ */
    virtual ~IPhase() = default;
    /** @brief この状態へ入る際の初期設定を適用する */
    virtual void Enter(PlayerDefeat&) const { }
    /** @return この段階を終えたら true */
    virtual bool Update(PlayerDefeat& defeat) const = 0;
    /** @brief この段階の間、ボタンで残りを飛ばせるか */
    virtual bool CanSkip(const PlayerDefeat&) const {
        return false;
    }
};

/** @brief 被弾した瞬間に画面を揺らし、プレイヤーをその場で震わせて止める */
class PlayerDefeat::HitPhase final : public PlayerDefeat::IPhase {
public:
    /** @brief 被弾の揺れ・フラッシュ・音を出す */
    void Enter(PlayerDefeat& defeat) const override {
        const DefeatTuning& tune = Tune();
        const HitEffectPlayer fx(defeat.services_.particles, defeat.services_.shaker, defeat.services_.rng);
        fx.Impact(tune.hitImpact);
        fx.Flash(tune.hitFlash);
        if (!tune.hitSound.empty()) {
            AudioBridge::GetInstance()->PlaySE(tune.hitSound, tune.hitSoundVolume);
        }
        defeat.airborne_ = false;
        defeat.cameraFocus_ = defeat.start_;
    }
    /** @brief 震えを小さくしながら止まる（終わったら true） */
    bool Update(PlayerDefeat& defeat) const override {
        const DefeatTuning& tune = Tune();
        const float progress = easing::Progress(defeat.phaseTimer_, tune.hitSeconds);
        const float shake = tune.hitShakeWidth * (1.0f - progress) * std::sin(defeat.phaseTimer_ * tune.hitShakeSpeed);
        defeat.playerPosition_ = defeat.start_;
        defeat.playerPosition_.x += shake;
        return progress >= 1.0f;
    }
};

/** @brief 後ろへ弧を描いて吹き飛び、宙で回りながら床へ落ちる */
class PlayerDefeat::LaunchPhase final : public PlayerDefeat::IPhase {
public:
    /** @brief この状態へ入る際の初期設定を適用する */
    void Enter(PlayerDefeat& defeat) const override {
        defeat.airborne_ = true;
    }
    /** @brief 吹き飛ぶ弧と回転を進める（終わったら true） */
    bool Update(PlayerDefeat& defeat) const override {
        // 最初に落ちる場所（床か敵の頭の上）へ弧を描き、落ちる頃には仰向けで落ち着く
        const DefeatTuning& tune = Tune();
        const float t = easing::Progress(defeat.phaseTimer_, tune.launchSeconds);
        defeat.playerPosition_ = ArcPosition(defeat.start_, defeat.touchdown_, tune.launchHeight, t);
        defeat.bodyPitch_ = SpinPitch(0.0f, tune.spinTurns, t);
        defeat.cameraFocus_ = defeat.playerPosition_;
        return t >= 1.0f;
    }
};

/** @brief 敵の頭の上に落ちた時だけ、そこで跳ねて敵のいない床まで飛ぶ（体が敵に埋まらないように） */
class PlayerDefeat::EnemyBouncePhase final : public PlayerDefeat::IPhase {
public:
    /** @brief 敵の上で跳ねる時は、当たった揺れと火花を出す */
    void Enter(PlayerDefeat& defeat) const override {
        if (!defeat.bounceOnEnemy_) {
            return;
        }
        const DefeatTuning& tune = Tune();
        const HitEffectPlayer fx(defeat.services_.particles, defeat.services_.shaker, defeat.services_.rng);
        fx.Impact(tune.enemyBounceImpact);
        fx.Sparks(tune.enemyBounceSparks, defeat.touchdown_);
    }
    /** @brief 敵の上から倒れ込む床へ跳ねる（敵の上で跳ねない時はすぐ終える） */
    bool Update(PlayerDefeat& defeat) const override {
        if (!defeat.bounceOnEnemy_) {
            return true;
        }
        const DefeatTuning& tune = Tune();
        const float t = easing::Progress(defeat.phaseTimer_, tune.enemyBounceSeconds);
        Vector3 lying = defeat.landing_;
        lying.y += tune.lieLift;
        defeat.playerPosition_ = ArcPosition(defeat.touchdown_, lying, tune.enemyBounceHeight, t);
        defeat.bodyPitch_ = SpinPitch(LiePitch(), tune.enemyBounceSpinTurns, t);
        defeat.cameraFocus_ = defeat.playerPosition_;
        return t >= 1.0f;
    }
};

/** @brief 床に叩きつけられて小さく跳ね、仰向けのまま動かなくなる */
class PlayerDefeat::DownPhase final : public PlayerDefeat::IPhase {
public:
    /** @brief 叩きつけの揺れ・衝撃リング・砂ぼこりを出す */
    void Enter(PlayerDefeat& defeat) const override {
        const DefeatTuning& tune = Tune();
        defeat.bodyPitch_ = LiePitch();
        const HitEffectPlayer fx(defeat.services_.particles, defeat.services_.shaker, defeat.services_.rng);
        fx.Impact(tune.downImpact);
        fx.Ring(tune.downRing, defeat.landing_);
        fx.Sparks(tune.downSparks, defeat.landing_);
    }
    /** @brief 跳ね返りを進め、少し倒れたまま待つ（終わったら true） */
    bool Update(PlayerDefeat& defeat) const override {
        const DefeatTuning& tune = Tune();
        const float bounce = easing::Progress(defeat.phaseTimer_, tune.bounceSeconds);
        defeat.playerPosition_ = defeat.landing_;
        defeat.playerPosition_.y += tune.lieLift + tune.bounceHeight * std::sin(bounce * gameConstants::kPi);
        defeat.cameraFocus_ = defeat.landing_;
        return defeat.phaseTimer_ >= tune.downSeconds;
    }
};

/** @brief 倒れたまま上半身を起こし、震える腕を空へ伸ばしてあがき、力尽きてドサッと倒れて動かなくなる */
class PlayerDefeat::StrugglePhase final : public PlayerDefeat::IPhase {
public:
    /** @brief 地上の待機のアニメにして、腕を伸ばすポーズレイヤーをプレイヤーへ貸す */
    void Enter(PlayerDefeat& defeat) const override {
        defeat.airborne_ = false;
        defeat.collapsed_ = false;
        defeat.SetStruggleReach(0.0f);
        if (defeat.services_.player != nullptr) {
            defeat.services_.player->SetCinematicPoseLayer(&defeat.struggleReach_);
        }
    }
    /** @brief 起こしかける → 震えてあがく → 力尽きて倒れる → 少し待つ、を進める（終わったら true） */
    bool Update(PlayerDefeat& defeat) const override {
        const DefeatTuning& tune = Tune();
        const float t = defeat.phaseTimer_;
        const float holdEnd = tune.struggleRiseSeconds + tune.struggleHoldSeconds;
        const float collapseEnd = holdEnd + tune.collapseSeconds;
        const float riseAngle = tune.struggleRiseDegrees * gameConstants::kDegToRad;
        defeat.playerPosition_ = defeat.landing_;
        defeat.playerPosition_.y += tune.lieLift;
        defeat.cameraFocus_ = defeat.landing_;
        if (t < holdEnd) {
            // 上半身を起こしかけ、起きるほど大きく震えながら腕を空へ伸ばしていく
            const float rise = easing::EaseOutCubic(easing::Progress(t, tune.struggleRiseSeconds));
            const float shake = tune.struggleShakeDegrees * gameConstants::kDegToRad * std::sin(t * tune.struggleShakeSpeed);
            defeat.bodyPitch_ = LiePitch() + (riseAngle + shake) * rise;
            defeat.SetStruggleReach(rise);
        } else if (t < collapseEnd) {
            // 力が抜けて一気に倒れ込み、伸ばした腕も落ちる（手足を開いた落下のアニメにする）
            const float collapse = easing::EaseInQuad(easing::Progress(t - holdEnd, tune.collapseSeconds));
            defeat.bodyPitch_ = LiePitch() + riseAngle * (1.0f - collapse);
            defeat.SetStruggleReach(1.0f - collapse);
            defeat.airborne_ = true;
        } else if (!defeat.collapsed_) {
            // 倒れた瞬間に揺れと砂ぼこりを出し、その姿勢のまま動かなくする
            defeat.collapsed_ = true;
            defeat.bodyPitch_ = LiePitch();
            defeat.SetStruggleReach(0.0f);
            const HitEffectPlayer fx(defeat.services_.particles, defeat.services_.shaker, defeat.services_.rng);
            fx.Impact(tune.collapseImpact);
            fx.Sparks(tune.collapseSparks, defeat.landing_);
            if (defeat.services_.player != nullptr) {
                defeat.services_.player->SetCinematicAnimationSpeed(0.0f);
            }
        }
        return t >= collapseEnd + tune.afterCollapseSeconds;
    }
};

/** @brief 画面を暗くして GAME OVER の文字を上から落とし、少し見せてから終える */
class PlayerDefeat::GameOverPhase final : public PlayerDefeat::IPhase {
public:
    /** @brief GAME OVER を出し始めたことをシーンへ伝える（シーンはこれでゲームオーバーの曲を流す） */
    void Enter(PlayerDefeat& defeat) const override {
        defeat.gameOverShownPending_ = true;
    }
    /** @brief 暗転と文字の落下を進める（終わったら true） */
    bool Update(PlayerDefeat& defeat) const override {
        const DefeatTuning& tune = Tune();
        const float t = defeat.phaseTimer_;
        defeat.overlayAlpha_ = easing::EaseOutCubic(easing::Progress(t, tune.fadeSeconds));
        defeat.textDrop_ = easing::EaseOutBounce(easing::Progress(t, tune.textDropSeconds));
        defeat.textAlpha_ = easing::Progress(t, tune.textDropSeconds * kTextFadeInRatio);
        return t >= tune.textDropSeconds + tune.holdSeconds;
    }
    /** @brief 文字が出始めて少し経てば飛ばせる */
    bool CanSkip(const PlayerDefeat& defeat) const override {
        return defeat.phaseTimer_ >= Tune().skipLockSeconds;
    }
};

namespace {
/** @brief 段階の並び（被弾 → 吹き飛び → 敵の上なら跳ねる → 倒れ込み → あがいて力尽きる → GAME OVER） */
const PlayerDefeat::IPhase* PhaseAt(size_t index) {
    static const PlayerDefeat::HitPhase kHit;
    static const PlayerDefeat::LaunchPhase kLaunch;
    static const PlayerDefeat::EnemyBouncePhase kEnemyBounce;
    static const PlayerDefeat::DownPhase kDown;
    static const PlayerDefeat::StrugglePhase kStruggle;
    static const PlayerDefeat::GameOverPhase kGameOver;
    static const PlayerDefeat::IPhase* const kPhases[] = { &kHit, &kLaunch, &kEnemyBounce, &kDown, &kStruggle, &kGameOver };
    return index < std::size(kPhases) ? kPhases[index] : nullptr;
}

/** @brief 選択肢の行番号に対応する行き先（Initialize で並べた項目の順） */
PlayerDefeat::Choice ChoiceAt(int row) {
    static constexpr PlayerDefeat::Choice kChoices[] = { PlayerDefeat::Choice::kRetry, PlayerDefeat::Choice::kMap };
    return row >= 0 && row < static_cast<int>(std::size(kChoices)) ? kChoices[row] : PlayerDefeat::Choice::kNone;
}
} // namespace

// ══════════════════════════════════════════════════════
// 進行
// ══════════════════════════════════════════════════════

PlayerDefeat::PlayerDefeat() = default;
/** @brief unique_ptr で持つ不完全型のメンバを破棄するため、定義を cpp に置く */
PlayerDefeat::~PlayerDefeat() = default;

/** @brief 暗転用のスプライトと選択肢のメニューを作る（シーンの Initialize で1回） */
void PlayerDefeat::Initialize(SpriteCommon* spriteCommon, FontRenderer* font, engine::Audio* audio) {
    overlay_ = std::make_unique<Sprite>();
    overlay_->Initialize(spriteCommon, "Resources/white.png");
    overlay_->SetPosition({ 0.0f, 0.0f });
    overlay_->SetSize({ gameConstants::kScreenWidth, gameConstants::kScreenHeight });

    // 項目の並びは ChoiceAt の表と揃える
    const DefeatTuning& tune = Tune();
    UIMenuStyle menuStyle;
    menuStyle.labelScale = kMenuLabelScale;
    menuStyle.centerLabels = true;
    menu_.Initialize(spriteCommon, font, audio);
    menu_.BindLayout(kLayoutName, "defeat_menu", kMenuPosition, kMenuItemSize, menuStyle);
    menu_.SetItems({ { tune.retryLabel }, { tune.mapLabel } });
}

/** @brief 演出を最初から始める（プレイヤーは今いる場所から、向いている方と逆へ吹き飛ぶ） */
void PlayerDefeat::Start(const Setup& setup, const Services& services) {
    CrashBreadcrumbs::Add("game", "やられ演出を開始");
    services_ = services;
    start_ = services_.player != nullptr ? services_.player->GetPosition() : Vector3 {};
    PlanLanding(setup);
    PlanWeaponDrop(setup);
    if (services_.player != nullptr) {
        services_.player->BeginDefeatPose();
        services_.player->SetCinematicPoseLayer(nullptr);
    }

    phaseIndex_ = 0;
    phaseTimer_ = 0.0f;
    totalTimer_ = 0.0f;
    active_ = true;
    playing_ = true;
    choosing_ = false;
    decided_ = false;
    choiceTimer_ = 0.0f;
    choice_ = Choice::kNone;
    gameOverShownPending_ = false;
    cameraFocus_ = start_;
    cameraZoom_ = 1.0f;
    cameraHeight_ = gameConstants::kCameraFollowOffsetY;
    playerPosition_ = start_;
    bodyPitch_ = 0.0f;
    airborne_ = false;
    collapsed_ = false;
    overlayAlpha_ = 0.0f;
    textDrop_ = 0.0f;
    textAlpha_ = 0.0f;
    PhaseAt(phaseIndex_)->Enter(*this);
}

/** @brief 吹き飛んで最初に落ちる場所と、最後に倒れ込む床の位置を決める（落ちる先に敵がいれば敵の上で跳ねさせる） */
void PlayerDefeat::PlanLanding(const Setup& setup) {
    // 攻撃してきた側（向いている方）から離れるよう後ろへ飛ばし、ステージの外へははみ出させない
    const DefeatTuning& tune = Tune();
    const float facingX = services_.player != nullptr ? services_.player->GetLastDirX() : 1.0f;
    landing_ = FloorAt(setup, start_.x - facingX * tune.launchDistance);
    touchdown_ = landing_;
    touchdown_.y += tune.lieLift;
    bounceOnEnemy_ = false;

    const Vector3* blocker = FindBlockingEnemy(landing_, setup.enemyPositions);
    if (blocker == nullptr) {
        return;
    }
    // 敵の頭の上に落ちて跳ね、飛んできた向きの先（無理なら手前）の敵のいない床へ倒れ込む
    bounceOnEnemy_ = true;
    touchdown_ = { blocker->x, blocker->y + tune.enemyTopOffset, landing_.z };
    const float flyDirection = landing_.x >= start_.x ? 1.0f : -1.0f;
    const float bounceDistance = tune.enemyClearance + tune.bounceExtraDistance;
    const Vector3 beyond = FloorAt(setup, blocker->x + flyDirection * bounceDistance);
    const Vector3 before = FloorAt(setup, blocker->x - flyDirection * bounceDistance);
    const bool beyondClear = FindBlockingEnemy(beyond, setup.enemyPositions) == nullptr;
    const bool beforeClear = FindBlockingEnemy(before, setup.enemyPositions) == nullptr;
    landing_ = (beyondClear || !beforeClear) ? beyond : before;
}

/** @brief 手からすっぽ抜けた武器が飛んで落ちる先を決める */
void PlayerDefeat::PlanWeaponDrop(const Setup& setup) {
    // プレイヤーが吹き飛ぶのと逆（攻撃してきた側）へ、手の高さから放り出す
    const DefeatTuning& tune = Tune();
    const float facingX = services_.player != nullptr ? services_.player->GetLastDirX() : 1.0f;
    weaponFrom_ = start_;
    weaponFrom_.y += tune.weaponHandHeight;
    weaponTo_ = FloorAt(setup, start_.x + facingX * tune.weaponDistance);
    weaponTo_.y += tune.weaponGroundOffsetY;
    weaponPosition_ = weaponFrom_;
    weaponSpin_ = 0.0f;
    weaponLanded_ = false;
}

/** @brief 指定した横位置の真下の床に立った位置（ステージの端からは内側に収める） */
Vector3 PlayerDefeat::FloorAt(const Setup& setup, float x) const {
    const DefeatTuning& tune = Tune();
    const float minX = setup.stageLeft + tune.stageEdgeMargin;
    const float maxX = (std::max)(minX, setup.stageRight - tune.stageEdgeMargin);
    // やられた位置から先の壁の手前で止め、壁にめり込んだり向こうへ抜けたりしないようにする（弧の高さまでの壁を見る）
    Vector3 position = start_;
    position.x = Collision::LimitHorizontalMove(setup.solids, start_.x, std::clamp(x, minX, maxX),
        start_.y + tune.wallBandBottom, start_.y + tune.launchHeight, tune.wallClearance);
    return services_.player != nullptr ? services_.player->FindStandingPosition(position, setup.solids) : position;
}

/** @brief 手放した武器の飛ぶ弧と回転を進め、床に落ちた瞬間に砂ぼこりを出す */
void PlayerDefeat::UpdateWeaponDrop() {
    // 勢いよく回りながら飛び、落ちる頃には床に寝た角度で落ち着く
    const DefeatTuning& tune = Tune();
    const float t = easing::Progress(totalTimer_, tune.weaponSeconds);
    weaponPosition_ = ArcPosition(weaponFrom_, weaponTo_, tune.weaponHeight, t);
    const float spinDirection = weaponTo_.x >= weaponFrom_.x ? -1.0f : 1.0f; // 飛ぶ向きへ前転するように回す
    const float endSpin = tune.weaponLieAngleDegrees * gameConstants::kDegToRad + spinDirection * tune.weaponSpinTurns * gameConstants::kTwoPi;
    weaponSpin_ = endSpin * easing::EaseOutQuad(t);
    if (t >= 1.0f && !weaponLanded_) {
        weaponLanded_ = true;
        const HitEffectPlayer fx(services_.particles, services_.shaker, services_.rng);
        fx.Sparks(tune.weaponLandSparks, weaponTo_);
    }
}

/** @brief あがく腕を空へ伸ばす割合を決め、震えを足して腕のポーズレイヤーへ渡す（0で元の姿勢） */
void PlayerDefeat::SetStruggleReach(float weight) {
    // 伸ばす向きを横と縦に少しずつずらして、力の入った腕が震えて見えるようにする（縦は横と周期をずらす）
    constexpr float kVerticalShakeRatio = 1.37f;
    const DefeatTuning& tune = Tune();
    const float phase = phaseTimer_ * tune.reachShakeSpeed;
    Vector3 direction = tune.reachDirection;
    direction.x += tune.reachShake * std::sin(phase);
    direction.y += tune.reachShake * std::sin(phase * kVerticalShakeRatio);
    // 肘から先は付け根を伸ばした後の向きから寄せるので、付け根を先に並べる
    struggleReach_.SetReaches({
        { tune.reachArm, direction, tune.reachWeight * weight },
        { tune.reachForearm, direction, tune.reachForearmWeight * weight },
    });
}

/** @brief 1フレーム進める（暗転が始まった後は決定・攻撃ボタンで最後まで飛ばせる。見せ終えた後は選択肢を操作する） */
void PlayerDefeat::Update(Input* input) {
    if (choosing_) {
        UpdateChoice(input);
        return;
    }
    if (!playing_) {
        return;
    }
    totalTimer_ += gameConstants::kFrameDeltaTime;
    phaseTimer_ += gameConstants::kFrameDeltaTime;

    // やられた瞬間からプレイヤーへ寄っていき、寄るのに合わせてカメラの高さも下げる
    const DefeatTuning& tune = Tune();
    const float zoomIn = easing::EaseOutCubic(easing::Progress(totalTimer_, tune.zoomSeconds));
    cameraZoom_ = 1.0f + (tune.closeZoom - 1.0f) * zoomIn;
    cameraHeight_ = gameConstants::kCameraFollowOffsetY + (tune.closeHeight - gameConstants::kCameraFollowOffsetY) * zoomIn;
    UpdateWeaponDrop();

    const IPhase* phase = PhaseAt(phaseIndex_);
    const bool skipPressed = input != nullptr && input->TriggerCinematicSkip();
    if (skipPressed && phase->CanSkip(*this)) {
        OpenChoice();
        return;
    }
    if (phase->Update(*this)) {
        AdvancePhase();
    }
}

/** @brief 段階を次へ進める（最後まで行ったら終える） */
void PlayerDefeat::AdvancePhase() {
    ++phaseIndex_;
    phaseTimer_ = 0.0f;
    const IPhase* next = PhaseAt(phaseIndex_);
    if (next == nullptr) {
        // 最後の画（倒れたプレイヤーと GAME OVER）は残したまま、その下に選択肢を出す
        OpenChoice();
        return;
    }
    next->Enter(*this);
}

/** @brief 演出を見せ終え、暗転と GAME OVER を出し切った画で選択肢を出す */
void PlayerDefeat::OpenChoice() {
    // 文字が落ち切る前に飛ばされた時も、選択肢の上で読める位置と濃さにそろえる
    playing_ = false;
    overlayAlpha_ = 1.0f;
    textDrop_ = 1.0f;
    textAlpha_ = 1.0f;
    choosing_ = true;
    decided_ = false;
    choiceTimer_ = 0.0f;
    choice_ = Choice::kNone;
}

/** @brief 選択肢のカーソルを動かし、決定されたら行き先を覚える */
void PlayerDefeat::UpdateChoice(Input* input) {
    // 決めた後はシーンが切り替わるまで選んだ項目を出したままにする
    if (input == nullptr || decided_) {
        return;
    }
    choiceTimer_ += gameConstants::kFrameDeltaTime;
    menu_.Update(input);
    if (choiceTimer_ < Tune().choiceLockSeconds || !menu_.ConsumeConfirm(input)) {
        return;
    }
    choice_ = ChoiceAt(menu_.GetSelectedIndex());
    decided_ = choice_ != Choice::kNone;
}

/** @brief GAME OVER を出し始めた最初の1回だけ true を返す */
bool PlayerDefeat::ConsumeGameOverShown() {
    const bool shown = gameOverShownPending_;
    gameOverShownPending_ = false;
    return shown;
}

/** @brief 選択肢が決定された最初の1回だけその行き先を返す */
PlayerDefeat::Choice PlayerDefeat::ConsumeChoice() {
    const Choice choice = choice_;
    choice_ = Choice::kNone;
    return choice;
}

/** @brief プレイヤーの位置・のけぞり・アニメを反映する（シーンがカメラを動かした後に呼ぶ） */
void PlayerDefeat::UpdatePlayerPose() {
    if (!active_ || services_.player == nullptr) {
        return;
    }
    services_.player->SetPosition(playerPosition_);
    services_.player->SetDroppedWeaponPose(weaponPosition_, weaponSpin_);
    services_.player->UpdateCinematicPose(airborne_, { bodyPitch_, 0.0f, 0.0f });
}

/** @brief GAME OVER の文字を積む（シーンの Update 中、fontRenderer.Reset() の後に呼ぶ） */
void PlayerDefeat::QueueText(FontRenderer& font) const {
    if (!active_ || textAlpha_ <= 0.0f) {
        return;
    }
    const DefeatTuning& tune = Tune();
    UILayout& layout = UILayout::Get(kLayoutName);
    const float scale = layout.Float("defeat.title_scale", kTitleScale);
    Vector4 color = layout.Color("defeat.title_color", kTitleColor);
    color.w *= textAlpha_;
    const float y = layout.Float("defeat.title_y", kTitleY) - tune.textDropHeight * (1.0f - textDrop_);
    font.DrawStringCenteredW(StringUtility::ConvertString(tune.text), y, scale, color);
}

/** @brief 暗転と選択肢を描く（SpriteCommon の共通設定を済ませた後、文字の Draw より前に呼ぶ） */
void PlayerDefeat::Draw() {
    if (!active_ || overlayAlpha_ <= 0.0f) {
        return;
    }
    Vector4 color = Tune().overlayColor;
    color.w *= overlayAlpha_;
    overlay_->SetColor(color);
    overlay_->Update();
    overlay_->Draw();
    if (choosing_) {
        menu_.Draw();
    }
}

/** @brief 調整値をファイルから読み直す（調整値エディタで保存した時に呼ぶ） */
void PlayerDefeat::ReloadTuning() {
    DefeatTuningStore::Reload();
}

} // namespace engine::game
