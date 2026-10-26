/**
 * @file TriggerVolume.cpp
 * @brief プレイヤーの進入を検出してフラグへ知らせるトリガー（TriggerVolume）の実装
 */
#include "TriggerVolume.h"
#include "GameFlags.h"

namespace engine::game {
/** @brief プレイヤーが範囲内に入った瞬間だけフラグを立てる */
bool TriggerVolume::Update(const Vector3& playerPos) {
    if (desc_.once && consumed_) {
        return false;
    }

    const float dx = playerPos.x - desc_.position.x;
    const float dy = playerPos.y - desc_.position.y;
    const float dz = playerPos.z - desc_.position.z;
    const bool inside = (dx * dx + dy * dy + dz * dz) <= (desc_.radius * desc_.radius);

    const bool justEntered = inside && !wasInside_;
    if (justEntered) {
        if (!desc_.flag.empty()) {
            GameFlags::GetInstance()->SetFlag(desc_.flag, desc_.value);
        }
        consumed_ = true;
    }
    wasInside_ = inside;
    return justEntered;
}

} // namespace engine::game
