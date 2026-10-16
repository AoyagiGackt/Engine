/**
 * @file PlayerWeaponBehaviors.cpp
 * @brief 武器種別ごとの固有技（Weapon Behavior Strategy）と、その状態を持つBehaviorの実装
 */
#include "Easing.h"
#include "GameConstants.h"
#include "Input.h"
#include "Player.h"
#include "PlayerTuning.h"
#include "Weapon.h"
#include <algorithm>
#include <array>
#include <cmath>

using namespace engine;
using namespace engine::game;

namespace {
const PlayerTuning& Tuning()
{
    return PlayerTuning::Get();
}

/** @brief クールタイムを1フレームぶん減らす（0で止める） */
void Cooldown(float& timer, float step)
{
    timer = (std::max)(timer - step, 0.0f);
}
} // namespace

namespace engine::game {

/** @brief 固有技の連撃1種ぶんの設定（どの連撃表を使い、クールタイムはどの調整値か） */
struct SkillSequenceDef {
    WeaponType type;
    float PlayerTuning::* cooldownSeconds;
    bool groundOnly; ///< 地上でだけ出せるか
    bool slamsOnFinish; ///< 締めで地面を叩いて衝撃波を出すか
    bool rampageWhenAwakened; ///< 覚醒中の攻撃ボタンが覚醒乱舞になるか
};

constexpr SkillSequenceDef kSwordSequence = { WeaponType::Sword, &PlayerTuning::swordSkillCooldown, false, false, true };
constexpr SkillSequenceDef kSpearSequence = { WeaponType::Spear, &PlayerTuning::spearSkillCooldown, false, false, false };
constexpr SkillSequenceDef kHammerSequence = { WeaponType::Hammer, &PlayerTuning::hammerSkillCooldown, true, true, false };
constexpr SkillSequenceDef kDaggerSequence = { WeaponType::Dagger, &PlayerTuning::daggerSkillCooldown, false, false, false };

class Player::SkillSequenceBehavior : public IWeaponBehavior {
public:
    explicit SkillSequenceBehavior(const SkillSequenceDef& def)
        : def_(def)
    {
    }

    void Update(Player& player, Input* input) override
    {
        if (def_.groundOnly && !player.IsOnGround()) {
            return;
        }
        player.TryStartSkillSequence(input, def_.type, cooldown_, Tuning().*def_.cooldownSeconds);
    }
    void Tick(Player&, Input*, float cooldownStep) override { Cooldown(cooldown_, cooldownStep); }
    bool StartsRampage(const Player& player) const override { return def_.rampageWhenAwakened && player.IsAwakened(); }
    bool SlamsOnSkillFinish() const override { return def_.slamsOnFinish; }

private:
    const SkillSequenceDef& def_;
    float cooldown_ = 0.0f;
};

class Player::DaggerBehavior final : public SkillSequenceBehavior {
public:
    DaggerBehavior()
        : SkillSequenceBehavior(kDaggerSequence)
    {
    }

