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
    if (input->TriggerKey(DIK_SPACE)) {
        player.pos_.x += player.lastDirX_ * kBlinkDist_ * player.skillMods_.blinkDistMult;
        player.pos_.x = std::clamp(player.pos_.x, kMinX_, kMaxX_);
        player.justBlinked_ = true;
    }
}

void Player::HammerBehavior::Update(Player& player, Input* input) const
{
    // ゲージチャージ（長押し約3秒で満タン）覚醒中は蓄積しない
    if (!player.isAwakened_ && input->PushKey(DIK_SPACE)) {
        player.justChargedGauge_ = true;
        player.awakenGauge_ = (std::min)(player.awakenGauge_ + kGaugeCharge_ * GameConstants::kFrameDeltaTime * player.skillMods_.gaugeChargeMult, 1.0f);
    }
}

void Player::BallBehavior::Update(Player& player, Input* input) const
{
    // スピン連射 + 空中くるくる
    if (input->PushKey(DIK_SPACE)) {
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
    if (input->TriggerKey(DIK_SPACE) && player.swordSkillCooldown_ <= 0.0f) {
        player.pos_.x += player.lastDirX_ * kSwordDashDist_;
        player.pos_.x = std::clamp(player.pos_.x, kMinX_, kMaxX_);
        player.justSwordDash_ = true;
        player.swordSkillCooldown_ = kSwordSkillCooldown_;
    }
}

void Player::SpearBehavior::Update(Player& player, Input* input) const
{
    // 間合い外し 後退しながら突く、牽制役らしいヒットアンドアウェイ
    if (input->TriggerKey(DIK_SPACE) && player.spearSkillCooldown_ <= 0.0f) {
        player.pos_.x -= player.lastDirX_ * kSpearRetreatDist_;
        player.pos_.x = std::clamp(player.pos_.x, kMinX_, kMaxX_);
        player.justSpearRetreat_ = true;
        player.spearSkillCooldown_ = kSpearSkillCooldown_;
    }
}

void Player::GreatswordBehavior::Update(Player& player, Input* input) const
{
    // 大地砕き その場に叩きつける設置型AoE、重量級らしく地上限定・長いクールタイム
    if (input->TriggerKey(DIK_SPACE) && player.greatswordSkillCooldown_ <= 0.0f && player.onGround_) {
        player.justGreatswordSlam_ = true;
        player.greatswordSkillCooldown_ = kGreatswordSkillCooldown_;
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
    if (input->PushKey(DIK_SPACE) && player.scytheHoverTimer_ > 0.0f) {
        player.isScytheHovering_ = true;
        player.velocityY_ = (std::max)(player.velocityY_, kScytheHoverVYCap_);
        player.scytheHoverTimer_ -= GameConstants::kFrameDeltaTime;
    }
}

void Player::AxeBehavior::Update(Player& player, Input* input) const
{
    // バーサーク突進 突進しつつ、命中の有無に関わらず一定時間ダメージが上がる（狂戦士らしいリスク覚悟の一撃）
    if (input->TriggerKey(DIK_SPACE) && player.axeSkillCooldown_ <= 0.0f) {
        player.pos_.x += player.lastDirX_ * kAxeChargeDist_;
        player.pos_.x = std::clamp(player.pos_.x, kMinX_, kMaxX_);
        player.justAxeCharge_ = true;
        player.axeSkillCooldown_ = kAxeSkillCooldown_;
        player.axeRageTimer_ = kAxeRageDuration_;
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
