/**
 * @file GameConstants.h
 * @brief ゲーム全体で使う定数をまとめたファイル
 */
#pragma once

namespace engine {
namespace GameConstants {

    // フレームレート
    inline constexpr float kTargetFps      = 60.0f;
    inline constexpr float kFrameDeltaTime = 1.0f / kTargetFps;

    // 画面中心（1280×720）
    inline constexpr float kScreenCenterX = 640.0f;
    inline constexpr float kScreenCenterY = 360.0f;

    // 数学定数
    inline constexpr float kPi      = 3.14159265358979323846f;
    inline constexpr float kTwoPi   = kPi * 2.0f;
    inline constexpr float kHalfPi  = kPi / 2.0f;
    inline constexpr float kDegToRad = kPi / 180.0f;

    // カメラ投影半幅・半高
    inline constexpr float kCameraHalfW = 12.25f;
    inline constexpr float kCameraHalfH =  6.888f;

    // ヒットストップ（フレーム数）
    inline constexpr int kHitStopLaunch = 8;   // 打ち上げ
    inline constexpr int kHitStopJuggle = 4;   // ジャグル
    inline constexpr int kHitStopFinish = 12;  // フィニッシュ

    // カメラシェイク
    inline constexpr float kShakeLaunchAmt = 0.35f;
    inline constexpr float kShakeLaunchDur = 0.28f;
    inline constexpr float kShakeFinishAmt = 0.55f;
    inline constexpr float kShakeFinishDur = 0.40f;

    // 打ち上げ速度
    inline constexpr float kLaunchSpeed = 0.48f;
}
} // namespace engine