    void Update(Player& player, Input* input) override
    {
        // 空中ダッシュ（機動力枠）。滑っている間は落下を止め、足場から足場へ飛び移れるようにする
        if (airDash_.active) {
            player.StopVerticalMotion();
            player.AdvanceDash(airDash_);
            return;
        }
        if (player.IsOnGround()) {
            // 地上では乱れ斬り: 回転を挟んだ高速の斬撃を重ね、回りながら斬り上げて敵を打ち飛ばす
            SkillSequenceBehavior::Update(player, input);
            return;
        }
        if (input->TriggerAction(Input::Action::Skill) && airDashAvailable_) {
            const PlayerTuning& t = Tuning();
            airDashAvailable_ = false;
            player.StopVerticalMotion();
            player.BeginDash(airDash_, player.GetLastDirX() * t.daggerAirDashDist * player.GetSkillMods().blinkDistMult);
            player.PlaySkillAnim(&CharacterRig::runningJumpAnim, t.daggerAirDashAnimSpeed);
        }
    }
    void Interrupt() override { airDash_.active = false; }
    void OnLanded() override { airDashAvailable_ = true; }

private:
    DashMotion airDash_;
    bool airDashAvailable_ = true;
};

class Player::BallBehavior final : public IWeaponBehavior {
public:
    void Update(Player& player, Input* input) override
    {
        // スピン連射 + 空中くるくる
        if (!input->PushAction(Input::Action::Skill)) {
            return;
        }
        const PlayerTuning& t = Tuning();
        if (input->TriggerAction(Input::Action::Skill)) {
            player.PlaySkillAnim(&CharacterRig::punchAnim, t.ballSpinShotAnimSpeed);
        }
        if (shootCooldown_ <= 0.0f) {
            player.SetFrameEvent(FrameEvent::SpinShot);
            shootCooldown_ = t.ballShootInterval * player.GetSkillMods().fireIntervalMult;
        }
        if (!player.IsOnGround()) {
            player.AddSpin(t.ballSpinSpeed);
        }
    }
    void Tick(Player&, Input*, float) override { Cooldown(shootCooldown_, GameConstants::kFrameDeltaTime); }
    // 転がっている間は空中で回り続け、着地したら戻す
    bool KeepsSpin(const Player& player) const override { return !player.IsOnGround(); }

private:
    float shootCooldown_ = 0.0f;
};

class Player::GreatswordBehavior final : public IWeaponBehavior {
public:
    void Update(Player& player, Input* input) override
    {
        // 投げ回転斬り 大剣そのものを投げ、途中で静止して渦のように回転し、周囲の敵を巻き込みながら多段ヒットする
        // 発生（投げる瞬間）だけここで扱う。進行中の状態（飛行→渦→帰還）はTick()が装備武器に関係なく毎フレーム進める
        if (active_ || !input->TriggerAction(Input::Action::Skill) || cooldown_ > 0.0f) {
            return;
        }
        const PlayerTuning& t = Tuning();
        startPos_ = player.GetSkillHoldPosition();
        restPos_ = startPos_;
        restPos_.x = player.ClampToStageX(startPos_.x + player.GetLastDirX() * t.greatswordThrowDist);
        timer_ = 0.0f;
        spinHitTimer_ = 0.0f;
        active_ = true;
        returnCaptured_ = false;
        cooldown_ = t.greatswordThrowCooldown;
        player.SetFrameEvent(FrameEvent::GreatswordThrown);
        player.PlaySkillAnim(&CharacterRig::slashAnim, t.greatswordThrowAnimSpeed);
    }

    void Tick(Player& player, Input* input, float cooldownStep) override
    {
        Cooldown(cooldown_, cooldownStep);
        if (!active_) {
            return;
        }
        const PlayerTuning& t = Tuning();
        timer_ += GameConstants::kFrameDeltaTime;
        if (timer_ < t.greatswordThrowTravelTime) {
            return; // 飛んでいる最中（静止するまで）はまだ渦を巻かない
        }

        const float spinElapsed = timer_ - t.greatswordThrowTravelTime;
        if (spinElapsed < t.greatswordVortexMaxDuration) {
            // 渦の最中にもう一度スペースを押したら、上限まで待たずにその場で帰還を開始する（手動リコール）
            if (input->TriggerAction(Input::Action::Skill)) {
                timer_ = t.greatswordThrowTravelTime + t.greatswordVortexMaxDuration;
                return;
            }
            spinHitTimer_ += GameConstants::kFrameDeltaTime;
            if (spinHitTimer_ >= t.greatswordSpinHitInterval) {
                spinHitTimer_ -= t.greatswordSpinHitInterval;
                player.SetFrameEvent(FrameEvent::GreatswordSpinHit);
            }
            return;
        }

        // 渦が終わったら、瞬間移動で戻さず手元へ飛んで帰るフェーズへ（帰還先はこの瞬間の位置を1回だけ記録）
        if (!returnCaptured_) {
            returnTargetPos_ = player.GetSkillHoldPosition();
            returnCaptured_ = true;
        }
        if (spinElapsed - t.greatswordVortexMaxDuration >= t.greatswordReturnTime) {
            active_ = false; // 帰還完了、次フレームから手元のボーン追従に戻る
        }
    }

    void OnUnequipped() override
    {
        // 渦の途中で持ち替えても居残らないよう仕切り直す
        active_ = false;
        returnCaptured_ = false;
    }

    bool IsDisarmed() const override { return active_; }

