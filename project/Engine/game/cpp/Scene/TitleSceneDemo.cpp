/** @file TitleSceneDemo.cpp
 * @brief タイトルの自動戦闘デモの初期化と更新
 */
#include "TitleScene.h"
#include "GameConstants.h"
#include "GameSettings.h"
#include "HitEffectLibrary.h"
#include "ModelManager.h"
#include "RunData.h"
#include "SaveData.h"
#include "SceneFlow.h"
#include "SceneManager.h"
#include "SceneShared.h"
#include "StageEditor.h"
#include "TitleDemoTuning.h"
#include "UILayout.h"
#include "SkinnedObject3d.h"
#include "SrvManager.h"
#include "WeaponManager.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cfloat>
#include <numbers>
#include <random>
#ifdef USE_IMGUI
#include <imgui.h>
#endif
using namespace engine::graphics;

namespace engine::game {
namespace {
constexpr Vector4 kDummyNormalColor = { 1.0f, 1.0f, 1.0f, 1.0f };

// 武器ごとのデモのコンボ段数
constexpr int kSwordComboLength = 4;
constexpr int kSpearComboLength = 3;
constexpr int kDaggerComboLength = 5;
constexpr int kHammerComboLength = 2;
constexpr int kDefaultComboLength = 3;

/** @brief デモの命中演出に渡す位置と色 */
struct DemoHitContext {
    const HitEffectPlayer& fx;
    Vector3 weaponPos; ///< 武器の位置（演出用Z適用済み）
    Vector3 targetPos; ///< 的の位置（演出用Z適用済み）
    float facing; ///< 的のある向き（右=+1, 左=-1）
    Vector4 color; ///< 武器の演出色
    bool skillPulse; ///< 武器固有技として演出するか
};

/** @brief タイトルデモでの武器ごとの振る舞い（コンボ段数・固有技の出し所・演出） */
class DemoWeaponStyle {
public:
    /** @brief 派生クラスを基底クラスから正しく破棄できるようにする仮想デストラクタ */
    virtual ~DemoWeaponStyle() = default;
    /** @brief デモで振るコンボ段数 */
    virtual int ComboLength() const {
        return kDefaultComboLength;
    }
    /** @brief この段を武器固有技として出すか */
    virtual bool IsSkillPulse(int, int) const {
        return false;
    }
    /** @brief 命中時に撒く軌跡のレシピ名（hit_effects.json の trails） */
    virtual const char* HitTrail() const {
        return "title_hit";
    }
    /** @brief 常時出す軌跡のレシピ名（hit_effects.json の trails） */
    virtual const char* IdleTrail() const {
        return "title_idle";
    }
    /** @brief 武器ごとの追加命中演出 */
    virtual void EmitHit(const DemoHitContext&) const { }

