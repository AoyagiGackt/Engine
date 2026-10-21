/**
 * @file GameConstants.h
 * @brief ゲーム全体で使う定数をまとめたファイル
 */
#pragma once

namespace engine {
namespace GameConstants {

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

    // カメラ投影半幅・半高（fovY=0.45, dist=24 のカメラで Z=0 面に映る可視半幅・半高）
    inline constexpr float kCameraHalfW = 9.800f;
    inline constexpr float kCameraHalfH = 5.510f;
    // カメラのZ距離（上のkCameraHalfW/Hの前提そのもの。全シーンのSetTranslate/UpdateCameraFollowで共通）
    inline constexpr float kCameraDistanceZ = -24.0f;
    // カメラ追従時、プレイヤーのYより少し上を映すオフセット（頭上の間合いを見せるため）
    inline constexpr float kCameraFollowOffsetY = 3.0f;

}
} // namespace engine