    bool GetThrownPose(Vector3& outPosition, float& outSpinZ) const override
    {
        if (!active_) {
            return false;
        }
        const PlayerTuning& t = Tuning();
        if (timer_ < t.greatswordThrowTravelTime) {
            // 飛行中: 手元から静止地点へ補間しながら飛んでいく
            outPosition = Lerp(startPos_, restPos_, Easing::EaseOutQuad(timer_ / t.greatswordThrowTravelTime));
        } else {
            const float spinElapsed = timer_ - t.greatswordThrowTravelTime;
            if (spinElapsed < t.greatswordVortexMaxDuration) {
                // 渦の最中: その場に留まり、渦らしく小さく上下に揺れる
                outPosition = restPos_;
                outPosition.y += std::sin(spinElapsed * t.greatswordVortexBobSpeed) * t.greatswordVortexBobAmplitude;
            } else {
                // 帰還中: 渦の中心から手元へ飛んで戻る（瞬間移動に見えないよう補間する）
                const float returnElapsed = spinElapsed - t.greatswordVortexMaxDuration;
                const float progress = (std::min)(returnElapsed / t.greatswordReturnTime, 1.0f);
                outPosition = Lerp(restPos_, returnTargetPos_, Easing::EaseInQuad(progress));
            }
        }
        // 目にも留まらぬ速さで回転させ続ける（飛行中→静止後の渦で途切れず連続した見た目にする）
        outSpinZ = timer_ * t.greatswordThrowSpinSpeed;
        return true;
    }

    /** @brief 静止して渦を巻いている最中か（飛行中・帰還中はfalse） */
    bool IsSpinning() const
    {
        if (!active_) {
            return false;
        }
        const PlayerTuning& t = Tuning();
        const float spinElapsed = timer_ - t.greatswordThrowTravelTime;
        return spinElapsed >= 0.0f && spinElapsed < t.greatswordVortexMaxDuration;
    }
    const Vector3& GetRestPosition() const { return restPos_; }

private:
    static Vector3 Lerp(const Vector3& from, const Vector3& to, float t)
    {
        return { from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t, from.z + (to.z - from.z) * t };
    }

    bool active_ = false; ///< 投げてから手元へ戻るまでtrue（飛行中も含む）
    Vector3 startPos_ { }; ///< 投げた瞬間の手元位置（飛行中の補間の始点）
    Vector3 restPos_ { }; ///< 大剣が静止する位置（渦の中心）
    Vector3 returnTargetPos_ { }; ///< 帰還先（渦が終わった瞬間の手元位置を1回だけ記録する）
    bool returnCaptured_ = false;
    float timer_ = 0.0f; ///< 投げてからの経過秒（飛行→渦→帰還の順で進む）
    float spinHitTimer_ = 0.0f; ///< 渦の中でのヒット間隔用タイマー
    float cooldown_ = 0.0f;
};

class Player::ScytheBehavior final : public IWeaponBehavior {
public:
    void Update(Player& player, Input* input) override
    {
        // 滞空ホバー 空中限定で降下を抑える。時間制のリソースで無限滞空を防ぎ、着地で回復する
        const PlayerTuning& t = Tuning();
        if (player.IsOnGround()) {
            hoverTimer_ = (std::min)(hoverTimer_ + GameConstants::kFrameDeltaTime * t.scytheHoverRecoverRate, t.scytheHoverMax);
            return;
        }
        if (!input->PushAction(Input::Action::Skill) || hoverTimer_ <= 0.0f) {
            return;
        }
        if (input->TriggerAction(Input::Action::Skill)) {
            player.PlaySkillAnim(&CharacterRig::slashAnim, t.scytheHoverAnimSpeed);
            player.SetFrameEvent(FrameEvent::ScytheSpin);
        }
        player.LimitFallSpeed(t.scytheHoverVYCap);
        hoverTimer_ -= GameConstants::kFrameDeltaTime;
    }

private:
    float hoverTimer_ = Tuning().scytheHoverMax; ///< 残りの連続ホバー秒数
};

class Player::AxeBehavior final : public IWeaponBehavior {
public:
    void Update(Player& player, Input* input) override
    {
        // バーサーク突進 突進しつつ、命中の有無に関わらず一定時間ダメージが上がる（狂戦士らしいリスク覚悟の一撃）
        if (dash_.active) {
            if (player.AdvanceDash(dash_)) {
                player.SetFrameEvent(FrameEvent::AxeCharge);
            }
            return;
        }
        if (!input->TriggerAction(Input::Action::Skill) || cooldown_ > 0.0f) {
            return;
        }
        const PlayerTuning& t = Tuning();
        player.BeginDash(dash_, player.GetLastDirX() * t.axeChargeDist);
        cooldown_ = t.axeSkillCooldown;
        rageTimer_ = t.axeRageDuration;
        player.PlaySkillAnim(&CharacterRig::punchAnim, t.axeChargeAnimSpeed);
    }
    void Tick(Player&, Input*, float cooldownStep) override
    {
        Cooldown(cooldown_, cooldownStep);
        Cooldown(rageTimer_, GameConstants::kFrameDeltaTime);
    }
    void Interrupt() override { dash_.active = false; }

