#pragma once
#include "MakeAffine.h"
namespace engine::game {
// カメラ振動エフェクト
class CameraShaker {
public:
    void Request(float intensity, float duration);

    // 毎フレーム呼ぶ。現在フレームの揺れオフセットを返す
    Vector3 Update(float dt);

    bool IsShaking() const { return timer_ > 0.0f; }

private:
    float timer_     = 0.0f;
    float duration_  = 0.0f;
    float intensity_ = 0.0f;
    int   seed_      = 0;
};

} // namespace engine::game
