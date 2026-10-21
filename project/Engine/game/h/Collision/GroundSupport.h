/** @file GroundSupport.h
 * @brief キャラクターの足元にある支持面の検索
 */
#pragma once
#include "CollisionConfig.h"
#include <optional>
#include <vector>
namespace engine::game {
/** @brief 接地面の候補として許容する足元からの段差 */
inline constexpr float kGroundStepTolerance = 0.12f;

/** @brief 体の水平範囲に重なる足元以下の最も高い床上面を返す。奥行き幅がゼロなら中心点で判定する */
inline std::optional<float> FindSupportingFloor(const Vector3& position, float halfWidth,
    float halfDepth, float footOffset, const std::vector<AABB>& blocks)
{
    std::optional<float> highest;
    const float maxTop = position.y - footOffset + kGroundStepTolerance;
    for (const AABB& block : blocks)
    {
        const bool overlapX = position.x + halfWidth > block.min.x && position.x - halfWidth < block.max.x;
        const bool overlapZ = halfDepth > 0.0f
            ? position.z + halfDepth > block.min.z && position.z - halfDepth < block.max.z
            : position.z >= block.min.z && position.z <= block.max.z;
        if (overlapX && overlapZ && block.max.y <= maxTop && (!highest || block.max.y > *highest))
        {
            highest = block.max.y;
        }
    }
    return highest;
}
} // namespace engine::game
