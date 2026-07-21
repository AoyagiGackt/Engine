/**
 * @file PlayerWeaponBehaviors.cpp
 * @brief PlayerWeaponBehaviorsが担当する処理を実装するファイル
 */
#include "Player.h"
#include "GameConstants.h"
#include "Input.h"
#include "Weapon.h"
#include <algorithm>

using namespace engine;
using namespace engine::game;

//  Weapon Behavior Strategy（武器種別ごとのスペースキー挙動）

void Player::DaggerBehavior::Update(Player& player, Input* input) const
{
    // ブリンク（瞬間移動）
    if (input->TriggerAction(Input::Action::Skill)) {
        player.pos_.x += player.lastDirX_ * kBlinkDist_ * player.skillMods_.blinkDistMult;
        player.pos_.x = std::clamp(player.pos_.x, player.minX_, player.maxX_);
        player.justBlinked_ = true;
        player.PlayAttackAnim(player.rig_->slashAnim, 1.8f);
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
    if (input->TriggerAction(Input::Action::Skill) && player.swordSkillCooldown_ <= 0.0f) {
        player.pos_.x += player.lastDirX_ * kSwordDashDist_;
        player.pos_.x = std::clamp(player.pos_.x, player.minX_, player.maxX_);
        player.justSwordDash_ = true;
        player.swordSkillCooldown_ = kSwordSkillCooldown_;
        player.PlayAttackAnim(player.rig_->slashAnim, 1.6f);
    }
}

void Player::SpearBehavior::Update(Player& player, Input* input) const
{
    // 間合い外し 後退しながら突く、牽制役らしいヒットアンドアウェイ
    if (input->TriggerAction(Input::Action::Skill) && player.spearSkillCooldown_ <= 0.0f) {
        player.pos_.x -= player.lastDirX_ * kSpearRetreatDist_;
        player.pos_.x = std::clamp(player.pos_.x, player.minX_, player.maxX_);
        player.justSpearRetreat_ = true;
        player.spearSkillCooldown_ = kSpearSkillCooldown_;
        player.PlayAttackAnim(player.rig_->punchAnim, 1.4f);
    }
}

void Player::GreatswordBehavior::Update(Player& player, Input* input) const
{
    // 大地砕き その場に叩きつける設置型AoE、重量級らしく地上限定・長いクールタイム
    if (input->TriggerAction(Input::Action::Skill) && player.greatswordSkillCooldown_ <= 0.0f && player.onGround_) {
        player.justGreatswordSlam_ = true;
        player.greatswordSkillCooldown_ = kGreatswordSkillCooldown_;
        player.PlayAttackAnim(player.rig_->slashAnim, 0.65f);
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
    if (input->TriggerAction(Input::Action::Skill) && player.axeSkillCooldown_ <= 0.0f) {
        player.pos_.x += player.lastDirX_ * kAxeChargeDist_;
        player.pos_.x = std::clamp(player.pos_.x, player.minX_, player.maxX_);
        player.justAxeCharge_ = true;
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
