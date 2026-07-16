#include "Player.h"
#include "GameConstants.h"
#include "GravityBody.h"
#include "Input.h"
#include <algorithm>

using namespace engine;
using namespace engine::game;

//  Physics State（水中/水上）

void Player::UnderwaterPhysicsState::Update(Player& player, Input* input) const
{
    const float speedMult = (player.isAwakened_ ? 1.5f : 1.0f) * player.skillMods_.speedMult;

    // 横移動（水の抵抗で遅い）
    if (input->PushKey(DIK_A) || input->PushKey(DIK_LEFT)) {
        player.pos_.x -= kWaterSpeed_ * speedMult;
        player.lastDirX_ = -1.0f;
    }
    if (input->PushKey(DIK_D) || input->PushKey(DIK_RIGHT)) {
        player.pos_.x += kWaterSpeed_ * speedMult;
        player.lastDirX_ = 1.0f;
    }

    // 浮力（弱い下向き加速）
    player.velocityY_ -= kWaterGravity_;
    player.velocityY_ = (std::max)(player.velocityY_, kSinkMaxVY_);

    // ジャンプ長押し = 上昇スイム
    if (input->PushKey(DIK_W) || input->PushKey(DIK_UP) || input->PushKey(DIK_SPACE)) {
        player.velocityY_ = (std::min)(player.velocityY_ + kSwimAccel_, kSwimMaxVY_);
    }

    player.pos_.y += player.velocityY_;

    // 床クランプ（水底でも止まる）
    if (player.pos_.y <= kGroundY_) {
        player.pos_.y = kGroundY_;
        player.velocityY_ = 0.0f;
        player.onGround_ = true;
    } else {
        player.onGround_ = false;
    }

    // 天井クランプ
    if (player.pos_.y > kCeilingY_) {
        player.pos_.y = kCeilingY_;
        player.velocityY_ = 0.0f;
    }
}

void Player::GroundedPhysicsState::Update(Player& player, Input* input) const
{
    const float speedMult = (player.isAwakened_ ? 1.5f : 1.0f) * player.skillMods_.speedMult;
    const float jumpMult = (player.isAwakened_ ? 1.3f : 1.0f) * player.skillMods_.jumpMult;

    if (player.rampagePhase_ == RampagePhase::Inactive && !player.finisherCharging_) {
        if (input->PushKey(DIK_A) || input->PushKey(DIK_LEFT)) {
            player.pos_.x -= kSpeed_ * speedMult;
            player.lastDirX_ = -1.0f;
        }
        if (input->PushKey(DIK_D) || input->PushKey(DIK_RIGHT)) {
            player.pos_.x += kSpeed_ * speedMult;
            player.lastDirX_ = 1.0f;
        }
    }

    if (player.onGround_ && !player.finisherCharging_) {
        if (input->TriggerKey(DIK_W) || input->TriggerKey(DIK_UP)) {
            // 打ち上げ直後は追撃用に高く跳べる（浮かせた敵にジャンプで追いつく）
            float followMult = (player.launchFollowTimer_ > 0.0f) ? kLaunchFollowJumpMult_ : 1.0f;
            player.velocityY_ = kJumpPower_ * jumpMult * followMult;
            player.onGround_ = false;
            player.justJumped_ = true;
        }
    }

    if (ApplyGravityAndClampY(player.pos_.y, player.velocityY_, kGravity_, kGroundY_, kCeilingY_)) {
        player.onGround_ = true;
    }

    player.justLanded_ = !player.prevOnGround_ && player.onGround_;
}

const Player::IPhysicsState& Player::GetPhysicsState(bool inWater)
{
    static GroundedPhysicsState grounded;
    static UnderwaterPhysicsState underwater;
    return inWater ? static_cast<const IPhysicsState&>(underwater) : static_cast<const IPhysicsState&>(grounded);
}