    /** @brief 武器種に対応する振る舞いを取得する */
    static const DemoWeaponStyle& For(WeaponType type);
};

/** @brief 剣: 素早い交差斬りと、幅の広い締め攻撃 */
class SwordDemoStyle final : public DemoWeaponStyle {
public:
    /** @brief この武器が使用するコンボの段数を返す */
    int ComboLength() const override {
        return kSwordComboLength;
    }
    /** @brief 剣: 最後の段を固有技として出す */
    bool IsSkillPulse(int pulseIndex, int attackCount) const override {
        return pulseIndex == attackCount - 1;
    }
    /** @brief 剣: デモの命中演出を出す */
    void EmitHit(const DemoHitContext& ctx) const override {
        ctx.fx.Slash(ctx.skillPulse ? "title_sword_skill" : "title_sword", ctx.weaponPos, ctx.facing, { .color = ctx.color });
        ctx.fx.Slash(ctx.skillPulse ? "title_sword_cross_skill" : "title_sword_cross", ctx.targetPos, ctx.facing, { .color = ctx.color });
    }
};

/** @brief 槍: 細く長い突きの軌跡と、命中点に集中する衝撃リング */
class SpearDemoStyle final : public DemoWeaponStyle {
public:
    /** @brief この武器が使用するコンボの段数を返す */
    int ComboLength() const override {
        return kSpearComboLength;
    }
    /** @brief 待機中の武器軌跡に使う演出レシピ名を返す */
    const char* IdleTrail() const override {
        return "title_idle_spear";
    }
    /** @brief 槍: デモの命中演出を出す */
    void EmitHit(const DemoHitContext& ctx) const override {
        const Vector3 midpoint = { (ctx.weaponPos.x + ctx.targetPos.x) * 0.5f, ctx.weaponPos.y, ctx.weaponPos.z };
        ctx.fx.Trail("title_spear_thrust", midpoint, ctx.color);
        ctx.fx.Slash("title_spear_thrust", ctx.targetPos, ctx.facing, { .color = ctx.color });
        ctx.fx.Ring("title_spear", ctx.targetPos, { .color = ctx.color });
    }
};

/** @brief 短剣: 小さな斬線を重ねて攻撃の速さを見せる */
class DaggerDemoStyle final : public DemoWeaponStyle {
public:
    /** @brief この武器が使用するコンボの段数を返す */
    int ComboLength() const override {
        return kDaggerComboLength;
    }
    /** @brief 短剣: 最初の段を固有技として出す */
    bool IsSkillPulse(int pulseIndex, int) const override {
        return pulseIndex == 0;
    }
    /** @brief 待機中の武器軌跡に使う演出レシピ名を返す */
    const char* IdleTrail() const override {
        return "title_idle_dagger";
    }
    /** @brief 短剣: デモの命中演出を出す */
    void EmitHit(const DemoHitContext& ctx) const override {
        const TitleDemoTuning& tune = TitleDemoTune();
        const SlashDef& cut = HitEffectLibrary::GetInstance()->Slash("title_dagger_cut");
        const float baseAngle = ctx.facing > 0.0f ? cut.angleRight : cut.angleLeft;
        // 斬線を中央の1本を挟んで斜めにずらして重ねる
        const float center = static_cast<float>(tune.daggerCutCount - 1) * 0.5f;
        for (int i = 0; i < tune.daggerCutCount; ++i) {
            const float offset = (static_cast<float>(i) - center) * tune.daggerCutSpacing;
            ctx.fx.Slash("title_dagger_cut", { ctx.targetPos.x + offset, ctx.targetPos.y + offset, ctx.targetPos.z }, ctx.facing,
                { .angle = baseAngle + offset, .color = ctx.color });
        }
    }
};

/** @brief 槌: 地面への重い衝撃を強調する */
class HammerDemoStyle final : public DemoWeaponStyle {
public:
    /** @brief この武器が使用するコンボの段数を返す */
    int ComboLength() const override {
        return kHammerComboLength;
    }
    /** @brief ハンマー: 最後の段を固有技として出す */
    bool IsSkillPulse(int pulseIndex, int attackCount) const override {
        return pulseIndex == attackCount - 1;
    }
    /** @brief 命中時の武器軌跡に使う演出レシピ名を返す */
    const char* HitTrail() const override {
        return "title_hit_hammer";
    }
    /** @brief 待機中の武器軌跡に使う演出レシピ名を返す */
    const char* IdleTrail() const override {
        return "title_idle_hammer";
    }
    /** @brief ハンマー: デモの命中演出を出す */
    void EmitHit(const DemoHitContext& ctx) const override {
        const Vector3 ground = { ctx.targetPos.x, TitleDemoTune().groundY, ctx.targetPos.z };
        ctx.fx.Ring(ctx.skillPulse ? "title_hammer_skill" : "title_hammer", ground, { .color = ctx.color });
        ctx.fx.Star("title_hammer", ground, { .color = ctx.color });
    }
};

/** @brief 武器種に対応する振る舞いを取得する */
const DemoWeaponStyle& DemoWeaponStyle::For(WeaponType type) {
    static const SwordDemoStyle sword {};
    static const SpearDemoStyle spear {};
    static const DaggerDemoStyle dagger {};
    static const HammerDemoStyle hammer {};
    static const DemoWeaponStyle def {};
    // WeaponType の並び順。専用の見せ方が無い武器は既定の見せ方
    static const std::array<const DemoWeaponStyle*, kWeaponTypeCount> kStyles = {
        &sword, // Sword
        &spear, // Spear
        &hammer, // Hammer
        &dagger, // Dagger
        &def, // Ball
        &def, // Greatsword
        &def, // Scythe
        &def, // Axe
    };
    const size_t index = static_cast<size_t>(type);
    return index < kStyles.size() ? *kStyles[index] : def;
}
} // namespace

/** @brief 背景デモの初期化（3D共通設定・プレイヤー・足場ブロック・武器全解放） */
void TitleScene::InitializeDemo() {
    // 3D描画の共通設定を作り、カメラを闘技場の固定位置へ置く
    BuildRenderFoundation(dxCommon_, SrvManager::GetInstance(),
        { modelCommon_, objectCommon_, shadowManager_, camera_ });

    const TitleDemoTuning& tune = TitleDemoTune();
    demoCameraX_ = tune.cameraX;
    camera_->SetTranslate({ demoCameraX_, tune.cameraY, gameConstants::kCameraDistanceZ });

    // プレイヤーは闘技場の範囲から出ないようにして開始位置に置く
    player_ = std::make_unique<Player>();
    player_->Initialize(modelCommon_.get());
    player_->SetHorizontalBounds(tune.arenaMinX, tune.arenaMaxX);
    player_->SetPosition({ tune.startX, tune.groundY, 0.0f });

    // パーティクルの設定と背景の天球
    particleManager_ = ParticleManager::GetInstance();
    sceneShared::CreateParticleGroupsFromJson(particleManager_, "Resources/particles/gameplay.json");

    skydome_ = std::make_unique<Skydome>();
    skydome_->InitializeDefault(modelCommon_.get());

    // 床と背景のビルはレベルJSON（Resources/Levels/title.json）に置き、F2のステージエディタで編集する

    // 的は初期配置の表どおりに置き、番号ごとに揺れ・速さ・攻撃間隔をずらす
    dummyModel_ = ModelManager::GetInstance()->GetOrLoad(modelCommon_.get(),
        "Resources/AnimatedMonsterPackby@Quaternius/OBJ/Slime.obj",
        "Resources/AnimatedMonsterPackby@Quaternius/OBJ/SlimePalette.png");
    demoDummies_.clear();
    int dummyIndex = 0;
    for (const TitleDemoInitialDummy& initial : tune.initialDummies) {
        const float order = static_cast<float>(dummyIndex);
        DemoDummy dummy;
        dummy.x = initial.x;
        dummy.airborne = initial.airborne;
        dummy.baseY = tune.groundY + (dummy.airborne ? tune.dummyAirborneHeight : tune.dummyFootOffsetY);
        dummy.currentY = dummy.baseY;
        dummy.bobPhase = order * tune.dummyBobPhaseStep;
        dummy.bobSpeed = tune.dummyBobSpeedBase + order * tune.dummyBobSpeedStep;
        dummy.moveSpeed = tune.dummyMoveSpeedBase + order * tune.dummyMoveSpeedStep;
        dummy.attackCooldown = tune.dummyAttackCooldownBase + order * tune.dummyAttackCooldownStep;
        dummy.object = std::make_unique<Object3d>();
        dummy.object->Initialize(modelCommon_.get());
        dummy.object->SetModel(dummyModel_);
        dummy.object->SetEnableLighting(false);
        dummy.object->SetScale({ tune.dummyScale, tune.dummyScale, tune.dummyScale });
        // このモデルは正面軸が画面奥向きなので、カメラ側を向くよう180度反転する。
        dummy.object->SetRotation({ 0.0f, std::numbers::pi_v<float>, 0.0f });
        dummy.object->SetPosition({ dummy.x, dummy.currentY, 0.0f });
        dummy.object->Update();
        demoDummies_.push_back(std::move(dummy));
        ++dummyIndex;
    }
    // 狙い・攻撃・移動の進み具合を最初に戻す
    demoTargetIndex_ = 0;
    demoAttackTimer_ = 0.0f;
    demoTravelTimer_ = 0.0f;
    demoMoveTrickUsed_ = false;
    demoBackstepTimer_ = 0.0f;

    // デモでは全武器を披露するため、実際の解放状況は退避しておいて終了時に必ず戻す
    // （Reset()せずUnlockAll()すると装備選択には影響しない＝CONTINUE直前でも安全）
    weaponManagerSnapshot_ = WeaponManager::GetInstance()->SaveSnapshot();
    WeaponManager::GetInstance()->UnlockAll();
    demoWeaponIndex_ = 0;
    if (!WeaponManager::GetInstance()->GetList().empty()) {
        WeaponManager::GetInstance()->EquipForDemo(WeaponManager::GetInstance()->GetList()[0].type);
    }
}

/** @brief デモ1フレームぶんの判断材料と、プレイヤーに押させるボタン */
struct TitleScene::DemoFrame {
    float playerX = 0.0f; ///< このフレーム開始時のプレイヤーのX座標
    DemoDummy* target = nullptr; ///< 狙っている的（いなければ nullptr）
    float targetDeltaX = 0.0f; ///< プレイヤーから的までのX方向の差
    bool inRange = false; ///< 的が攻撃の間合いに入っているか
    bool walkingRight = false; ///< 右へ歩かせるか
    bool walkingLeft = false; ///< 左へ歩かせるか
    bool dodgeAwayFromTarget = false; ///< 回避を的から離れる向きに出すか
    std::array<bool, static_cast<size_t>(Input::Action::kCount)> pressed {}; ///< 押させるボタン（Input::Action の並び順）

