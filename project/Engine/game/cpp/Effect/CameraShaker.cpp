/**
 * @file CameraShaker.cpp
 * @brief 時間制御されたカメラ振動オフセット（CameraShaker）の実装
 */
#include "CameraShaker.h"
#include "GraphicsTuning.h"
#include <algorithm>
#include <cmath>

namespace engine::game {
namespace {
/** @brief 描画設定JSONから読むカメラ振動の周波数・位相・混合比。既定値には従来の係数を使う */
struct CameraShakeSettings {
    float seedPhaseScale = graphics::GraphicsTuning::Float("cameraShake.seedPhaseScale", 2.7f);
    float phaseSpeed = graphics::GraphicsTuning::Float("cameraShake.phaseSpeed", 60.0f);
    float majorWeight = graphics::GraphicsTuning::Float("cameraShake.majorWeight", 0.6f, 0.0f, 1.0f);
    float minorWeight = graphics::GraphicsTuning::Float("cameraShake.minorWeight", 0.4f, 0.0f, 1.0f);
    float xMajorFrequency = graphics::GraphicsTuning::Float("cameraShake.xMajorFrequency", 1.7f);
    float xMinorFrequency = graphics::GraphicsTuning::Float("cameraShake.xMinorFrequency", 3.1f);
    float xMinorSeedScale = graphics::GraphicsTuning::Float("cameraShake.xMinorSeedScale", 2.1f);
    float yMajorFrequency = graphics::GraphicsTuning::Float("cameraShake.yMajorFrequency", 1.3f);
    float yMajorSeedScale = graphics::GraphicsTuning::Float("cameraShake.yMajorSeedScale", 1.5f);
    float yMinorFrequency = graphics::GraphicsTuning::Float("cameraShake.yMinorFrequency", 2.9f);
    float yMinorSeedScale = graphics::GraphicsTuning::Float("cameraShake.yMinorSeedScale", 0.7f);
};

/** @brief 初めて振動を更新する時に設定を読み、以降のフレームでは同じ設定を共有する */
const CameraShakeSettings& ShakeSettings() {
    static const CameraShakeSettings kSettings;
    return kSettings;
}
}

/** @brief カメラ振動を開始する */
void CameraShaker::Request(float intensity, float duration) {
    if (intensity > intensity_ || timer_ <= 0.0f) {
        intensity_ = intensity;
        duration_ = duration;
        timer_ = duration;
        ++seed_;
    }
}

/** @brief 振動時間を進めて現在の位置オフセットを返す */
Vector3 CameraShaker::Update(float dt) {
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
    const CameraShakeSettings& settings = ShakeSettings();
    float s = static_cast<float>(seed_) * settings.seedPhaseScale;
    float phase = timer_ * settings.phaseSpeed;
    float ox = mag * (std::sin(phase * settings.xMajorFrequency + s) * settings.majorWeight + std::sin(phase * settings.xMinorFrequency + s * settings.xMinorSeedScale) * settings.minorWeight);
    float oy = mag * (std::cos(phase * settings.yMajorFrequency + s * settings.yMajorSeedScale) * settings.majorWeight + std::cos(phase * settings.yMinorFrequency + s * settings.yMinorSeedScale) * settings.minorWeight);

    return { ox, oy, 0.0f };
}

} // namespace engine::game
