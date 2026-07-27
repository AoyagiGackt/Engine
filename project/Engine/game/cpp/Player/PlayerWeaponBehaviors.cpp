/**
 * @file PlayerWeaponBehaviors.cpp
 * @brief PlayerWeaponBehaviorsのプレイヤーの操作、戦闘、状態遷移に関する具体的な処理を実装するファイル
 */
#include "GameConstants.h"
#include "Input.h"
#include "Player.h"
#include "Weapon.h"
#include <algorithm>

using namespace engine;
using namespace engine::game;

//  Weapon Behavior Strategy（武器種別ごとのスペースキー挙動）

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

    if (player.daggerStingerHitIndex_ < 0) {
        if (input->TriggerAction(Input::Action::Skill) && player.daggerStingerCooldown_ <= 0.0f) {
            player.BeginDash(player.daggerStingerDash_,
                player.lastDirX_ * kDaggerStingerDashDist_ * player.skillMods_.blinkDistMult);
            player.daggerStingerCooldown_ = kDaggerStingerCooldown_;
            player.PlayAttackAnim(player.rig_->slashAnim, 1.8f);
        }
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
    // 地上でハンマーを叩きつけ、障害物と周囲の敵をまとめて破壊する
    if (input->TriggerAction(Input::Action::Skill) && player.greatswordSkillCooldown_ <= 0.0f && player.onGround_) {
        player.justGreatswordSlam_ = true;
        player.greatswordSkillCooldown_ = kGreatswordSkillCooldown_;
        player.PlayAttackAnim(player.rig_->slashAnim, 0.7f);
    }
}

void Player::BallBehavior::Update(Player& player, Input* input) const
{
    // スピン連射 + 空中くるくる
    if (input->PushAction(Input::Action::Skill)) {
        if (input->TriggerAction(Input::Action::Skill)) {
            player.PlayAttackAnim(player.rig_->punchAnim, 2.2f);
        }
        if (player.shootCooldown_ <= 0.0f) {
            player.justSpinShot_ = true;
            player.shootCooldown_ = kShootInterval_ * player.skillMods_.fireIntervalMult;
        }
        if (!player.onGround_) {
            player.spinAngle_ += kSpinSpeed_;
            if (player.spinAngle_ >= 360.0f) {
                player.spinAngle_ -= 360.0f;
            }
        }
    }
}

void Player::SwordBehavior::Update(Player& player, Input* input) const
{
    // 瞬迅斬り 短距離を踏み込みながら斬る、全能武器らしく癖のない攻守一体の一撃
    // 踏み込みは瞬間移動にせず、短時間で滑らかに移動しきったフレームでヒットさせる
    if (player.swordDash_.active) {
        if (player.AdvanceDash(player.swordDash_)) {
            player.justSwordDash_ = true;
        }
        return;
    }
    if (input->TriggerAction(Input::Action::Skill) && player.swordSkillCooldown_ <= 0.0f) {
        player.BeginDash(player.swordDash_, player.lastDirX_ * kSwordDashDist_);
        player.swordSkillCooldown_ = kSwordSkillCooldown_;
        player.PlayAttackAnim(player.rig_->slashAnim, 1.6f);
    }
}

void Player::SpearBehavior::Update(Player& player, Input* input) const
{
    // 間合い外し 後退しながら突く、牽制役らしいヒットアンドアウェイ
    if (player.spearDash_.active) {
        if (player.AdvanceDash(player.spearDash_)) {
            player.justSpearRetreat_ = true;
        }
        return;
    }
    if (input->TriggerAction(Input::Action::Skill) && player.spearSkillCooldown_ <= 0.0f) {
        player.BeginDash(player.spearDash_, -player.lastDirX_ * kSpearRetreatDist_);
        player.spearSkillCooldown_ = kSpearSkillCooldown_;
        player.PlayAttackAnim(player.rig_->punchAnim, 1.4f);
    }
}

void Player::GreatswordBehavior::Update(Player& player, Input* input) const
{
    // 投げ回転斬り 大剣そのものを投げ、途中で静止して渦のように回転し、周囲の敵を巻き込みながら多段ヒットする
    if (!player.greatswordThrowActive_) {
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
            player.PlayAttackAnim(player.rig_->slashAnim, 1.3f);
        }
        return;
    }

    player.greatswordThrowTimer_ += GameConstants::kFrameDeltaTime;
    if (player.greatswordThrowTimer_ < kGreatswordThrowTravelTime_) {
        return; // 飛んでいる最中（静止するまで）はまだ渦を巻かない
    }

    const float spinElapsed = player.greatswordThrowTimer_ - kGreatswordThrowTravelTime_;
    if (spinElapsed < kGreatswordVortexMaxDuration_) {
        // 渦の最中にもう一度スペースを押したら、上限まで待たずにその場で帰還を開始する（手動リコール）
        if (input->TriggerAction(Input::Action::Skill)) {
            player.greatswordThrowTimer_ = kGreatswordThrowTravelTime_ + kGreatswordVortexMaxDuration_;
            return;
        }
        player.greatswordSpinHitTimer_ += GameConstants::kFrameDeltaTime;
        if (player.greatswordSpinHitTimer_ >= kGreatswordSpinHitInterval_) {
            player.greatswordSpinHitTimer_ -= kGreatswordSpinHitInterval_;
            player.justGreatswordSpinHit_ = true;
        }
        return;
    }

    // 渦が終わったら、瞬間移動で戻さず手元へ飛んで帰るフェーズへ（帰還先はこの瞬間の位置を1回だけ記録）
    if (!player.greatswordReturnCaptured_) {
        player.greatswordReturnTargetPos_ = { player.pos_.x, player.pos_.y + 0.5f, player.pos_.z };
        player.greatswordReturnCaptured_ = true;
    }
    const float returnElapsed = spinElapsed - kGreatswordVortexMaxDuration_;
    if (returnElapsed >= kGreatswordReturnTime_) {
        player.greatswordThrowActive_ = false; // 帰還完了、次フレームから手元のボーン追従に戻る
    }
}

void Player::ScytheBehavior::Update(Player& player, Input* input) const
{
    // 滞空ホバー 空中限定で降下を抑える。時間制のリソースで無限滞空を防ぎ、着地で回復する
    player.isScytheHovering_ = false;
    if (player.onGround_) {
        player.scytheHoverTimer_ = (std::min)(player.scytheHoverTimer_ + GameConstants::kFrameDeltaTime * kScytheHoverRecoverRate_, kScytheHoverMax_);
        return;
    }
    if (input->PushAction(Input::Action::Skill) && player.scytheHoverTimer_ > 0.0f) {
        if (input->TriggerAction(Input::Action::Skill)) {
            player.PlayAttackAnim(player.rig_->slashAnim, 1.25f);
            player.justScytheSpin_ = true;
        }
        player.isScytheHovering_ = true;
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
        player.PlayAttackAnim(player.rig_->punchAnim, 1.1f);
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
