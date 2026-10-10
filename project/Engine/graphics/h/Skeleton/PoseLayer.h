/**
 * @file PoseLayer.h
 * @brief ベースアニメを評価した後のスケルトンへ追加の回転を重ねる、ポーズレイヤーのインターフェースを定義するファイル
 */
#pragma once
#include "Quaternion.h"
#include <cstdint>
#include <vector>

namespace engine::graphics {

class Skeleton;

/** @brief レイヤーが1ボーンへ重ねる回転（ボーンのローカル空間での差分） */
struct PoseLayerRotation {
    int32_t jointIndex; ///< Skeleton::GetJoints() の添字
    Quaternion delta; ///< ベースの回転より先に掛ける差分回転
};

/**
 * @brief ポーズレイヤー
 * @note SkinnedObject3d::Update() がベースアニメを各ボーンへ書き込んだ後、階層行列を組む前に呼ぶ。
 *       レイヤーは差分回転を列挙するだけで、ジョイントへの適用と次の評価前の巻き戻しは SkinnedObject3d が行う
 *       （同じフレームに何度評価しても差分が積み重ならないようにするため）。
 */
class IPoseLayer {
public:
    virtual ~IPoseLayer() = default;

    /**
     * @brief 今の時刻で重ねる差分回転を集める
     * @param skeleton 対象のスケルトン（ボーン名から添字を引くために使う）
     * @param out      差分回転の追加先（呼び出し側で空にしてから渡す）
     */
    virtual void Collect(const Skeleton& skeleton, std::vector<PoseLayerRotation>& out) const = 0;
};

} // namespace engine::graphics
