/**
 * @file PlayerPhysicsStates.cpp
 * @brief PlayerPhysicsStatesのプレイヤーの操作、戦闘、状態遷移に関する具体的な処理を実装するファイル
 */
#include "GameConstants.h"
#include "GravityBody.h"
#include "Input.h"
#include "Player.h"
#include "PlayerTuning.h"
#include <algorithm>
#include <limits>

using namespace engine;
using namespace engine::game;

namespace {
const PlayerTuning& Tuning()
{
    return PlayerTuning::Get();
}

/** @brief 横移動の入力1つぶん（押している間その向きへ進む） */
struct MoveInput {
    Input::Action action;
    float dirX;
};
// 両方押した時は後ろの行が向きを決める
constexpr MoveInput kMoveInputs[] = {
    { Input::Action::MoveLeft, -1.0f },
    { Input::Action::MoveRight, 1.0f },
};
} // namespace

void Player::ApplyHorizontalInput(Input* input, float step)
{
    for (const MoveInput& move : kMoveInputs) {
        if (input->PushAction(move.action)) {
            pos_.x += move.dirX * step;
            lastDirX_ = move.dirX;
        }
    }
}

float Player::GetHeldMoveDirection(Input* input) const
{
    for (const MoveInput& move : kMoveInputs) {
        if (input->PushAction(move.action)) {
            return move.dirX;
        }
    }
    return 0.0f;
}

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
    const float speedMult = (player.isAwakened_ ? Tuning().awakenedSpeedMult : 1.0f) * player.skillMods_.speedMult;

    // 横移動（水の抵抗で遅い）
    player.ApplyHorizontalInput(input, Tuning().waterSpeed * speedMult);

    // 浮力（弱い下向き加速）
    player.velocityY_ -= Tuning().waterGravity;
    player.velocityY_ = (std::max)(player.velocityY_, Tuning().sinkMaxVY);

    // ジャンプ長押し = 上昇スイム
    if (input->PushAction(Input::Action::Jump)) {
        player.velocityY_ = (std::min)(player.velocityY_ + Tuning().swimAccel, Tuning().swimMaxVY);
    }

    player.pos_.y += player.velocityY_;

    // 床クランプ（水底でも止まる）
    if (!player.useStageFloor_ && player.pos_.y <= Tuning().groundY) {
        player.pos_.y = Tuning().groundY;
        player.velocityY_ = 0.0f;
        player.onGround_ = true;
    } else {
        player.onGround_ = false;
    }

    // 天井クランプ
    if (player.pos_.y > Tuning().ceilingY) {
        player.pos_.y = Tuning().ceilingY;
        player.velocityY_ = 0.0f;
    }
}

void Player::GroundedPhysicsState::Update(Player& player, Input* input) const
{
    const float speedMult = (player.isAwakened_ ? Tuning().awakenedSpeedMult : 1.0f) * player.skillMods_.speedMult;
    const float jumpMult = (player.isAwakened_ ? Tuning().awakenedJumpMult : 1.0f) * player.skillMods_.jumpMult;

    // 回避中は回避の移動が位置を決めるので、通常の左右移動とジャンプは受け付けない
    if (!player.IsRampaging() && !player.finisherCharging_ && !player.dodgeActive_) {
        player.ApplyHorizontalInput(input, Tuning().speed * speedMult);
    }

    if (player.onGround_ && !player.finisherCharging_ && !player.dodgeActive_) {
        if (input->TriggerAction(Input::Action::Jump) || player.autoCombo_.ConsumeJumpRequest()) {
            // 打ち上げ直後は追撃用に高く跳べる（浮かせた敵にジャンプで追いつく）
            float followMult = (player.launchFollowTimer_ > 0.0f) ? Tuning().launchFollowJumpMult : 1.0f;
            player.velocityY_ = Tuning().jumpPower * jumpMult * followMult;
            player.onGround_ = false;
            player.SetFrameEvent(FrameEvent::Jumped);
        }
    }

    const float groundY = player.useStageFloor_ ? (std::numeric_limits<float>::lowest)() : Tuning().groundY;
    if (ApplyGravityAndClampY(player.pos_.y, player.velocityY_, Tuning().gravity, groundY, Tuning().ceilingY)) {
        player.onGround_ = true;
    }

    player.SetFrameEvent(FrameEvent::Landed, !player.prevOnGround_ && player.onGround_);
}

const Player::IPhysicsState& Player::GetPhysicsState(bool inWater)
{
    static GroundedPhysicsState grounded;
    static UnderwaterPhysicsState underwater;
    return inWater ? static_cast<const IPhysicsState&>(underwater) : static_cast<const IPhysicsState&>(grounded);
}
