/**
 * @file PlayerPhysicsStates.cpp
 * @brief PlayerPhysicsStatesのプレイヤーの操作、戦闘、状態遷移に関する具体的な処理を実装するファイル
 */
#include "GameConstants.h"
#include "GravityBody.h"
#include "Input.h"
#include "Player.h"
#include <algorithm>
#include <limits>

using namespace engine;
using namespace engine::game;

//  Physics State（水中/水上）

namespace engine::game {
class Player::GroundedPhysicsState : public IPhysicsState {
    public:
        void Update(Player& player, Input* input) const override;
    };
class Player::UnderwaterPhysicsState : public IPhysicsState {
    public:
        void Update(Player& player, Input* input) const override;
    };
}

void Player::UnderwaterPhysicsState::Update(Player& player, Input* input) const
{
    const float speedMult = (player.isAwakened_ ? kAwakenedSpeedMult_ : 1.0f) * player.skillMods_.speedMult;

    // 横移動（水の抵抗で遅い）
    if (input->PushAction(Input::Action::MoveLeft)) {
        player.pos_.x -= kWaterSpeed_ * speedMult;
        player.lastDirX_ = -1.0f;
    }
    if (input->PushAction(Input::Action::MoveRight)) {
        player.pos_.x += kWaterSpeed_ * speedMult;
        player.lastDirX_ = 1.0f;
    }

    // 浮力（弱い下向き加速）
    player.velocityY_ -= kWaterGravity_;
    player.velocityY_ = (std::max)(player.velocityY_, kSinkMaxVY_);

    // ジャンプ長押し = 上昇スイム
    if (input->PushAction(Input::Action::Jump)) {
        player.velocityY_ = (std::min)(player.velocityY_ + kSwimAccel_, kSwimMaxVY_);
    }

    player.pos_.y += player.velocityY_;

    // 床クランプ（水底でも止まる）
    if (!player.useStageFloor_ && player.pos_.y <= kGroundY_) {
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
    const float speedMult = (player.isAwakened_ ? kAwakenedSpeedMult_ : 1.0f) * player.skillMods_.speedMult;
    const float jumpMult = (player.isAwakened_ ? kAwakenedJumpMult_ : 1.0f) * player.skillMods_.jumpMult;

    // 回避中は回避の移動が位置を決めるので、通常の左右移動とジャンプは受け付けない
    if (!player.IsRampaging() && !player.finisherCharging_ && !player.dodgeActive_) {
        if (input->PushAction(Input::Action::MoveLeft)) {
            player.pos_.x -= kSpeed_ * speedMult;
            player.lastDirX_ = -1.0f;
        }
        if (input->PushAction(Input::Action::MoveRight)) {
            player.pos_.x += kSpeed_ * speedMult;
            player.lastDirX_ = 1.0f;
        }
    }

    if (player.onGround_ && !player.finisherCharging_ && !player.dodgeActive_) {
        if (input->TriggerAction(Input::Action::Jump)) {
            // 打ち上げ直後は追撃用に高く跳べる（浮かせた敵にジャンプで追いつく）
            float followMult = (player.launchFollowTimer_ > 0.0f) ? kLaunchFollowJumpMult_ : 1.0f;
            player.velocityY_ = kJumpPower_ * jumpMult * followMult;
            player.onGround_ = false;
            player.justJumped_ = true;
        }
    }

    const float groundY = player.useStageFloor_ ? (std::numeric_limits<float>::lowest)() : kGroundY_;
    if (ApplyGravityAndClampY(player.pos_.y, player.velocityY_, kGravity_, groundY, kCeilingY_)) {
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
