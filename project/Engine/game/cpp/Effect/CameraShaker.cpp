/**
 * @file CameraShaker.cpp
 * @brief CameraShakerの画面効果の生成、更新、描画に関する具体的な処理を実装するファイル
 */
#include "CameraShaker.h"
#include <algorithm>
#include <cmath>
using namespace engine;
using namespace engine::game;

void CameraShaker::Request(float intensity, float duration)
{
    if (intensity > intensity_ || timer_ <= 0.0f) {
        intensity_ = intensity;
        duration_ = duration;
        timer_ = duration;
        ++seed_;
    }
}

Vector3 CameraShaker::Update(float dt)
{
    if (timer_ <= 0.0f) {
        return { };
    }

    timer_ -= dt;
    if (timer_ < 0.0f) {
        timer_ = 0.0f;
    }

    // 線形減衰した振幅
    float t = (duration_ > 0.0f) ? (timer_ / duration_) : 0.0f;
    float mag = intensity_ * t;

    // sin 重ね合わせによる擬似ランダム揺れ
    float s = static_cast<float>(seed_) * 2.7f;
    float phase = timer_ * 60.0f;
    float ox = mag * (std::sin(phase * 1.7f + s) * 0.6f + std::sin(phase * 3.1f + s * 2.1f) * 0.4f);
    float oy = mag * (std::cos(phase * 1.3f + s * 1.5f) * 0.6f + std::cos(phase * 2.9f + s * 0.7f) * 0.4f);

    return { ox, oy, 0.0f };
}
