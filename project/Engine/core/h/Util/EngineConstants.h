/**
 * @file EngineConstants.h
 * @brief エンジン全体で使う、ゲームに依らない定数（フレームレート・画面サイズ・数学定数）をまとめたファイル
 * @note ゲームだけで使う値（カメラの距離など）はゲーム側の GameConstants.h に置く
 */
#pragma once

namespace engine {
namespace engineConstants {

    // フレームレート
    inline constexpr float kTargetFps = 60.0f;
    inline constexpr float kFrameDeltaTime = 1.0f / kTargetFps;

    // 画面サイズ・画面中心
    inline constexpr float kScreenWidth = 1280.0f;
    inline constexpr float kScreenHeight = 720.0f;
    inline constexpr float kScreenCenterX = kScreenWidth * 0.5f;
    inline constexpr float kScreenCenterY = kScreenHeight * 0.5f;

    // 数学定数
    inline constexpr float kPi = 3.14159265358979323846f;
    inline constexpr float kTwoPi = kPi * 2.0f;
    inline constexpr float kHalfPi = kPi / 2.0f;
    inline constexpr float kDegToRad = kPi / 180.0f;

}
} // namespace engine
