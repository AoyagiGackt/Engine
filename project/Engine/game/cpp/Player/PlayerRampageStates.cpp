/**
 * @file PlayerRampageStates.cpp
 * @brief プレイヤーの乱舞の状態（待機・打ち上げ・空中連撃）の実装
 */
#include "GameConstants.h"
#include "Input.h"
#include "Player.h"
#include "PlayerTuning.h"
#include <algorithm>
#include <cmath>


namespace engine::game {
namespace {
/** @brief 読み込み済みの調整値を取得する */
const PlayerTuning& Tuning()
{
    return PlayerTuning::Get();
}
} // namespace

//  Rampage State（覚醒乱舞の進行フェーズ）


/** @brief 乱舞開始の入力と条件を受け付ける通常状態 */
class Player::InactiveRampageState : public IRampageState {
    public:
        void HandleAttackInput(Player& player, Input* input, const Vector3& enemyPos) const override;
        /** @brief 乱舞していない時は物理に手を加えない */
        void UpdatePhysics(Player& player, const Vector3& enemyPos) const override { }
        /** @brief 乱舞の最中かを返す */
        bool IsActive() const override {
            return false;
        }
    };
/** @brief 乱舞の打ち上げ動作を進める状態 */
class Player::LaunchRampageState : public IRampageState {
    public:
        /** @brief 打ち上げ中は攻撃入力を扱わない */
        void HandleAttackInput(Player& player, Input* input, const Vector3& enemyPos) const override { }
        void UpdatePhysics(Player& player, const Vector3& enemyPos) const override;
    };
/** @brief 打ち上げた対象への追撃入力を処理する乱舞状態 */
class Player::JuggleRampageState : public IRampageState {
    public:
        void HandleAttackInput(Player& player, Input* input, const Vector3& enemyPos) const override;
        void UpdatePhysics(Player& player, const Vector3& enemyPos) const override;
        /** @brief 空中連撃の最中かを返す */
        bool IsJuggling() const override {
            return true;
        }
    };


/** @brief 通常時の攻撃入力はコンボ側が扱うので何もしない */
void Player::InactiveRampageState::HandleAttackInput(Player&, Input*, const Vector3&) const
{
    // 通常時の攻撃入力は MeleeComboController（Player::Update 内）が担当するため何もしない
    // （乱舞の開始判定も覚醒ソード限定なので Update 側で行う）
}

/** @brief 敵へ向かって突進し、十分近づいたら打ち上げて空中連撃へ移る */
void Player::LaunchRampageState::UpdatePhysics(Player& player, const Vector3& enemyPos) const
{
    // 敵X座標へ向かって突進（重力無効）
    // ステージ外の座標が渡されてもプレイヤーが到達できる位置にクランプ
    float targetX = std::clamp(enemyPos.x, player.minX_ + kBodyHalfExtent, player.maxX_ - kBodyHalfExtent);
    float dx = targetX - player.pos_.x;
    float dir = (dx > 0.0f) ? 1.0f : -1.0f;
    player.lastDirX_ = dir;
    player.velocityY_ = 0.0f;
    player.pos_.x += dir * Tuning().rampageSpeed;
    player.pos_.x = std::clamp(player.pos_.x, player.minX_, player.maxX_);

    // 十分近づいたら打ち上げヒット → ジャグルフェーズへ
    if (std::abs(dx) < Tuning().rampageReachDistance)
    {
        player.SetFrameEvent(FrameEvent::Launched);
        player.velocityY_ = Tuning().rampageLaunchSpeed;
        player.rampage_ = &JuggleRampage();
    }
}
/** @brief 攻撃入力のたびに敵の周りの次の角度へ移って斬る（最後の一撃で乱舞を終える） */
void Player::JuggleRampageState::HandleAttackInput(Player& player, Input* input, const Vector3& enemyPos) const
{
    const int effectiveMax = Tuning().juggleMaxSlashes + player.skillMods_.juggleMaxBonus;
    if (player.juggleSlashCount_ >= effectiveMax)
    {
        return;
    }

    // 乱舞中 L → 敵の周囲の次の角度へテレポートしてスラッシュ
    float angle = player.juggleAngleIdx_ * (GameConstants::kTwoPi / effectiveMax);
    float dx = std::cos(angle) * Tuning().juggleRadius;
    float dy = std::sin(angle) * Tuning().juggleRadius;
    player.pos_.x = std::clamp(enemyPos.x + dx, player.minX_, player.maxX_);
    player.pos_.y = std::clamp(enemyPos.y + dy, Tuning().groundY, Tuning().ceilingY);
    player.velocityY_ = 0.0f;
    player.lastDirX_ = (enemyPos.x >= player.pos_.x) ? 1.0f : -1.0f;
    player.juggleAngleIdx_ = (player.juggleAngleIdx_ + 1) % effectiveMax;
    player.juggleSlashCount_++;

    bool isLast = (player.juggleSlashCount_ >= effectiveMax);
    player.SetFrameEvent(FrameEvent::RampageHit);
    player.SetFrameEvent(FrameEvent::RampageFinish, isLast);
    if (isLast)
    {
        player.rampage_ = &InactiveRampage();
    }
}

/** @brief 空中連撃中は重力を止めて空中に留まる */
void Player::JuggleRampageState::UpdatePhysics(Player& player, const Vector3& enemyPos) const
{
    // ジャグル中は重力無効でホバリング
    player.velocityY_ = 0.0f;
}

/** @brief 乱舞していない時の共有インスタンスを返す */
const Player::IRampageState& Player::InactiveRampage()
{
    static const InactiveRampageState instance;
    return instance;
}

/** @brief 打ち上げ中の共有インスタンスを返す */
const Player::IRampageState& Player::LaunchRampage()
{
    static const LaunchRampageState instance;
    return instance;
}

/** @brief 空中連撃中の共有インスタンスを返す */
const Player::IRampageState& Player::JuggleRampage()
{
    static const JuggleRampageState instance;
    return instance;
}

} // namespace engine::game
