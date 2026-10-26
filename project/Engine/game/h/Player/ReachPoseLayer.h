/**
 * @file ReachPoseLayer.h
 * @brief 指定したボーンを、モデルの座標で決めた向きへ伸ばすポーズレイヤー（演出で腕を伸ばす時などに使う）
 */
#pragma once
#include "PoseLayer.h"
#include "Vector3.h"
#include <string>
#include <vector>

namespace engine::game {

/**
 * @brief ボーンをモデルの座標の向きへ伸ばすポーズレイヤー
 * @note ボーンのローカル軸を知らなくても使えるよう、今の姿勢でのボーンの向き（子ボーンへの向き）から
 *       目標の向きまでの回転を求めて差分にする。ボーン名が見つからない・子ボーンが無いものは何もしない
 */
class ReachPoseLayer : public engine::graphics::IPoseLayer {
public:
    /** @brief 1本のボーンをどの向きへどれだけ伸ばすか */
    struct Reach {
        std::string bone; ///< 対象のボーン名
        Vector3 direction {}; ///< 伸ばしたい向き（モデルの座標。長さは問わない）
        float weight = 0.0f; ///< 目標の向きへ寄せる割合（0で元の姿勢、1で目標の向き）
    };

    /** @brief 掛けるボーンの一覧を差し替える（親から子の順に並べると、子は親を伸ばした後の向きから寄せる） */
    void SetReaches(const std::vector<Reach>& reaches) {
        reaches_ = reaches;
    }

    /** @brief 一覧の各ボーンを目標の向きへ寄せる差分回転を集める */
    void Collect(const engine::graphics::Skeleton& skeleton,
        std::vector<engine::graphics::PoseLayerRotation>& out) const override;

private:
    std::vector<Reach> reaches_;
};

} // namespace engine::game
