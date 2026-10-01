/**
 * @file CameraShaker.cpp
 * @brief 時間制御されたカメラ振動オフセット（CameraShaker）の実装
 */
#include "CameraShaker.h"
#include <algorithm>
#include <cmath>
using namespace engine;
using namespace engine::game;

namespace {
// 周期の異なるsin/cosを重ねて擬似ランダムな揺れを作る係数
constexpr float kSeedPhaseScale = 2.7f;
constexpr float kPhaseSpeed = 60.0f;
constexpr float kMajorWeight = 0.6f; // 主となる低い周波数の揺れの比重
constexpr float kMinorWeight = 0.4f;
constexpr float kXMajorFreq = 1.7f;
constexpr float kXMinorFreq = 3.1f;
constexpr float kXMinorSeedScale = 2.1f;
constexpr float kYMajorFreq = 1.3f;
constexpr float kYMajorSeedScale = 1.5f;
constexpr float kYMinorFreq = 2.9f;
constexpr float kYMinorSeedScale = 0.7f;
}

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
    float s = static_cast<float>(seed_) * kSeedPhaseScale;
    float phase = timer_ * kPhaseSpeed;
    float ox = mag * (std::sin(phase * kXMajorFreq + s) * kMajorWeight + std::sin(phase * kXMinorFreq + s * kXMinorSeedScale) * kMinorWeight);
    float oy = mag * (std::cos(phase * kYMajorFreq + s * kYMajorSeedScale) * kMajorWeight + std::cos(phase * kYMinorFreq + s * kYMinorSeedScale) * kMinorWeight);

    return { ox, oy, 0.0f };
}
