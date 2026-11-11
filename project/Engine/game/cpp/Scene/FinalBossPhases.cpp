/**
 * @file FinalBossPhases.cpp
 * @brief ラスボス戦の段階（武器が集まって刀になる・名乗り・戦闘・後半戦の演出・撃破）
 * @note 状態は FinalBossBattle が持ち、段階は時間の進め方と見せ方だけを決める
 */
#include "FinalBossBattle.h"
#include "CharacterVisuals.h"
#include "Easing.h"
#include "EnemyEntity.h"
#include "FinalBossTuning.h"
#include "GameConstants.h"
#include "HitEffectLibrary.h"
#include "Input.h"
#include "Player.h"
#include "ScreenFlash.h"
#include "StringUtility.h"
#include "TimeManager.h"
#include <algorithm>
#include <cmath>

using namespace engine::graphics;

namespace engine::game {
namespace {
// 武器が集まる時のカメラ（プレイヤーの斜め前から）
constexpr Vector3 kGatherEyeOffset = { 2.6f, 1.4f, -4.8f };
constexpr float kGatherTargetHeight = 1.3f;
constexpr float kGatherStartHeight = 0.3f; ///< 回り始める高さ（体の中心から）
constexpr float kGatherTilt = 0.35f; ///< 回る武器の傾き
constexpr float kKatanaSpin = 6.0f; ///< 生まれた刀が回る速さ（毎秒のラジアン）
constexpr float kKatanaHandHeight = 0.3f; ///< 刀が降りて消える高さ（体の中心から）
// 名乗り
constexpr float kIntroStartAngle = -2.2f; ///< 回るカメラの始めの角度（ボスの正面寄り）
constexpr float kIntroTargetHeight = 0.8f;
constexpr float kDrawSlowMult = 2.0f; ///< 抜き放つ段をふつうの何倍の長さで見せるか
constexpr float kNameFadeIn = 0.4f;
constexpr float kNameFadeOut = 0.5f;
constexpr float kIntroLineDelay = 1.1f; ///< 抜き放ってから台詞を出すまで
// 後半戦と撃破のカメラ（ボスの前から寄る）
constexpr Vector3 kCloseEyeOffset = { 3.2f, 1.2f, -2.4f };
constexpr float kCloseTargetHeight = 0.9f;
constexpr float kRageLean = 0.25f; ///< 後半戦に入る時に前へかがむ角度
constexpr float kLetterboxIn = 1.0f;
constexpr float kLetterboxOut = 0.0f;
constexpr Vector4 kRageFlashColor = { 1.0f, 0.25f, 0.2f, 0.6f };
constexpr float kRageFlashSeconds = 0.5f;
constexpr Vector4 kDefeatFlashColor = { 1.0f, 1.0f, 1.0f, 0.8f };
constexpr float kDefeatFlashSeconds = 0.4f;

/** @brief 調整値の短い呼び名 */
const FinalBossTuning& Tune() {
    return FinalBossTuningStore::Get();
}

/** @brief 前の時刻から今の時刻までの間に mark を越えたか */
bool Crossed(float before, float after, float mark) {
    return before < mark && after >= mark;
}

/** @brief 値を目標へ一定の速さで寄せる */
float Approach(float value, float target, float step) {
    return value < target ? (std::min)(value + step, target) : (std::max)(value - step, target);
}
} // namespace

// ══════════════════════════════════════════════════════
// 段階の型
// ══════════════════════════════════════════════════════

/** @brief ラスボス戦の1段階（Enter で始め、毎フレーム Update を呼ぶ） */
class FinalBossBattle::IPhase {
public:
    virtual ~IPhase() = default;
    /** @brief 段階を始める */
    virtual void Enter(FinalBossBattle& battle) const = 0;
    /** @brief 1フレーム進める */
    virtual void Update(FinalBossBattle& battle, float deltaTime) const = 0;
    /** @brief プレイヤーの操作と通常の戦闘を止めるか */
    virtual bool BlocksGameplay(const FinalBossBattle&) const {
        return true;
    }
};

/** @brief 持っている武器がプレイヤーの周りを回って頭上へ集まり、光って刀になり、手元へ降りる */
class FinalBossBattle::GatherPhase : public FinalBossBattle::IPhase {
public:
    /** @brief 共有の実体 */
    static const GatherPhase& Instance() {
        static const GatherPhase kInstance;
        return kInstance;
    }
    /** @brief 武器を並べ、ボスを隠して始める */
    void Enter(FinalBossBattle& battle) const override {
        battle.phaseTimer_ = 0.0f;
        battle.bossVanished_ = true;
        battle.gatherVisible_ = true;
        battle.katanaVisible_ = false;
        battle.fightCamera_ = false;
    }
    /** @brief 回る → 光る → 降りる、の順に進め、降り切ったら白い幕で名乗りへ */
    void Update(FinalBossBattle& battle, float deltaTime) const override;
};

/** @brief ボスが居合の構えから抜き放ち、名乗る（カメラはボスの周りを回る） */
class FinalBossBattle::IntroPhase : public FinalBossBattle::IPhase {
public:
    /** @brief 共有の実体 */
    static const IntroPhase& Instance() {
        static const IntroPhase kInstance;
        return kInstance;
    }
    /** @brief ボスを出して構えさせる */
    void Enter(FinalBossBattle& battle) const override {
        battle.phaseTimer_ = 0.0f;
        battle.bossVanished_ = false;
        battle.nameReveal_ = 0.0f;
        battle.pose_ = { };
        battle.pose_.attack = KatanaStep(0);
    }
    /** @brief 白い幕を引き、回るカメラで抜き放ちと名乗りを見せる */
    void Update(FinalBossBattle& battle, float deltaTime) const override;
};

/** @brief 戦闘の前に闘技場の操作説明を出し、決定キーで戦闘を始める（カメラはもうプレイヤーの後ろ） */
class FinalBossBattle::GuidePhase : public FinalBossBattle::IPhase {
public:
    /** @brief 共有の実体 */
    static const GuidePhase& Instance() {
        static const GuidePhase kInstance;
        return kInstance;
    }
    /** @brief 後ろのカメラと体力バーを出し、操作説明を出し始める */
    void Enter(FinalBossBattle& battle) const override {
        battle.phaseTimer_ = 0.0f;
        battle.fightCamera_ = true;
        battle.barVisible_ = true;
        battle.guideVisible_ = true;
        battle.pose_ = { };
    }
    /** @brief 決定を待ち、押されたら操作説明を消して戦闘へ */
    void Update(FinalBossBattle& battle, float deltaTime) const override;
};

/** @brief 戦闘（ボスが技を選んで仕掛け、体力が半分を切ったら後半戦、尽きたら撃破） */
class FinalBossBattle::FightPhase : public FinalBossBattle::IPhase {
public:
    /** @brief 共有の実体 */
    static const FightPhase& Instance() {
        static const FightPhase kInstance;
        return kInstance;
    }
    /** @brief プレイヤーの後ろのカメラと体力バーを出して始める */
    void Enter(FinalBossBattle& battle) const override {
        battle.fightCamera_ = true;
        battle.barVisible_ = true;
        battle.pose_ = { };
        battle.thinkTimer_ = Tune().thinkMaxSeconds;
    }
    /** @brief 戦闘中は操作を止めない */
    bool BlocksGameplay(const FinalBossBattle&) const override {
        return false;
    }
    /** @brief ボスの技と撃ち出す剣を進め、体力で段階を切り替える */
    void Update(FinalBossBattle& battle, float deltaTime) const override;
};

/** @brief 後半戦に入る短い演出（ボスに寄り、色が変わって台詞） */
class FinalBossBattle::RagePhase : public FinalBossBattle::IPhase {
public:
    /** @brief 共有の実体 */
    static const RagePhase& Instance() {
        static const RagePhase kInstance;
        return kInstance;
    }
    /** @brief 技を打ち切り、赤い閃光と台詞で始める */
    void Enter(FinalBossBattle& battle) const override {
        battle.CancelMoves();
        battle.phaseTimer_ = 0.0f;
        battle.fightCamera_ = false;
        battle.line_ = StringUtility::ConvertString(Tune().rageLine);
        ScreenFlash::GetInstance()->Request(kRageFlashColor, kRageFlashSeconds);
        const HitEffectPlayer fx(battle.links_.particles, battle.links_.shaker, battle.links_.rng);
        fx.Impact("lastboss_rage");
        fx.Ring("lastboss_rage", { battle.bossX_, Tune().shadowBaseY, 0.0f });
    }
    /** @brief ボスに寄って見せ、時間が来たら戦闘へ戻す */
    void Update(FinalBossBattle& battle, float deltaTime) const override;
};

/** @brief 撃破（スローの中で後ろへ倒れ、操作を返したら落とした刀を奪わせる） */
class FinalBossBattle::DefeatPhase : public FinalBossBattle::IPhase {
public:
    /** @brief 共有の実体 */
    static const DefeatPhase& Instance() {
        static const DefeatPhase kInstance;
        return kInstance;
    }
    /** @brief 技を打ち切り、スローと閃光で始める */
    void Enter(FinalBossBattle& battle) const override {
        battle.CancelMoves();
        battle.phaseTimer_ = 0.0f;
        battle.fightCamera_ = false;
        battle.barVisible_ = false;
        battle.bossVanished_ = false;
        TimeManager::GetInstance()->RequestSlowMotion(Tune().defeatSlowSeconds, Tune().defeatSlowScale);
        ScreenFlash::GetInstance()->Request(kDefeatFlashColor, kDefeatFlashSeconds);
        const HitEffectPlayer fx(battle.links_.particles, battle.links_.shaker, battle.links_.rng);
        fx.Impact("lastboss_defeat");
        fx.Ring("lastboss_defeat", { battle.bossX_, Tune().shadowBaseY, 0.0f });
    }
    /** @brief 倒れる演出の間だけ操作を止める */
    bool BlocksGameplay(const FinalBossBattle& battle) const override {
        return battle.phaseTimer_ < Tune().defeatCinematicSeconds;
    }
    /** @brief 後ろへ倒し、演出が済んだら後ろのカメラと奪う案内に切り替える */
    void Update(FinalBossBattle& battle, float deltaTime) const override {
        const FinalBossTuning& t = Tune();
        battle.phaseTimer_ += deltaTime;
        const float fall = easing::EaseOutQuad(easing::Progress(battle.phaseTimer_, t.defeatFallSeconds));
        battle.pose_ = { };
        battle.pose_.lean = { -t.defeatLieAngle * fall, 0.0f, 0.0f };
        battle.bossLift_ = t.defeatLieLift * fall;
        battle.UpdatePhantoms(deltaTime);
        if (BlocksGameplay(battle)) {
            Player& player = *battle.links_.player;
            player.UpdateCinematicPose(!player.IsOnGround());
            battle.letterbox_ = Approach(battle.letterbox_, kLetterboxIn, deltaTime / t.letterboxSeconds);
            const float side = battle.bossFacing_;
            const Vector3 boss = { battle.bossX_, t.shadowBaseY, 0.0f };
            battle.desiredShot_ = { { boss.x + side * kCloseEyeOffset.x, boss.y + kCloseEyeOffset.y, boss.z + kCloseEyeOffset.z },
                { boss.x, boss.y + kCloseTargetHeight, boss.z } };
            return;
        }
        battle.fightCamera_ = true;
        battle.letterbox_ = Approach(battle.letterbox_, kLetterboxOut, deltaTime / t.letterboxSeconds);
        battle.line_ = StringUtility::ConvertString(t.stealLine);
        battle.lineAlpha_ = 1.0f;
    }
};

// ══════════════════════════════════════════════════════
// 次の段階へ進む Update（どの段階も定義し終えてから書く）
// ══════════════════════════════════════════════════════

/** @brief 回る → 光る → 降りる、の順に進め、降り切ったら白い幕で名乗りへ */
void FinalBossBattle::GatherPhase::Update(FinalBossBattle& battle, float deltaTime) const {
    const FinalBossTuning& t = Tune();
    const float before = battle.phaseTimer_;
    battle.phaseTimer_ += deltaTime;
    const float now = battle.phaseTimer_;
    const float forgeAt = t.gatherSeconds;
    const float descendAt = forgeAt + t.forgeSeconds;
    const float endAt = descendAt + t.descendSeconds;
    Player& player = *battle.links_.player;
    const Vector3 center = player.GetPosition();
    player.UpdateCinematicPose(false);
    battle.letterbox_ = Approach(battle.letterbox_, kLetterboxIn, deltaTime / t.letterboxSeconds);

    // 回る武器: 半径を縮めながら頭上へ上がる
    const float gather = easing::Progress(now, t.gatherSeconds);
    const float radius = t.gatherRadius * (1.0f - easing::EaseInQuad(gather));
    const float height = kGatherStartHeight + (t.gatherHeight - kGatherStartHeight) * easing::EaseOutQuad(gather);
    for (GatherWeapon& weapon : battle.gatherWeapons_) {
        const float angle = weapon.angle + t.gatherSpin * now;
        weapon.object->SetPosition({ center.x + std::cos(angle) * radius, center.y + height, center.z + std::sin(angle) * radius });
        weapon.object->SetRotation({ 0.0f, angle, kGatherTilt });
    }
    // 集まり切った瞬間に光って、武器は一振りの刀になる
    const HitEffectPlayer fx(battle.links_.particles, battle.links_.shaker, battle.links_.rng);
    if (Crossed(before, now, forgeAt)) {
        battle.gatherVisible_ = false;
        battle.katanaVisible_ = true;
        ScreenFlash::GetInstance()->Request(t.forgeFlashColor, t.forgeSeconds);
        fx.Impact("lastboss_forge");
        fx.Ring("lastboss_forge", { center.x, center.y + t.gatherHeight, center.z });
    }
    // 刀は頭上で回り、時間が来たら手元へ降りる
    const float descend = easing::Progress(now - descendAt, t.descendSeconds);
    const float katanaY = center.y + t.gatherHeight + (kKatanaHandHeight - t.gatherHeight) * easing::EaseInOutQuad(descend);
    battle.forgedKatana_->SetPosition({ center.x, katanaY, center.z });
    battle.forgedKatana_->SetRotation({ 0.0f, kKatanaSpin * now, 0.0f });
    // 降り切ったら持ち物を刀だけにして、白い幕で闘技場の名乗りへ
    if (now >= endAt || battle.links_.input->TriggerCinematicSkip()) {
        battle.EquipKatanaOnly();
        battle.gatherVisible_ = false;
        battle.katanaVisible_ = false;
        battle.whiteVeil_ = 1.0f;
        battle.ChangePhase(IntroPhase::Instance());
        return;
    }
    battle.desiredShot_ = { { center.x + kGatherEyeOffset.x, center.y + kGatherEyeOffset.y, center.z + kGatherEyeOffset.z },
        { center.x, center.y + kGatherTargetHeight, center.z } };
}

/** @brief 白い幕を引き、回るカメラで抜き放ちと名乗りを見せる */
void FinalBossBattle::IntroPhase::Update(FinalBossBattle& battle, float deltaTime) const {
    const FinalBossTuning& t = Tune();
    const float before = battle.phaseTimer_;
    battle.phaseTimer_ += deltaTime;
    const float now = battle.phaseTimer_;
    Player& player = *battle.links_.player;
    player.FaceTarget({ battle.bossX_, player.GetPosition().y, 0.0f });
    player.UpdateCinematicPose(false);
    battle.whiteVeil_ = 1.0f - easing::Progress(now, t.veilSeconds);
    battle.letterbox_ = kLetterboxIn;

    // 構え（段の始めの姿勢）→ 抜き放つ（段をゆっくり通す）→ 構えを解く
    const float drawSeconds = t.slashSeconds * kDrawSlowMult;
    const float drawn = (now - t.introDrawTime) / drawSeconds;
    battle.pose_.attack = drawn < 1.0f ? KatanaStep(0) : nullptr;
    battle.pose_.attackTime = std::clamp(drawn, 0.0f, 1.0f);
    if (Crossed(before, now, t.introDrawTime)) {
        const HitEffectPlayer fx(battle.links_.particles, battle.links_.shaker, battle.links_.rng);
        const Vector3 bossPos = { battle.bossX_, t.shadowBaseY, 0.0f };
        fx.Impact("lastboss_draw");
        fx.Slash("lastboss_draw", bossPos, battle.bossFacing_);
    }
    // 抜き放った後に名前と台詞を出し、終わり際に名前を消す
    const float nameIn = easing::Progress(now - t.introDrawTime, kNameFadeIn);
    const float nameOut = easing::Progress(now - (t.introSeconds - kNameFadeOut), kNameFadeOut);
    battle.nameReveal_ = nameIn * (1.0f - nameOut);
    if (now >= t.introDrawTime + kIntroLineDelay) {
        battle.line_ = StringUtility::ConvertString(t.introLine);
        battle.lineAlpha_ = 1.0f;
    }

    const float angle = kIntroStartAngle + t.introOrbitSpeed * now;
    const Vector3 boss = { battle.bossX_, t.shadowBaseY, 0.0f };
    battle.desiredShot_ = { { boss.x + std::sin(angle) * t.introOrbitRadius, boss.y + t.introCameraHeight, boss.z + std::cos(angle) * t.introOrbitRadius },
        { boss.x, boss.y + kIntroTargetHeight, boss.z } };
    if (now >= t.introSeconds || battle.links_.input->TriggerCinematicSkip()) {
        battle.whiteVeil_ = 0.0f;
        battle.nameReveal_ = 0.0f;
        battle.ChangePhase(GuidePhase::Instance());
    }
}

/** @brief 決定を待ち、押されたら操作説明を消して戦闘へ */
void FinalBossBattle::GuidePhase::Update(FinalBossBattle& battle, float deltaTime) const {
    battle.phaseTimer_ += deltaTime;
    Player& player = *battle.links_.player;
    player.FaceTarget({ battle.bossX_, player.GetPosition().y, 0.0f });
    player.UpdateCinematicPose(false);
    battle.bossFacing_ = battle.DirectionToPlayer();
    battle.letterbox_ = Approach(battle.letterbox_, kLetterboxOut, deltaTime / Tune().letterboxSeconds);
    // 出してすぐは受け付けない（名乗りを飛ばした勢いで読まずに閉じないように）
    if (battle.phaseTimer_ < Tune().guideMinSeconds) {
        return;
    }
    Input* input = battle.links_.input;
    if (input->TriggerMenuConfirm() || input->TriggerAction(Input::Action::kAttack)) {
        battle.guideVisible_ = false;
        battle.ChangePhase(FightPhase::Instance());
    }
}

/** @brief ボスの技と撃ち出す剣を進め、体力で段階を切り替える */
void FinalBossBattle::FightPhase::Update(FinalBossBattle& battle, float deltaTime) const {
    battle.letterbox_ = Approach(battle.letterbox_, kLetterboxOut, deltaTime / Tune().letterboxSeconds);
    if (battle.boss_->IsDefeated()) {
        battle.ChangePhase(DefeatPhase::Instance());
        return;
    }
    const float rageHp = static_cast<float>(battle.boss_->GetMaxHp()) * Tune().rageHpRatio;
    if (!battle.rageStarted_ && static_cast<float>(battle.boss_->GetHp()) <= rageHp) {
        battle.rageStarted_ = true;
        battle.ChangePhase(RagePhase::Instance());
        return;
    }
    battle.UpdateBoss(deltaTime);
    battle.UpdatePhantoms(deltaTime);
}

/** @brief ボスに寄って見せ、時間が来たら戦闘へ戻す */
void FinalBossBattle::RagePhase::Update(FinalBossBattle& battle, float deltaTime) const {
    battle.phaseTimer_ += deltaTime;
    Player& player = *battle.links_.player;
    player.UpdateCinematicPose(!player.IsOnGround());
    battle.letterbox_ = Approach(battle.letterbox_, kLetterboxIn, deltaTime / Tune().letterboxSeconds);
    battle.lineAlpha_ = 1.0f;
    battle.pose_ = { };
    battle.pose_.lean = { kRageLean, 0.0f, 0.0f };
    const float side = battle.bossFacing_;
    const Vector3 boss = { battle.bossX_, Tune().shadowBaseY, 0.0f };
    battle.desiredShot_ = { { boss.x + side * kCloseEyeOffset.x, boss.y + kCloseEyeOffset.y, boss.z + kCloseEyeOffset.z },
        { boss.x, boss.y + kCloseTargetHeight, boss.z } };
    if (battle.phaseTimer_ >= Tune().rageSeconds) {
        battle.ChangePhase(FightPhase::Instance());
    }
}

// ══════════════════════════════════════════════════════
// 段階の切り替えと毎フレームの入口
// ══════════════════════════════════════════════════════

/** @brief レベルの読み込み後に、ボスの実体を受け取って演出を始める */
void FinalBossBattle::Start(EnemyEntity* boss) {
    if (!initialized_ || boss == nullptr) {
        return;
    }
    const FinalBossTuning& t = Tune();
    // ボスの実体は判定・体力・撃破後に落とす刀だけに使い、見た目は分身が受け持つ
    boss_ = boss;
    boss_->SetScripted(true);
    boss_->SetBodyHidden(true);
    boss_->SetMaxHp(t.bossMaxHp);
    lastBossHp_ = boss_->GetHp();
    bossBaseY_ = boss_->GetPosition().y;

    // プレイヤーの開始位置の先にボスを置き、2人の真ん中を闘技場の中心にする
    const Vector3 playerPos = links_.player->GetPosition();
    bossX_ = playerPos.x + t.startDistance;
    bossFacing_ = -1.0f;
    bossLift_ = 0.0f;
    arenaCenter_ = { playerPos.x + t.startDistance * 0.5f, 0.0f, 0.0f };
    arenaYaw_ = 0.0f;
    cameraYaw_ = gameConstants::kHalfPi;
    rageStarted_ = false;
    guideVisible_ = false;
    guideAlpha_ = 0.0f;
    links_.player->FaceTarget({ bossX_, playerPos.y, 0.0f });

    // 集まる武器は、持っている近接武器を等間隔に並べる（何も無ければ刀だけ）
    gatherWeapons_.clear();
    const WeaponManager* weapons = WeaponManager::GetInstance();
    for (const HeldWeaponVisual& visual : kHeldWeaponVisuals) {
        if (weapons->Owns(visual.type)) {
            gatherWeapons_.push_back({ CreateWeaponObject(links_.modelCommon, visual.type, t.gatherScale), 0.0f });
        }
    }
    if (gatherWeapons_.empty()) {
        gatherWeapons_.push_back({ CreateWeaponObject(links_.modelCommon, WeaponType::kSword, t.gatherScale), 0.0f });
    }
    for (size_t i = 0; i < gatherWeapons_.size(); ++i) {
        gatherWeapons_[i].angle = gameConstants::kTwoPi * static_cast<float>(i) / static_cast<float>(gatherWeapons_.size());
    }
    ChangePhase(GatherPhase::Instance());
    UpdateCommon(0.0f);
}

/** @brief 段階を切り替える */
void FinalBossBattle::ChangePhase(const IPhase& next) {
    phase_ = &next;
    next.Enter(*this);
}

/** @brief 演出の途中で、プレイヤーの操作と通常の戦闘を止めるか */
bool FinalBossBattle::BlocksGameplay() const {
    return phase_ != nullptr && phase_->BlocksGameplay(*this);
}

/** @brief 操作を止めている演出の間の1フレーム */
void FinalBossBattle::UpdateCinematic() {
    if (phase_ == nullptr) {
        return;
    }
    // 演出の間はプレイヤーを動かさないので、操作の差し替えも外しておく
    ClearInputOverrides();
    const float dt = TimeManager::GetInstance()->GetDeltaTime();
    telegraph_.BeginFrame();
    phase_->Update(*this, dt);
    UpdateCommon(dt);
}

/** @brief プレイヤーの更新と戦闘の判定の後に呼ぶ */
void FinalBossBattle::UpdateFight(float deltaTime) {
    if (phase_ == nullptr) {
        return;
    }
    UpdatePlayerFacing(deltaTime);
    // 予兆の印は技がこのフレームに置き直す
    telegraph_.BeginFrame();
    telegraphClock_ += deltaTime;
    phase_->Update(*this, deltaTime);
    UpdateCommon(deltaTime);
}

} // namespace engine::game
