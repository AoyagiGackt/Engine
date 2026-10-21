/** @file EnemyEntityVisuals.cpp
 * @brief 敵の装備位置とヒットストップ中の見た目更新
 */
#include "EnemyEntity.h"
namespace engine::game {
/** @brief 落とした武器を地面まで落とし、寝かせた姿勢で置く */
void EnemyEntity::UpdateDroppedWeapon()
{
    if (!weaponDropped_ || droppedWeaponLanded_)
    {
        return;
    }
    droppedWeaponVelY_ -= kDroppedWeaponGravity;
    droppedWeaponPos_.y += droppedWeaponVelY_;
    droppedWeaponSpin_ += kDroppedWeaponSpinPerFrame;
    if (droppedWeaponPos_.y <= droppedWeaponGroundY_)
    {
        droppedWeaponPos_.y = droppedWeaponGroundY_;
        droppedWeaponLanded_ = true;
    }
}

/** @brief 武器の位置を本体の手元（落としていれば落とした場所）へ合わせる */
void EnemyEntity::PlaceWeapon(const Vector3& bodyPos)
{
    if (weaponDropped_)
    {
        weaponObject_->SetPosition(droppedWeaponPos_);
        return;
    }
    weaponObject_->SetPosition({ bodyPos.x + facingSign_ * kWeaponOffsetX, bodyPos.y + kWeaponOffsetY, bodyPos.z + kWeaponOffsetZ });
}

/** @brief ヒットストップ中の1フレーム（物理・AIを止め、被弾直後なら本体を左右に震わせて描画行列だけ更新する） */
void EnemyEntity::UpdateDuringHitStop()
{
    // 1フレームだけ立つフラグは停止前のフレームでシーン側が読み終えているため、停止明けに二重に拾われないよう落とす
    justLanded_ = false;
    justFiredAttack_ = false;
    justStartedTelegraph_ = false;

    Vector3 visualPos = pos_;
    if (hitFlashTimer_ > 0.0f)
    {
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
    weaponObject_->Update();
    SyncMonsterVisual(false);
}
} // namespace engine::game
