/**
 * @file PlayerWeaponBehaviors.cpp
 * @brief PlayerWeaponBehaviorsのプレイヤーの操作、戦闘、状態遷移に関する具体的な処理を実装するファイル
 */
#include "GameConstants.h"
#include "Input.h"
#include "Player.h"
#include "Weapon.h"
#include <algorithm>
#include <cmath>

using namespace engine;
using namespace engine::game;

namespace {
// 固有技ごとのモーション再生速度
constexpr float kDaggerAirDashAnimSpeed = 2.0f;
constexpr float kBallSpinShotAnimSpeed = 2.2f;
constexpr float kGreatswordThrowAnimSpeed = 1.3f;
constexpr float kScytheHoverAnimSpeed = 1.25f;
constexpr float kAxeChargeAnimSpeed = 1.1f;

}

//  Weapon Behavior Strategy（武器種別ごとのスペースキー挙動）

namespace engine::game {
class Player::DaggerBehavior : public IWeaponBehavior {
    public:
        void Update(Player& player, Input* input) const override;
    };
class Player::HammerBehavior : public IWeaponBehavior {
    public:
        void Update(Player& player, Input* input) const override;
    };
class Player::BallBehavior : public IWeaponBehavior {
    public:
        void Update(Player& player, Input* input) const override;
    };
class Player::SwordBehavior : public IWeaponBehavior {
    public:
        void Update(Player& player, Input* input) const override;
    };
class Player::SpearBehavior : public IWeaponBehavior {
    public:
        void Update(Player& player, Input* input) const override;
    };
class Player::GreatswordBehavior : public IWeaponBehavior {
    public:
        void Update(Player& player, Input* input) const override;
    };
class Player::ScytheBehavior : public IWeaponBehavior {
    public:
        void Update(Player& player, Input* input) const override;
    };
class Player::AxeBehavior : public IWeaponBehavior {
    public:
        void Update(Player& player, Input* input) const override;
    };
class Player::DefaultWeaponBehavior : public IWeaponBehavior {
    public:
        void Update(Player&, Input*) const override { }
    };
}

void Player::DaggerBehavior::Update(Player& player, Input* input) const
{
    // スティンガー: 踏み込みながら3連続で刺す多段突き。手数武器らしく「当てたら連続で刺さる」判断を体感させる
    // 踏み込み自体は瞬間移動にせず、短時間で滑らかに移動しきったフレームで1段目を発生させる
    if (player.daggerStingerDash_.active) {
        if (player.AdvanceDash(player.daggerStingerDash_)) {
            player.daggerStingerHitIndex_ = 0;
            player.daggerStingerTimer_ = 0.0f;
            player.justDaggerStingerHit_ = true;
        }
        return;
    }

    // 空中ダッシュ（機動力枠）。滑っている間は落下を止め、足場から足場へ飛び移れるようにする
    if (player.airDash_.active) {
        player.velocityY_ = 0.0f;
        player.AdvanceDash(player.airDash_);
        return;
    }
    if (!player.onGround_ && player.daggerStingerHitIndex_ < 0) {
        if (input->TriggerAction(Input::Action::Skill) && player.airDashAvailable_) {
            player.airDashAvailable_ = false;
            player.velocityY_ = 0.0f;
            player.BeginDash(player.airDash_, player.lastDirX_ * kDaggerAirDashDist_ * player.skillMods_.blinkDistMult);
            player.PlayAttackAnim(player.rig_->runningJumpAnim, kDaggerAirDashAnimSpeed);
        }
        return;
    }

    if (player.daggerStingerHitIndex_ < 0) {
        // 地上では乱れ斬り: 回転を挟んだ高速の斬撃を重ね、回りながら斬り上げて敵を打ち飛ばす
        player.TryStartSkillSequence(input, WeaponType::Dagger, player.daggerStingerCooldown_, kDaggerStingerCooldown_);
        return;
    }

    // 2段目以降は入力不要、踏み込みの勢いのまま一定間隔で自動的に刺し込む
    player.daggerStingerTimer_ += GameConstants::kFrameDeltaTime;
    if (player.daggerStingerTimer_ >= kDaggerStingerHitInterval_) {
        player.daggerStingerTimer_ -= kDaggerStingerHitInterval_;
        player.daggerStingerHitIndex_++;
        if (player.daggerStingerHitIndex_ >= kDaggerStingerHitCount_) {
            player.daggerStingerHitIndex_ = -1;
            return;
        }
        player.justDaggerStingerHit_ = true;
    }
}

