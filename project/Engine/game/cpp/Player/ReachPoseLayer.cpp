/**
 * @file ReachPoseLayer.cpp
 * @brief ボーンをモデルの座標の向きへ伸ばすポーズレイヤー（ReachPoseLayer）の実装
 */
#include "ReachPoseLayer.h"
#include "Quaternion.h"
#include "Skeleton.h"
#include <algorithm>
#include <optional>

using engine::graphics::Joint;
using engine::graphics::PoseLayerRotation;
using engine::graphics::Skeleton;

namespace engine::game {
namespace {
constexpr Quaternion kIdentity = { 0.0f, 0.0f, 0.0f, 1.0f };

/** @brief 最初に見つかった子ボーンの番号（子が無ければ空） */
std::optional<int32_t> FirstChildOf(const Skeleton& skeleton, int32_t index) {
    for (size_t i = 0; i < skeleton.GetJointCount(); ++i) {
        const Joint& joint = skeleton.GetJoint(i);
        if (joint.parent.has_value() && *joint.parent == index) {
            return joint.index;
        }
    }
    return std::nullopt;
}

/** @brief このレイヤーがすでに集めた差分回転のうち、指定ボーンのもの（無ければ単位回転） */
Quaternion AppliedDelta(int32_t index, const std::vector<PoseLayerRotation>& applied) {
    const auto found = std::find_if(applied.begin(), applied.end(), [index](const PoseLayerRotation& rotation) {
        return rotation.jointIndex == index;
    });
    return found != applied.end() ? found->delta : kIdentity;
}

/**
 * @brief ボーンのモデルの座標での回転（根から順に、ローカルの回転と集めた差分を重ねたもの）
 * @note 行列の合成と同じく、親の回転の後に子のローカルの回転を掛ける（Multiply(親, 子)）
 */
Quaternion ModelRotation(const Skeleton& skeleton, int32_t index, const std::vector<PoseLayerRotation>& applied) {
    // 自分から根までたどった順を、根から順に掛け直す
    std::vector<int32_t> chain;
    for (std::optional<int32_t> current = index; current.has_value(); current = skeleton.GetJoint(*current).parent) {
        chain.push_back(*current);
    }
    Quaternion rotation = kIdentity;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        const Quaternion local = Multiply(skeleton.GetJoint(*it).transform.rotate, AppliedDelta(*it, applied));
        rotation = Normalize(Multiply(rotation, local));
    }
    return rotation;
}
} // namespace

/** @brief 一覧の各ボーンを目標の向きへ寄せる差分回転を集める */
void ReachPoseLayer::Collect(const Skeleton& skeleton, std::vector<PoseLayerRotation>& out) const {
    for (const Reach& reach : reaches_) {
        // 寄せない・ボーンが無い・向きを決める子ボーンが無いものは飛ばす
        const std::optional<int32_t> index = skeleton.FindJointIndex(reach.bone);
        if (reach.weight <= 0.0f || !index.has_value()) {
            continue;
        }
        const std::optional<int32_t> child = FirstChildOf(skeleton, *index);
        if (!child.has_value()) {
            continue;
        }
        // 今の姿勢でのボーンの向き（子ボーンの付け根への向き）を、モデルの座標で求める
        const Quaternion boneRotation = ModelRotation(skeleton, *index, out);
        const Vector3 boneDirection = RotateVector(boneRotation, skeleton.GetJoint(*child).transform.translate);
        // モデルの座標での回し方を割合ぶんだけにし、ボーンのローカル軸での差分（G の逆 × 回転 × G）へ直す
        const Quaternion turn = Slerp(kIdentity, MakeRotationFromTo(boneDirection, reach.direction), (std::min)(reach.weight, 1.0f));
        const Quaternion delta = Multiply(Multiply(Conjugate(boneRotation), turn), boneRotation);
        out.push_back({ *index, Normalize(delta) });
    }
}

} // namespace engine::game
