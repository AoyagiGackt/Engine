/**
 * @file FallingFloorBehavior.h
 * @brief 崩れる床の周期的な表示・衝突・予告演出を評価するクラスを定義するファイル
 */
#pragma once
#include "Vector3.h"
namespace engine::graphics { class ParticleManager; }
namespace engine::game {
/** @brief 崩れる床の表示・衝突・予告演出を同じ周期で評価する。 */
class FallingFloorBehavior {
public:
    struct Snapshot {
        float yOffset = 0.0f;
        bool visible = true;
        bool solid = true;
        float phase = 0.0f;
    };
    static Snapshot Evaluate(float runtimeTimer, float motionSpeed, float motionAmount);
    static void EmitDebris(engine::graphics::ParticleManager* particles, const Vector3& position,
        const Vector3& halfExtent, const Snapshot& state, bool& wasSolid);
};
}
