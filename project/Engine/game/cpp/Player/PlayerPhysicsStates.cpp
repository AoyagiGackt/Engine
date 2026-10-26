/**
 * @file PlayerPhysicsStates.cpp
 * @brief プレイヤーの物理の状態（地上・空中と水中）の実装
 */
#include "GameConstants.h"
#include "GravityBody.h"
#include "Input.h"
#include "Player.h"
#include "PlayerTuning.h"
#include <algorithm>
#include <limits>


namespace engine::game {
namespace {
/** @brief 読み込み済みの調整値を取得する */
const PlayerTuning& Tuning() {
    return PlayerTuning::Get();
}

/** @brief 横移動の入力1つぶん（押している間その向きへ進む） */
struct MoveInput {
    Input::Action action;
    float dirX;
};
// 両方押した時は後ろの行が向きを決める
constexpr MoveInput kMoveInputs[] = {
    { Input::Action::kMoveLeft, -1.0f },
    { Input::Action::kMoveRight, 1.0f },
};
} // namespace

/** @brief 左右の移動入力ぶん step だけ横へ動かし、向きを更新する */
void Player::ApplyHorizontalInput(Input* input, float step) {
    for (const MoveInput& move : kMoveInputs) {
        if (input->PushAction(move.action)) {
            pos_.x += move.dirX * step;
            lastDirX_ = move.dirX;
        }
    }
}

/** @brief 押している左右の向き（-1/+1、押していなければ0。両方なら左を優先） */
float Player::GetHeldMoveDirection(Input* input) const {
    for (const MoveInput& move : kMoveInputs) {
        if (input->PushAction(move.action)) {
            return move.dirX;
        }
    }
    return 0.0f;
}

//  Physics State（水中/水上）


/** @brief 地上移動とジャンプの入力を物理へ反映する状態 */
class Player::GroundedPhysicsState : public IPhysicsState {
    public:
        void Update(Player& player, Input* input) const override;
    };
/** @brief 水の抵抗と浮力を使って水中移動を進める状態 */
class Player::UnderwaterPhysicsState : public IPhysicsState {
    public:
        void Update(Player& player, Input* input) const override;
    };


/** @brief 水中での移動・浮力・泳ぎを1フレーム進める */
void Player::UnderwaterPhysicsState::Update(Player& player, Input* input) const {
    const float speedMult = (player.isAwakened_ ? Tuning().awakenedSpeedMult : 1.0f) * player.skillMods_.speedMult;

    // 横移動（水の抵抗で遅い）
    player.ApplyHorizontalInput(input, Tuning().waterSpeed * speedMult);

    // 浮力（弱い下向き加速）
    player.velocityY_ -= Tuning().waterGravity;
    player.velocityY_ = (std::max)(player.velocityY_, Tuning().sinkMaxVY);

    // ジャンプ長押し = 上昇スイム
    if (input->PushAction(Input::Action::kJump)) {
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

/** @brief 地上・空中での移動・重力・ジャンプを1フレーム進める */
void Player::GroundedPhysicsState::Update(Player& player, Input* input) const {
    const float speedMult = (player.isAwakened_ ? Tuning().awakenedSpeedMult : 1.0f) * player.skillMods_.speedMult;
    const float jumpMult = (player.isAwakened_ ? Tuning().awakenedJumpMult : 1.0f) * player.skillMods_.jumpMult;

    // 回避中は回避の移動が位置を決めるので、通常の左右移動とジャンプは受け付けない
    if (!player.IsRampaging() && !player.finisherCharging_ && !player.dodgeActive_) {
        player.ApplyHorizontalInput(input, Tuning().speed * speedMult);
    }

    if (player.onGround_ && !player.finisherCharging_ && !player.dodgeActive_) {
        if (input->TriggerAction(Input::Action::kJump) || player.autoCombo_.ConsumeJumpRequest()) {
            // 打ち上げ直後は追撃用に高く跳べる（浮かせた敵にジャンプで追いつく）
            float followMult = (player.launchFollowTimer_ > 0.0f) ? Tuning().launchFollowJumpMult : 1.0f;
            player.velocityY_ = Tuning().jumpPower * jumpMult * followMult;
            player.onGround_ = false;
            player.SetFrameEvent(FrameEvent::kJumped);
        }
    }

    const float groundY = player.useStageFloor_ ? (std::numeric_limits<float>::lowest)() : Tuning().groundY;
    // 空中射撃の各段の開始から一定時間、落下中だけ重力を弱める
    constexpr float kZero = 0.0f;
    constexpr float kNormalGravityScale = 1.0f;
    const bool gunFloating = !player.onGround_ && player.velocityY_ <= kZero
        && player.gunCombo_.GetActive() != nullptr
        && player.gunCombo_.GetElapsedTime() < Tuning().gunAirFloatDuration;
    const float gravityScale = gunFloating
        ? std::clamp(Tuning().gunAirGravityScale, kZero, kNormalGravityScale) : kNormalGravityScale;
    if (ApplyGravityAndClampY(player.pos_.y, player.velocityY_, Tuning().gravity * gravityScale, groundY, Tuning().ceilingY)) {
        player.onGround_ = true;
    }

    player.SetFrameEvent(FrameEvent::kLanded, !player.prevOnGround_ && player.onGround_);
}

/** @brief 水中かどうかに合った物理状態を返す */
const Player::IPhysicsState& Player::GetPhysicsState(bool inWater) {
    static GroundedPhysicsState sGrounded;
    static UnderwaterPhysicsState sUnderwater;
    return inWater ? static_cast<const IPhysicsState&>(sUnderwater) : static_cast<const IPhysicsState&>(sGrounded);
}

} // namespace engine::game
