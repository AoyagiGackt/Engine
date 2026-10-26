/** @file EnemySeparation.h
 * @brief 敵どうし・敵とプレイヤー・敵と地形の横方向の重なり解消
 */
#pragma once
#include "CollisionConfig.h"
#include <optional>
#include <utility>
#include <vector>
namespace engine::game {
class IEnemyEntity;
/** @brief 敵の位置補正をステージ編集機能から独立して実行する */
class EnemySeparation {
public:
    using Target = std::pair<IEnemyEntity*, bool>;
    /**
     * @brief 対象の敵と固定フラグから位置を補正し、移動した敵を返す
     * @param playerPosition 渡すと、敵をプレイヤーからも離す（プレイヤーは動かさない。ボスも離す側に含める）
     */
    static std::vector<IEnemyEntity*> Resolve(const std::vector<Target>& targets, const std::vector<AABB>& walls,
        const std::optional<Vector3>& playerPosition = std::nullopt);
};
}
