/** @file IPositionTarget.h
 * @brief 内部座標への参照を公開せずに位置を操作する共通インターフェース
 */
#pragma once
#include "Vector3.h"
namespace engine::game {
/** @brief キャラクターとエディタが値の取得と更新を介して位置を共有する */
class IPositionTarget {
public:
    /** @brief 派生実体の寿命を正しく終了する */
    virtual ~IPositionTarget() = default;
    /** @brief 現在のワールド座標を値として取得する */
    virtual Vector3 GetPosition() const = 0;
    /** @brief ワールド座標を更新する。描画行列の更新は呼び出し側の更新順に従う */
    virtual void SetPosition(const Vector3& position) = 0;
    /** @brief 現在位置に相対移動量を加える */
    void MoveBy(const Vector3& delta) {
        SetPosition(GetPosition() + delta);
    }
    /** @brief 奥行きを保ち、目標へ水平と垂直の位置を補間する */
    void MoveTowardXY(const Vector3& target, float ratio)
    {
        Vector3 position = GetPosition();
        position.x += (target.x - position.x) * ratio;
        position.y += (target.y - position.y) * ratio;
        SetPosition(position);
    }
};
} // namespace engine::game