    /** @brief ボタンを押す（on=false で離す） */
    void Press(Input::Action action, bool on = true) {
        pressed[static_cast<size_t>(action)] = on;
    }
    /** @brief ボタンを押させることにしたか */
    bool IsPressed(Input::Action action) const {
        return pressed[static_cast<size_t>(action)];
    }
};

/** @brief 背景デモを1フレーム進める（Inputのアクションオーバーライドでプレイヤーを自動操作する） */
void TitleScene::UpdateDemo() {
    const TitleDemoTuning& tune = TitleDemoTune();
    const float arenaLeft = tune.arenaMinX + tune.arenaEdgeMargin;
    const float arenaRight = tune.arenaMaxX - tune.arenaEdgeMargin;
    // 倒された敵は戦闘範囲の外から個別に復帰する。ほかの状態は巻き戻さず、
    // タイトル表示中はひと続きの戦闘としてデモを動かし続ける。
    RespawnDefeatedDummies(arenaLeft, arenaRight);

    // 敵を動かしてから、プレイヤーが狙う的と間合いを決める
    DemoFrame frame;
    frame.playerX = player_->GetPosition().x;
    UpdateDummyBehavior(frame.playerX);
    frame.target = SelectNearestDummy(frame.playerX);
    // 回避中は回避方向を向いたままにする。前転モーションのまま後ろへ動くと滑って見える
    if (frame.target != nullptr && !player_->IsDodging()) {
        player_->FaceTarget({ frame.target->x, frame.target->currentY, 0.0f });
    }
    frame.inRange = frame.target != nullptr && std::abs(frame.target->x - frame.playerX) <= tune.approachStopDistance;
    frame.targetDeltaX = frame.target != nullptr ? frame.target->x - frame.playerX : 0.0f;
    frame.walkingRight = frame.target != nullptr && !frame.inRange && frame.targetDeltaX > 0.0f;
    frame.walkingLeft = frame.target != nullptr && !frame.inRange && frame.targetDeltaX < 0.0f;

    // 移動の判断→攻撃の判断の順に押すボタンを決め、プレイヤーへ渡す
    DecideDemoMovement(frame);
    UpdateDemoAttack(frame);
    ApplyDemoInput(frame);

    player_->Update(input_);
    // 攻撃の踏み込みは通常移動のクランプ後に加算されるため、描画前にも舞台内へ収める。
    Vector3 playerPosition = player_->GetPosition();
    playerPosition.x = std::clamp(playerPosition.x, arenaLeft, arenaRight);
    player_->SetPosition(playerPosition);
    player_->RefreshVisualTransforms();

    UpdateDummyVisuals();
    EmitDemoIdleTrail();

    // 闘技場を固定カメラで映し、独立した敵の動きを見やすくする。
    demoCameraX_ = tune.cameraX;
    camera_->SetTranslate({ demoCameraX_, tune.cameraY, gameConstants::kCameraDistanceZ });

    skydome_->Update(camera_.get());

    // 影の行列を更新し、通常モデルとスキンモデルの両方へ渡す
    shadowManager_->Update(objectCommon_->GetLightDirection());
    Object3d::SetLightViewProjection(shadowManager_->GetLightViewProjection());
    SkinnedObject3d::SetLightViewProjection(shadowManager_->GetLightViewProjection());
}

/** @brief 倒された的のうち復帰時間を過ぎたものを、プレイヤーの左右どちらかに出し直す */
void TitleScene::RespawnDefeatedDummies(float arenaLeft, float arenaRight) {
    const TitleDemoTuning& tune = TitleDemoTune();
    // 復帰ごとに位置・速さ・攻撃までの時間をばらつかせ、同じ動きの繰り返しに見せない
    std::uniform_real_distribution<float> respawnDistanceDist(tune.respawnDistanceMin, tune.respawnDistanceMax);
    std::uniform_real_distribution<float> moveSpeedDist(tune.respawnMoveSpeedMin, tune.respawnMoveSpeedMax);
    std::uniform_real_distribution<float> attackDelayDist(tune.respawnAttackDelayMin, tune.respawnAttackDelayMax);
    std::uniform_real_distribution<float> phaseDist(0.0f, gameConstants::kTwoPi);
    std::bernoulli_distribution sideDist(std::clamp(tune.respawnRightChance, 0.0f, 1.0f));
    for (auto& dummy : demoDummies_) {
        // 倒されていて、復帰の待ち時間が終わった的だけを出し直す
        if (!dummy.defeated) {
            continue;
        }
        dummy.respawnTimer -= gameConstants::kFrameDeltaTime;
        if (dummy.respawnTimer > 0.0f) {
            continue;
        }
        // プレイヤーの左右どちらかに、舞台からはみ出さない位置で地上に出す
        const float side = sideDist(demoRng_) ? 1.0f : -1.0f;
        dummy.x = std::clamp(player_->GetPosition().x + side * respawnDistanceDist(demoRng_), arenaLeft, arenaRight);
        dummy.airborne = false;
        dummy.baseY = tune.groundY + tune.dummyFootOffsetY;
        dummy.currentY = dummy.baseY;
        dummy.bobPhase = phaseDist(demoRng_);
        dummy.moveSpeed = moveSpeedDist(demoRng_);
        dummy.attackCooldown = attackDelayDist(demoRng_);
        // 撃破前の攻撃・被弾の途中状態を消す
        dummy.attackAnimTimer = 0.0f;
        dummy.lungeOffset = 0.0f;
        dummy.hitFlashTimer = 0.0f;
        dummy.defeated = false;
        // 見た目を通常の色・大きさに戻して、新しい位置へ置く
        dummy.object->SetColor(kDummyNormalColor);
        dummy.object->SetScale({ tune.dummyScale, tune.dummyScale, tune.dummyScale });
        dummy.object->SetPosition({ dummy.x, dummy.currentY, 0.0f });
        dummy.object->Update();
    }
}

/** @brief 生きている的をそれぞれの速度でプレイヤーへ近づけ、間合いに入ったら攻撃動作を始めさせる */
void TitleScene::UpdateDummyBehavior(float playerX) {
    const TitleDemoTuning& tune = TitleDemoTune();
    const float attackAnimSeconds = (std::max)(tune.dummyAttackAnimSeconds, gameConstants::kFrameDeltaTime);
    const int distanceVariants = (std::max)(tune.dummyPreferredDistanceVariants, 1);
    // 各敵は独立して行動する。それぞれの速度でプレイヤーに近づき、
    // 少しずつ異なる距離で止まって、個別の待機時間で攻撃する。
    for (size_t i = 0; i < demoDummies_.size(); ++i) {
        auto& dummy = demoDummies_[i];
        if (dummy.defeated) {
            continue;
        }
        // 止まる距離は的の番号ごとに数段階ずらす（全員が同じ位置に重ならないように）
        const float delta = playerX - dummy.x;
        const float distance = std::abs(delta);
        const float preferredDistance = tune.dummyPreferredDistanceBase
            + static_cast<float>(static_cast<int>(i) % distanceVariants) * tune.dummyPreferredDistanceStep;
        dummy.attackCooldown = (std::max)(0.0f,
            dummy.attackCooldown - gameConstants::kFrameDeltaTime);
        dummy.attackAnimTimer = (std::max)(0.0f,
            dummy.attackAnimTimer - gameConstants::kFrameDeltaTime);
        // 遠ければ近づき、止まる距離まで来たら待ち時間が明けしだい攻撃を始める
        if (distance > preferredDistance && dummy.attackAnimTimer <= 0.0f) {
            const float direction = delta >= 0.0f ? 1.0f : -1.0f;
            dummy.x += direction * dummy.moveSpeed * gameConstants::kFrameDeltaTime;
            dummy.x = std::clamp(dummy.x, tune.stageMinX + tune.arenaEdgeMargin, tune.stageMaxX - tune.arenaEdgeMargin);
        } else if (dummy.attackCooldown <= 0.0f) {
            dummy.attackAnimTimer = attackAnimSeconds;
            std::uniform_real_distribution<float> nextAttackDist(tune.dummyNextAttackMin, tune.dummyNextAttackMax);
            dummy.attackCooldown = nextAttackDist(demoRng_);
        }
        // 攻撃動作中はプレイヤー側へ半周期の sin で踏み込んで戻る
        const float attackPhase = 1.0f - dummy.attackAnimTimer / attackAnimSeconds;
        const float lungeDirection = delta >= 0.0f ? 1.0f : -1.0f;
        dummy.lungeOffset = dummy.attackAnimTimer > 0.0f
            ? std::sin(std::clamp(attackPhase, 0.0f, 1.0f) * gameConstants::kPi) * tune.dummyLungeDistance * lungeDirection
            : 0.0f;
    }
}

/** @brief 最も近い生きている的を狙いに決める */
TitleScene::DemoDummy* TitleScene::SelectNearestDummy(float playerX) {
    // デモのプレイヤーは最も近い生存中の敵に反応する。
    demoTargetIndex_ = -1;
    float closestDistance = FLT_MAX;
    for (int i = 0; i < static_cast<int>(demoDummies_.size()); ++i) {
        if (demoDummies_[i].defeated) {
            continue;
        }
        const float distance = std::abs(demoDummies_[i].x - playerX);
        if (distance < closestDistance) {
            closestDistance = distance;
            demoTargetIndex_ = i;
        }
    }
    return demoTargetIndex_ >= 0 ? &demoDummies_[demoTargetIndex_] : nullptr;
}

/** @brief 敵の攻撃の回避・攻撃間の後退・道中のジャンプや回避を決める */
void TitleScene::DecideDemoMovement(DemoFrame& frame) {
    const TitleDemoTuning& tune = TitleDemoTune();
    // 近くの敵が攻撃の当たる瞬間に入っていたら、的から離れる向きへ回避する（連続で回避しないよう待ち時間を置く）
    demoEnemyDodgeCooldown_ = (std::max)(0.0f,
        demoEnemyDodgeCooldown_ - gameConstants::kFrameDeltaTime);
    if (frame.target != nullptr && demoEnemyDodgeCooldown_ <= 0.0f) {
        for (const auto& enemy : demoDummies_) {
            const bool incomingStrike = !enemy.defeated
                && enemy.attackAnimTimer > tune.incomingStrikeWindowStart && enemy.attackAnimTimer < tune.incomingStrikeWindowEnd
                && std::abs(enemy.x - frame.playerX) < tune.incomingStrikeRange;
            if (incomingStrike) {
                frame.Press(Input::Action::kDodge);
                frame.dodgeAwayFromTarget = true;
                demoEnemyDodgeCooldown_ = tune.enemyDodgeCooldown;
                break;
            }
        }
    }
    demoBackstepTimer_ = (std::max)(demoBackstepTimer_ - gameConstants::kFrameDeltaTime, 0.0f);
    if (demoBackstepTimer_ > 0.0f) {
        // 対象から離れる方向へ短くステップする。
        frame.walkingRight = frame.targetDeltaX < 0.0f;
        frame.walkingLeft = frame.targetDeltaX >= 0.0f;
    }
    if (!frame.walkingRight && !frame.walkingLeft) {
        return;
    }
    demoTravelTimer_ += gameConstants::kFrameDeltaTime;
    // 空中敵へ高度を合わせる時だけジャンプする。地上敵には回避接近を混ぜる。
    /** @brief 的との距離と高さから接近アクションを選ぶ規則 */
    struct MoveTrick {
        bool (*applies)(const DemoDummy& target, float distance);
        Input::Action action;
    };
    static constexpr MoveTrick kMoveTricks[] = {
        {
            [](const DemoDummy& t, float distance) {
                return t.airborne && distance <= TitleDemoTune().jumpToAirborneRange;
            }, Input::Action::kJump
        },
        {
            [](const DemoDummy& t, float distance) {
                return !t.airborne && distance > TitleDemoTune().dodgeApproachMinDistance;
            }, Input::Action::kDodge
        },
    };
    // 移動を始めて少し経ったら、1区間に1回だけ当てはまった接近アクションを出す
    if (!demoMoveTrickUsed_ && demoTravelTimer_ >= tune.moveTrickDelay && frame.target != nullptr) {
        for (const MoveTrick& trick : kMoveTricks) {
            if (trick.applies(*frame.target, std::abs(frame.targetDeltaX))) {
                frame.Press(trick.action);
                demoMoveTrickUsed_ = true;
                break;
            }
        }
    }
}

/** @brief 間合いに入った的へコンボを振り、命中演出を出し、最後の段で撃破する */
void TitleScene::UpdateDemoAttack(DemoFrame& frame) {
    const TitleDemoTuning& tune = TitleDemoTune();
    DemoDummy* target = frame.target;
    // 空中の敵には遠距離攻撃を使い、ジャンプの高さが一致するのを待たない。
    const bool heightAligned = target != nullptr
        && (target->airborne
            || std::abs(player_->GetPosition().y - target->currentY) <= tune.hitHeightTolerance);
    if (target == nullptr || !frame.inRange || !heightAligned || demoBackstepTimer_ > 0.0f) {
        return;
    }
    // 一定間隔ごとの瞬間を攻撃1段とし、武器のコンボ段数まで数える
    demoAttackTimer_ += gameConstants::kFrameDeltaTime;
    const DemoWeaponStyle& style = DemoWeaponStyle::For(WeaponManager::GetInstance()->GetCurrent().type);
    const int attackCount = style.ComboLength();
    const float pulseInterval = (std::max)(tune.attackPulseInterval, gameConstants::kFrameDeltaTime);
    const int pulseIndex = static_cast<int>(demoAttackTimer_ / pulseInterval);
    const bool onPulseFrame = std::fmod(demoAttackTimer_, pulseInterval) < gameConstants::kFrameDeltaTime;
    if (onPulseFrame && pulseIndex < attackCount) {
        // 敵と周回に応じて通常コンボと武器固有技を織り交ぜる（上の行から順に見て、最初に当てはまった入力を押す）
        /** @brief 的とコンボ段数から攻撃アクションを選ぶ規則 */
        struct StrikeRule {
            bool (*applies)(const DemoDummy& target, const DemoWeaponStyle& style, int pulseIndex, int attackCount);
            Input::Action action;
        };
        static constexpr StrikeRule kStrikeRules[] = {
            {
                [](const DemoDummy& t, const DemoWeaponStyle&, int, int) {
                    return t.airborne;
                }, Input::Action::kShoot
            },
            {
                [](const DemoDummy&, const DemoWeaponStyle& s, int p, int c) {
                    return s.IsSkillPulse(p, c);
                }, Input::Action::kSkill
            },
            {
                [](const DemoDummy&, const DemoWeaponStyle&, int, int) {
                    return true;
                }, Input::Action::kAttack
            },
        };
        for (const StrikeRule& rule : kStrikeRules) {
            if (rule.applies(*target, style, pulseIndex, attackCount)) {
                frame.Press(rule.action);
                break;
            }
        }
        // 当てた的を点滅させ、命中演出を出す
        target->hitFlashTimer = tune.dummyHitFlashSeconds;
        EmitDemoHitEffects(frame);
        // 連打の合間に一歩引いて再接近させ、棒立ちコンボにならないようにする。
        if (pulseIndex + 1 < attackCount && ((pulseIndex + demoTargetIndex_) % 2) != 0) {
            demoBackstepTimer_ = tune.backstepSeconds;
        }
    }
    // コンボを振り切ったら撃破する
    if (pulseIndex >= attackCount) {
        DefeatDemoTarget(frame);
    }
}

/** @brief 攻撃1段ぶんの命中演出を出す */
void TitleScene::EmitDemoHitEffects(const DemoFrame& frame) {
    if (!particleManager_ || frame.target == nullptr) {
        return;
    }
    const TitleDemoTuning& tune = TitleDemoTune();
    // 演出の色・向き・位置を装備中の武器と的から決める（Zは演出用の手前の面へ寄せる）
    const auto& weapon = WeaponManager::GetInstance()->GetCurrent();
    const DemoWeaponStyle& style = DemoWeaponStyle::For(weapon.type);
    const Vector4 color = { weapon.effectColor[0], weapon.effectColor[1], weapon.effectColor[2], tune.hitEffectAlpha };
    const float facing = frame.targetDeltaX >= 0.0f ? 1.0f : -1.0f;
    Vector3 weaponPos = player_->GetActiveWeaponWorldPosition();
    weaponPos.z = tune.effectZ;
    const Vector3 targetPos = { frame.target->x, frame.target->currentY, tune.effectZ };
    const HitEffectPlayer fx(particleManager_, nullptr, &demoRng_);
    // 武器ごとの軌跡で、命中前から装備中の戦闘スタイルを見せる。
    fx.Trail(style.HitTrail(), weaponPos, color);
    fx.Star("title_hit", targetPos, { .color = color });
    style.EmitHit({ fx, weaponPos, targetPos, facing, color, frame.IsPressed(Input::Action::kSkill) });
    // 射撃なら銃口に発射の輪も出す
    if (frame.IsPressed(Input::Action::kShoot)) {
        fx.Ring("title_shot", weaponPos);
    }
}

/** @brief 狙っていた的を撃破して退場させ、次の的と次の武器へ切り替える */
void TitleScene::DefeatDemoTarget(DemoFrame& frame) {
    const TitleDemoTuning& tune = TitleDemoTune();
    DemoDummy* target = frame.target;
    // 撃破: 地面へ沈めて退場させ、復帰までの時間をばらつかせる
    std::uniform_real_distribution<float> respawnDelayDist(tune.respawnDelayMin, tune.respawnDelayMax);
    target->defeated = true;
    target->respawnTimer = respawnDelayDist(demoRng_);
    target->object->SetPosition({ target->x, target->currentY - tune.dummyDefeatSinkDepth, 0.0f });
    target->object->Update();
    // 次の的へ向かうので、狙いと接近・攻撃の進み具合を最初に戻す
    demoTargetIndex_ = -1;
    demoAttackTimer_ = 0.0f;
    demoTravelTimer_ = 0.0f;
    demoMoveTrickUsed_ = false;
    // 敵を1体倒すたびに武器を切り替え、同じコンボを連続して見せない。
    auto* wm = WeaponManager::GetInstance();
    const int weaponCount = wm->GetCount();
    if (weaponCount > 0) {
        demoWeaponIndex_ = (demoWeaponIndex_ + 1) % weaponCount;
        wm->EquipForDemo(wm->GetList()[demoWeaponIndex_].type);
    }
}

/** @brief 決めたボタンを Input のアクションオーバーライドへ書き込む */
void TitleScene::ApplyDemoInput(DemoFrame& frame) {
    frame.Press(Input::Action::kMoveRight, frame.walkingRight);
    frame.Press(Input::Action::kMoveLeft, frame.walkingLeft);
    // 回避は移動入力が無いと向いている方向の逆へ下がるので、進みたい側の移動入力も押して向きを決める
    if (frame.IsPressed(Input::Action::kDodge)) {
        const bool moveRight = frame.dodgeAwayFromTarget ? frame.targetDeltaX < 0.0f : frame.targetDeltaX >= 0.0f;
        frame.Press(moveRight ? Input::Action::kMoveRight : Input::Action::kMoveLeft);
    }
    // 実機の入力に一切左右されないよう、全アクションを毎フレーム明示的に指定する
    for (size_t i = 0; i < frame.pressed.size(); ++i) {
        input_->SetActionOverride(static_cast<Input::Action>(i), frame.pressed[i]);
    }
}

/** @brief 的の上下の揺れ・被弾時の点滅・向き・位置をモデルへ反映する */
void TitleScene::UpdateDummyVisuals() {
    const TitleDemoTuning& tune = TitleDemoTune();
    for (auto& dummy : demoDummies_) {
        dummy.hitFlashTimer = (std::max)(dummy.hitFlashTimer - gameConstants::kFrameDeltaTime, 0.0f);
        if (dummy.defeated) {
            continue;
        }
        // 空中の的だけ sin で上下に揺らす
        if (dummy.airborne) {
            dummy.bobPhase += gameConstants::kFrameDeltaTime * dummy.bobSpeed;
            dummy.currentY = dummy.baseY + std::sin(dummy.bobPhase) * tune.dummyBobAmplitude;
        } else {
            dummy.currentY = dummy.baseY;
        }
        // 被弾直後は色を変えて少し縮め、当たったことを見せる
        const bool flashing = dummy.hitFlashTimer > 0.0f;
        dummy.object->SetColor(flashing ? tune.dummyFlashColor : kDummyNormalColor);
        const float shrink = flashing ? tune.dummyFlashShrink : 1.0f;
        dummy.object->SetScale({ tune.dummyScale * shrink, tune.dummyScale * shrink, tune.dummyScale * shrink });
        // プレイヤーのいる側を向かせ、攻撃の踏み込みぶんずらして置く
        const float facingYaw = player_->GetPosition().x >= dummy.x
            ? 0.0f
            : gameConstants::kPi;
        dummy.object->SetRotation({ 0.0f, facingYaw, 0.0f });
        dummy.object->SetPosition({ dummy.x + dummy.lungeOffset, dummy.currentY, 0.0f });
        dummy.object->Update();
    }
}

/** @brief 装備中の武器が常に出している軌跡を出す */
void TitleScene::EmitDemoIdleTrail() {
    // 通常はゲームプレイ画面が継続的な武器の軌跡を出す。タイトルのデモでは
    // プレイヤーを直接描画するため、装備中の武器の軌跡もここで生成する。
    if (!particleManager_) {
        return;
    }
    const TitleDemoTuning& tune = TitleDemoTune();
    const WeaponData& weapon = WeaponManager::GetInstance()->GetCurrent();
    const Vector4 trailColor = ColorFromArray(weapon.effectColor);
    Vector3 trailPos = player_->GetActiveWeaponWorldPosition();
    trailPos.z = tune.effectZ;
    // 攻撃中ははっきり、待機中は薄く小さく出す
    const bool attacking = player_->IsMeleeAttacking();
    const HitEffectPlayer fx(particleManager_, nullptr, &demoRng_);
    fx.Trail(DemoWeaponStyle::For(weapon.type).IdleTrail(), trailPos,
        { trailColor.x, trailColor.y, trailColor.z, attacking ? 1.0f : tune.idleTrailAlpha },
        { .scale = attacking ? 1.0f : tune.idleTrailScaleRatio });
}


} // namespace engine::game