void Player::HammerBehavior::Update(Player& player, Input* input) const
{
    // 大車輪: 体ごと回る振り回しを重ね、最後に地面へ叩きつけて衝撃波を出す（叩きつけはHandleMeleeCombatで最終段のヒットに合わせて発生）
    if (player.onGround_) {
        player.TryStartSkillSequence(input, WeaponType::Hammer, player.greatswordSkillCooldown_, kGreatswordSkillCooldown_);
    }
}

void Player::BallBehavior::Update(Player& player, Input* input) const
{
    // スピン連射 + 空中くるくる
    if (input->PushAction(Input::Action::Skill)) {
        if (input->TriggerAction(Input::Action::Skill)) {
            player.PlayAttackAnim(player.rig_->punchAnim, kBallSpinShotAnimSpeed);
        }
        if (player.shootCooldown_ <= 0.0f) {
            player.justSpinShot_ = true;
            player.shootCooldown_ = kShootInterval_ * player.skillMods_.fireIntervalMult;
        }
        if (!player.onGround_) {
            player.spinAngle_ += kSpinSpeed_;
            if (player.spinAngle_ >= kFullTurnDegrees_) {
                player.spinAngle_ -= kFullTurnDegrees_;
            }
        }
    }
}

void Player::SwordBehavior::Update(Player& player, Input* input) const
{
    // 剣舞: 回転を織り交ぜた連撃を自動で出し切り、最後の一太刀で敵を打ち飛ばす
    player.TryStartSkillSequence(input, WeaponType::Sword, player.swordSkillCooldown_, kSwordSkillCooldown_);
}

