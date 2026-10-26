/**
 * @file PlayerStatus.cpp
 * @brief プレイヤーの状態（乱舞の終了・移動範囲など）を外から操作・取得する関数の実装
 */
#include "Player.h"

namespace engine::game {
/** @brief 乱舞フェーズを強制終了する（外部から撃破時などに呼ぶ） */
void Player::EndRampage() {
    if (rampage_->IsJuggling()) {
        rampage_ = &InactiveRampage();
    }
}

/** @brief プレイヤーが動ける左右の範囲を設定する */
void Player::SetHorizontalBounds(float minX, float maxX) {
    minX_ = minX;
    maxX_ = maxX;
}

/** @brief プレイヤーの現在位置から AABB コライダーを生成して返す */
Collider Player::GetCollider() const {
    Collider c;
    c.SetAsAABB({ { pos_.x - kBodyHalfExtent, pos_.y - kBodyHalfExtent, -kBodyHalfExtent },
        { pos_.x + kBodyHalfExtent, pos_.y + kBodyHalfExtent, kBodyHalfExtent } });
    return c;
}

/** @brief 回避中に敵の攻撃を紙一重で避けた（ジャスト回避）ことを1回だけ成立させる */
bool Player::ConsumeJustDodge() {
    if (!dodgeActive_ || dodgeRewardClaimed_) {
        return false;
    }
    dodgeRewardClaimed_ = true;
    return true;
}

/** @brief 攻撃が実際に命中した際にシーン側の当たり判定から呼び、覚醒ゲージを加算する */
void Player::ChargeAwakenGauge(float amount) {
    if (!isAwakened_) {
        awakenGauge_ = (std::min)(awakenGauge_ + amount * skillMods_.gaugeChargeMult, 1.0f);
    }
}

/** @brief このフレームに固有技の攻撃判定が出たか（叩きつけ・スピン連射・鎌の回転・斧の突進のどれか） */
bool Player::JustWeaponSkillHit() const {
    // 固有技の攻撃判定になる出来事（シーン側はどれか1つでも立っていれば周囲へ判定を出す）
    static constexpr FrameEvent kSkillHitEvents[] = {
        FrameEvent::kGreatswordSlam,
        FrameEvent::kSpinShot,
        FrameEvent::kScytheSpin,
        FrameEvent::kAxeCharge,
    };
    for (FrameEvent event : kSkillHitEvents) {
        if (HasFrameEvent(event)) {
            return true;
        }
    }
    return false;
}

} // namespace engine::game
