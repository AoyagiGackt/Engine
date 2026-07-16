#include "Player.h"
#include "GameConstants.h"
#include "Input.h"
#include <algorithm>
#include <cmath>

using namespace engine;
using namespace engine::game;

//  Rampage State（覚醒乱舞の進行フェーズ）

void Player::InactiveRampageState::HandleAttackInput(Player&, Input*, const Vector3&) const
{
    // 通常時の攻撃入力は MeleeComboController（Player::Update 内）が担当するため何もしない
    // （乱舞の開始判定も覚醒ソード限定なので Update 側で行う）
}

void Player::LaunchRampageState::UpdatePhysics(Player& player, const Vector3& enemyPos) const
{
    // 敵X座標へ向かって突進（重力無効）
    // ステージ外の座標が渡されてもプレイヤーが到達できる位置にクランプ
    float targetX = std::clamp(enemyPos.x, kMinX_ + 0.5f, kMaxX_ - 0.5f);
    float dx = targetX - player.pos_.x;
    float dir = (dx > 0.0f) ? 1.0f : -1.0f;
    player.lastDirX_ = dir;
    player.velocityY_ = 0.0f;
    player.pos_.x += dir * kRampageSpeed_;
    player.pos_.x = std::clamp(player.pos_.x, kMinX_, kMaxX_);

    // 十分近づいたら打ち上げヒット → ジャグルフェーズへ
    if (std::abs(dx) < 1.0f) {
        player.justLaunched_ = true;
        player.velocityY_ = 0.25f;
        player.rampagePhase_ = RampagePhase::Juggle;
    }
}
void Player::JuggleRampageState::HandleAttackInput(Player& player, Input* input, const Vector3& enemyPos) const
{
    const int effectiveMax = kJuggleMaxSlashes_ + player.skillMods_.juggleMaxBonus;
    if (player.juggleSlashCount_ >= effectiveMax) {
        return;
    }

    // 乱舞中 L → 敵の周囲の次の角度へテレポートしてスラッシュ
    float angle = player.juggleAngleIdx_ * (GameConstants::kTwoPi / effectiveMax);
    float dx = std::cos(angle) * kJuggleRadius_;
    float dy = std::sin(angle) * kJuggleRadius_;
    player.pos_.x = std::clamp(enemyPos.x + dx, kMinX_, kMaxX_);
    player.pos_.y = std::clamp(enemyPos.y + dy, kGroundY_, kCeilingY_);
    player.velocityY_ = 0.0f;
    player.lastDirX_ = (enemyPos.x >= player.pos_.x) ? 1.0f : -1.0f;
    player.juggleAngleIdx_ = (player.juggleAngleIdx_ + 1) % effectiveMax;
    player.juggleSlashCount_++;

    bool isLast = (player.juggleSlashCount_ >= effectiveMax);
    player.justRampageHit_ = true;
    player.justRampageFinish_ = isLast;
    if (isLast) {
        player.rampagePhase_ = RampagePhase::Inactive;
    }
}

void Player::JuggleRampageState::UpdatePhysics(Player& player, const Vector3& enemyPos) const
{
    // ジャグル中は重力無効でホバリング
    player.velocityY_ = 0.0f;
}

const Player::IRampageState& Player::GetRampageState(RampagePhase phase)
{
    static InactiveRampageState inactive;
    static LaunchRampageState launch;
    static JuggleRampageState juggle;
    switch (phase) {
    case RampagePhase::Launch:
        return launch;
    case RampagePhase::Juggle:
        return juggle;
    default:
        return inactive;
    }
}