bool Player::TryStartSkillSequence(Input* input, WeaponType type, float& cooldown, float cooldownSeconds)
{
    // 段の進行・ヒット・振りの見た目は通常コンボと同じ仕組み（MeleeComboController）に乗せる
    if (!input->TriggerAction(Input::Action::Skill) || cooldown > 0.0f || meleeCombo_.IsSequenceActive()) {
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

void Player::SpearBehavior::Update(Player& player, Input* input) const
{
    // 百裂突き: 連続突きから薙ぎ払いで回り、渾身の一突きで敵を打ち飛ばす
    player.TryStartSkillSequence(input, WeaponType::Spear, player.spearSkillCooldown_, kSpearSkillCooldown_);
}

void Player::GreatswordBehavior::Update(Player& player, Input* input) const
{
    // 投げ回転斬り 大剣そのものを投げ、途中で静止して渦のように回転し、周囲の敵を巻き込みながら多段ヒットする
    // 発生（投げる瞬間）だけここで扱う。進行中の状態（飛行→渦→帰還）はUpdateGreatswordThrowState()が
    // 装備武器に関係なく毎フレーム進めるので、持ち替えても凍結しない
    if (player.greatswordThrowActive_) {
        return;
    }
    if (input->TriggerAction(Input::Action::Skill) && player.greatswordThrowCooldown_ <= 0.0f) {
        // 浮遊高さは胸の高さ付近（他の命中エフェクトと同じ pos_.y + 0.5 に合わせる）
        player.greatswordThrowStartPos_ = { player.pos_.x, player.pos_.y + 0.5f, player.pos_.z };
        player.greatswordThrowPos_ = player.greatswordThrowStartPos_;
        player.greatswordThrowPos_.x = std::clamp(
            player.pos_.x + player.lastDirX_ * kGreatswordThrowDist_, player.minX_, player.maxX_);
        player.greatswordThrowTimer_ = 0.0f;
        player.greatswordSpinHitTimer_ = 0.0f;
        player.greatswordThrowActive_ = true;
        player.greatswordReturnCaptured_ = false;
        player.greatswordThrowCooldown_ = kGreatswordThrowCooldown_;
        player.justGreatswordThrown_ = true;
        player.PlayAttackAnim(player.rig_->slashAnim, kGreatswordThrowAnimSpeed);
    }
}

void Player::UpdateGreatswordThrowState(Input* input)
{
    if (!greatswordThrowActive_) {
        return;
    }

    greatswordThrowTimer_ += GameConstants::kFrameDeltaTime;
    if (greatswordThrowTimer_ < kGreatswordThrowTravelTime_) {
        return; // 飛んでいる最中（静止するまで）はまだ渦を巻かない
    }

    const float spinElapsed = greatswordThrowTimer_ - kGreatswordThrowTravelTime_;
    if (spinElapsed < kGreatswordVortexMaxDuration_) {
        // 渦の最中にもう一度スペースを押したら、上限まで待たずにその場で帰還を開始する（手動リコール）
        if (input->TriggerAction(Input::Action::Skill)) {
            greatswordThrowTimer_ = kGreatswordThrowTravelTime_ + kGreatswordVortexMaxDuration_;
            return;
        }
        greatswordSpinHitTimer_ += GameConstants::kFrameDeltaTime;
        if (greatswordSpinHitTimer_ >= kGreatswordSpinHitInterval_) {
            greatswordSpinHitTimer_ -= kGreatswordSpinHitInterval_;
            justGreatswordSpinHit_ = true;
        }
        return;
    }

    // 渦が終わったら、瞬間移動で戻さず手元へ飛んで帰るフェーズへ（帰還先はこの瞬間の位置を1回だけ記録）
    if (!greatswordReturnCaptured_) {
        greatswordReturnTargetPos_ = { pos_.x, pos_.y + 0.5f, pos_.z };
        greatswordReturnCaptured_ = true;
    }
    const float returnElapsed = spinElapsed - kGreatswordVortexMaxDuration_;
    if (returnElapsed >= kGreatswordReturnTime_) {
        greatswordThrowActive_ = false; // 帰還完了、次フレームから手元のボーン追従に戻る
    }
}

void Player::ScytheBehavior::Update(Player& player, Input* input) const
{
    // 滞空ホバー 空中限定で降下を抑える。時間制のリソースで無限滞空を防ぎ、着地で回復する
    if (player.onGround_) {
        player.scytheHoverTimer_ = (std::min)(player.scytheHoverTimer_ + GameConstants::kFrameDeltaTime * kScytheHoverRecoverRate_, kScytheHoverMax_);
        return;
    }
    if (input->PushAction(Input::Action::Skill) && player.scytheHoverTimer_ > 0.0f) {
        if (input->TriggerAction(Input::Action::Skill)) {
            player.PlayAttackAnim(player.rig_->slashAnim, kScytheHoverAnimSpeed);
            player.justScytheSpin_ = true;
        }
        player.velocityY_ = (std::max)(player.velocityY_, kScytheHoverVYCap_);
        player.scytheHoverTimer_ -= GameConstants::kFrameDeltaTime;
    }
}

void Player::AxeBehavior::Update(Player& player, Input* input) const
{
    // バーサーク突進 突進しつつ、命中の有無に関わらず一定時間ダメージが上がる（狂戦士らしいリスク覚悟の一撃）
    if (player.axeDash_.active) {
        if (player.AdvanceDash(player.axeDash_)) {
            player.justAxeCharge_ = true;
        }
        return;
    }
    if (input->TriggerAction(Input::Action::Skill) && player.axeSkillCooldown_ <= 0.0f) {
        player.BeginDash(player.axeDash_, player.lastDirX_ * kAxeChargeDist_);
        player.axeSkillCooldown_ = kAxeSkillCooldown_;
        player.axeRageTimer_ = kAxeRageDuration_;
        player.PlayAttackAnim(player.rig_->punchAnim, kAxeChargeAnimSpeed);
    }
}

const Player::IWeaponBehavior& Player::GetWeaponBehavior(WeaponType type)
{
    static DaggerBehavior dagger;
    static HammerBehavior hammer;
    static BallBehavior ball;
    static SwordBehavior sword;
    static SpearBehavior spear;
    static GreatswordBehavior greatsword;
    static ScytheBehavior scythe;
    static AxeBehavior axe;
    static DefaultWeaponBehavior def;
    switch (type) {
    case WeaponType::Dagger:
        return dagger;
    case WeaponType::Hammer:
        return hammer;
    case WeaponType::Ball:
        return ball;
    case WeaponType::Sword:
        return sword;
    case WeaponType::Spear:
        return spear;
    case WeaponType::Greatsword:
        return greatsword;
    case WeaponType::Scythe:
        return scythe;
    case WeaponType::Axe:
        return axe;
    default:
        return def;
    }
}
