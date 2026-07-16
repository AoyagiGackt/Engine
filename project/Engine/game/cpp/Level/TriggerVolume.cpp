#include "TriggerVolume.h"
#include "GameFlags.h"
using namespace engine::game;

void TriggerVolume::Update(const Vector3& playerPos)
{
    if (desc_.once && consumed_) {
        return;
    }

    const float dx = playerPos.x - desc_.position.x;
    const float dy = playerPos.y - desc_.position.y;
    const float dz = playerPos.z - desc_.position.z;
    const bool inside = (dx * dx + dy * dy + dz * dz) <= (desc_.radius * desc_.radius);

    if (inside && !wasInside_ && !desc_.flag.empty()) {
        GameFlags::GetInstance()->SetFlag(desc_.flag, desc_.value);
        consumed_ = true;
    }
    wasInside_ = inside;
}