    bool IsEnraged() const { return rageTimer_ > 0.0f; }

private:
    DashMotion dash_;
    float cooldown_ = 0.0f;
    float rageTimer_ = 0.0f;
};

struct Player::WeaponBehaviorSet {
    /** @brief 固有技を持たない武器 */
    class NoSkillBehavior final : public IWeaponBehavior {
    public:
        void Update(Player&, Input*) override { }
    };

    SkillSequenceBehavior sword { kSwordSequence };
    SkillSequenceBehavior spear { kSpearSequence };
    SkillSequenceBehavior hammer { kHammerSequence };
    DaggerBehavior dagger;
    BallBehavior ball;
    GreatswordBehavior greatsword;
    ScytheBehavior scythe;
    AxeBehavior axe;
    NoSkillBehavior none;

    // WeaponType の並び順
    std::array<IWeaponBehavior*, kWeaponTypeCount> byType = {
        &sword, &spear, &hammer, &dagger, &ball, &greatsword, &scythe, &axe,
    };

    IWeaponBehavior& For(WeaponType type)
    {
        const size_t index = static_cast<size_t>(type);
        return index < byType.size() ? *byType[index] : none;
    }
};

} // namespace engine::game

Player::Player()
    : weaponBehaviors_(std::make_unique<WeaponBehaviorSet>())
{
}

Player::~Player() = default;

Player::IWeaponBehavior& Player::BehaviorFor(WeaponType type)
{
    return weaponBehaviors_->For(type);
}

const Player::IWeaponBehavior& Player::BehaviorFor(WeaponType type) const
{
    return weaponBehaviors_->For(type);
}

void Player::InterruptWeaponSkills()
{
    for (IWeaponBehavior* behavior : weaponBehaviors_->byType) {
        behavior->Interrupt();
    }
}

void Player::ResetWeaponSkills()
{
    for (IWeaponBehavior* behavior : weaponBehaviors_->byType) {
        behavior->OnUnequipped();
    }
}

void Player::TickWeaponSkills(Input* input, float cooldownStep)
{
    for (IWeaponBehavior* behavior : weaponBehaviors_->byType) {
        behavior->Tick(*this, input, cooldownStep);
    }
}

void Player::NotifyWeaponSkillsLanded()
{
    for (IWeaponBehavior* behavior : weaponBehaviors_->byType) {
        behavior->OnLanded();
    }
}

bool Player::TryStartSkillSequence(Input* input, WeaponType type, float& cooldown, float cooldownSeconds)
{
    // 段の進行・ヒット・振りの見た目は通常コンボと同じ仕組み（MeleeComboController）に乗せる
    // オートコンボのルートが固有技で締める回なら、ボタンを押さなくても出す
    const bool requested = input->TriggerAction(Input::Action::Skill) || autoCombo_.ConsumeSkillRequest();
    if (!requested || cooldown > 0.0f || meleeCombo_.IsSequenceActive()) {
        return false;
    }
    const ComboArray<MeleeAttackDef>& sequence = GetSkillSequence(type);
    if (sequence.count <= 0) {
        return false;
    }
    // 1段目のモーションは次フレームのHandleMeleeCombat()で段の開始として再生される
    meleeCombo_.StartSequence(sequence);
    cooldown = cooldownSeconds;
    return true;
}

void Player::PlaySkillAnim(Animation CharacterRig::* clip, float speed)
{
    PlayAttackAnim(rig_->*clip, speed);
}

void Player::AddSpin(float degrees)
{
    spinAngle_ += degrees;
    if (spinAngle_ >= kFullTurnDegrees_) {
        spinAngle_ -= kFullTurnDegrees_;
    }
}

Vector3 Player::GetSkillHoldPosition() const
{
    return { pos_.x, pos_.y + Tuning().skillHoldHeight, pos_.z };
}

bool Player::IsGreatswordSpinning() const
{
    return weaponBehaviors_->greatsword.IsSpinning();
}

Vector3 Player::GetGreatswordThrowPos() const
{
    return weaponBehaviors_->greatsword.GetRestPosition();
}

bool Player::IsAxeEnraged() const
{
    return weaponBehaviors_->axe.IsEnraged();
}

float Player::GetAxeRageMult() const
{
    return IsAxeEnraged() ? Tuning().axeRageDamageMult : 1.0f;
}
