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

    // ヒットストップ（フレーム数）
    inline constexpr int kHitStopLaunch = 8; // 打ち上げ
    inline constexpr int kHitStopJuggle = 4; // ジャグル
    inline constexpr int kHitStopFinish = 12; // フィニッシュ
    inline constexpr int kHitStopFinisherSlash = 20; // 大技 本命ヒット
    inline constexpr int kHitStopFinisherBeat = 1; // 大技 斬撃線が1本出るごとの小停止

    // カメラシェイク
    inline constexpr float kShakeLaunchAmt = 0.35f;
    inline constexpr float kShakeLaunchDur = 0.28f;
    inline constexpr float kShakeFinishAmt = 0.55f;
    inline constexpr float kShakeFinishDur = 0.40f;
    inline constexpr float kShakeFinisherSlashAmt = 0.65f;
    inline constexpr float kShakeFinisherSlashDur = 0.45f;

    // 打ち上げ速度
    inline constexpr float kLaunchSpeed = 0.48f;

    // 近接攻撃・固有技の指向性判定（BattleTestScene/GamePlayScene共通）
    inline constexpr float kSkillRearReachMult = 0.4f; // 背面リーチ（前方リーチ比）

    // スタイルランク（D/C/B/A/S/SS/SSS）のしきい値（styleMeter_: 0.0〜1.0に対する各ランクの下限値、昇順）
    // GamePlayScene側のランク表示(DrawRankAndAwakenGauge)とランクアップ演出(EmitStyleRankUpParticles)の
    // 両方から参照し、二重管理で閾値がずれないようにする
    inline constexpr int kStyleRankCount = 7;
    inline constexpr float kStyleRankThresholds[kStyleRankCount] = {
        0.00f, // D
        0.05f, // C
        0.15f, // B
        0.30f, // A
        0.50f, // S
        0.70f, // SS
        0.90f, // SSS
    };

    // 覚醒ゲージ満タン消費の大技（溜め→高速連続斬撃で刻む→一斉解放）
    inline constexpr int kFinisherLineDamage = 1; // 斬撃線1本ごとのダメージ
    inline constexpr int kFinisherSlashDamage = 8; // 本命（解放の一撃）の固定ダメージ
    inline constexpr int kFinisherSlashLines = 16; // 斬撃線の本数
    inline constexpr float kFinisherSlashRadius = 4.5f; // 斬撃線の最大半長
    inline constexpr float kFinisherChargeDelay = 0.30f; // 発動から最初の斬撃までの溜め（秒）
    inline constexpr float kFinisherLineInterval = 0.045f; // 斬撃線の出現間隔（秒）
    inline constexpr float kFinisherImpactDelay = 0.35f; // 最後の斬撃線から解放までの溜め（秒）
    inline constexpr float kFinisherOverlayAlpha = 0.60f; // 演出中の画面暗転の濃さ
}
} // namespace engine
