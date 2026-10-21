/** @file EnemySeparation.h
 * @brief 敵どうしと地形の横方向の重なり解消
 */
#pragma once
#include "CollisionConfig.h"
#include <utility>
#include <vector>
namespace engine::game {
class IEnemyEntity;
/** @brief 敵の位置補正をステージ編集機能から独立して実行する */
class EnemySeparation {
public:
    using Target = std::pair<IEnemyEntity*, bool>;
    /** @brief 対象の敵と固定フラグから位置を補正し、移動した敵を返す */
    static std::vector<IEnemyEntity*> Resolve(const std::vector<Target>& targets, const std::vector<AABB>& walls);
};
}
