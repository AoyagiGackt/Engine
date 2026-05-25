#pragma once

namespace GameConstants {
    // フレームレート
    inline constexpr float kTargetFps      = 60.0f;
    inline constexpr float kFrameDeltaTime = 1.0f / kTargetFps;

    // スクリーン中央（WinApp::kClientWidth/Height = 1280/720 と一致させること）
    inline constexpr float kScreenCenterX = 640.0f;
    inline constexpr float kScreenCenterY = 360.0f;

    // 数学定数
    inline constexpr float kPi    = 3.14159265358979323846f;
    inline constexpr float kTwoPi = kPi * 2.0f;

    // 楕円パーティクル軌道
    inline constexpr float kOrbitPeriodSeconds         = 3.0f;
    inline constexpr float kEllipseTangentSpeed        = 0.03f;
    inline constexpr float kEllipseYVelocity           = 0.04f;
    inline constexpr float kEllipseLifetime            = 1.5f;
    inline constexpr float kEllipseBaseScale           = 0.4f;
    inline constexpr float kEllipseScaleRandom         = 0.2f;

    // 白パーティクル散布
    inline constexpr float kWhiteParticleScatterRadius = 20.0f;
    inline constexpr float kWhiteParticleLifetimeMin   = 2.0f;
    inline constexpr float kWhiteParticleLifetimeMax   = 5.0f;
}
