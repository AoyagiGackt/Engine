/** @file EnemyEntityVisuals.cpp
 * @brief 敵の装備位置とヒットストップ中の見た目更新
 */
#include "EnemyEntity.h"
#include "GameConstants.h"
#include <algorithm>
#include <cmath>
namespace engine::game {
/** @brief 落とした武器を地面まで落とし、寝かせた姿勢で置く */
void EnemyEntity::UpdateDroppedWeapon() {
    if (!weaponDropped_ || droppedWeaponLanded_) {
        return;
    }
    droppedWeaponVelY_ -= kDroppedWeaponGravity;
    droppedWeaponPos_.y += droppedWeaponVelY_;
    droppedWeaponSpin_ += kDroppedWeaponSpinPerFrame;
    if (droppedWeaponPos_.y <= droppedWeaponGroundY_) {
        droppedWeaponPos_.y = droppedWeaponGroundY_;
        droppedWeaponLanded_ = true;
    }
}

/** @brief 武器の位置を本体の手元（落としていれば落とした場所）へ合わせる */
void EnemyEntity::PlaceWeapon(const Vector3& bodyPos) {
    if (weaponDropped_) {
        weaponObject_->SetPosition(droppedWeaponPos_);
        return;
    }
    weaponObject_->SetPosition({ bodyPos.x + facingSign_ * kWeaponOffsetX, bodyPos.y + kWeaponOffsetY, bodyPos.z + kWeaponOffsetZ });
}

/** @brief 武器を奪いに行く位置（落とした武器があればそこ、倒れていれば寝た体の真ん中、それ以外は足元） */
Vector3 EnemyEntity::GetWeaponPickupPosition() const {
    if (weaponDropped_) {
        return droppedWeaponPos_;
    }
    if (!defeated_) {
        return pos_;
    }
    // 体は足元を軸にZ軸まわりへ倒れるので、立っていた時の体の真ん中を同じだけ回した位置を使う
    const float lean = object_->GetRotation().z;
    return { pos_.x - std::sin(lean) * kCorpseCenterHeight, pos_.y + std::cos(lean) * kCorpseCenterHeight, pos_.z };
}

/** @brief 撃破後、まだ吸い寄せていない武器を回収位置の上へ浮かべて回し、光らせる */
void EnemyEntity::UpdateOfferedWeapon(bool advance) {
    if (!defeated_ || weaponPulled_) {
        return;
    }
    if (advance) {
        offerTimer_ += gameConstants::kFrameDeltaTime;
    }
    // 手元（落ちた武器なら落ちた場所）から、回収位置の上へ勢いよく上がって減速する
    const float rise = (std::min)(offerTimer_ / kOfferRiseSeconds, 1.0f);
    const float eased = 1.0f - (1.0f - rise) * (1.0f - rise);
    const Vector3 from = weaponObject_->GetTransform().translate;
    const Vector3 base = GetWeaponPickupPosition();
    const float hover = std::sin(offerTimer_ * kOfferHoverSpeed) * kOfferHoverAmplitude;
    const Vector3 to = { base.x, base.y + kOfferHeight + hover, base.z };
    weaponObject_->SetPosition({ from.x + (to.x - from.x) * eased, from.y + (to.y - from.y) * eased, from.z + (to.z - from.z) * eased });
    weaponObject_->SetRotation({ kOfferTilt, offerTimer_ * kOfferSpinSpeed, 0.0f });
    const float scale = 1.0f + (kOfferScaleBoost - 1.0f) * eased;
    weaponObject_->SetScale({ weaponScale_.x * scale, weaponScale_.y * scale, weaponScale_.z * scale });
    // 撃破後の灰色ではなく、白と淡い青の間で明滅させて拾えることを目立たせる
    constexpr float kHalf = 0.5f;
    const float pulse = kHalf + kHalf * std::sin(offerTimer_ * kOfferPulseSpeed);
    weaponObject_->SetColor({
        kOfferDimColor.x + (1.0f - kOfferDimColor.x) * pulse,
        kOfferDimColor.y + (1.0f - kOfferDimColor.y) * pulse,
        kOfferDimColor.z + (1.0f - kOfferDimColor.z) * pulse,
        kOfferDimColor.w });
}

/** @brief ヒットストップ中の1フレーム（物理・AIを止め、被弾直後なら本体を左右に震わせて描画行列だけ更新する） */
void EnemyEntity::UpdateDuringHitStop() {
    // 1フレームだけ立つフラグは停止前のフレームでシーン側が読み終えているため、停止明けに二重に拾われないよう落とす
    justLanded_ = false;
    justFiredAttack_ = false;
    justStartedTelegraph_ = false;

    Vector3 visualPos = pos_;
    if (hitFlashTimer_ > 0.0f) {
        // 殴られた敵だけを白く光らせたまま左右に小刻みに震わせる
        ++hitStopShakeFrame_;
        visualPos.x += (hitStopShakeFrame_ % 2 == 0 ? 1.0f : -1.0f) * kHitStopShakeAmplitude;
        SetBodyColor(defeated_ ? kDefeatedColor : kHitFlashColor);
        weaponObject_->SetColor(defeated_ ? kDefeatedColor : kHitFlashColor);
        const float punchedScale = kBodyScale * (1.0f + kHitScalePunch);
        object_->SetScale({ punchedScale, punchedScale, punchedScale });
    }
    object_->SetPosition(visualPos);
    PlaceWeapon(visualPos);

    // アニメーションも止めたまま行列だけ更新する（カメラ揺れで画面に張り付かないように）
    const float animSpeed = object_->GetAnimSpeed();
    object_->SetAnimSpeed(0.0f);
    object_->Update();
    object_->SetAnimSpeed(animSpeed);
    UpdateOfferedWeapon(false);
    weaponObject_->Update();
    SyncMonsterVisual(false);
}
} // namespace engine::game
