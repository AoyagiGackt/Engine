/**
 * @file WaterPool.h
 * @brief ステージ内の水場（見た目・水面パーティクル・入水スプラッシュ）を管理するファイル
 */
#pragma once
#include "Camera.h"
#include "GameConstants.h"
#include "WaterSurfaceEffect.h"
#include "SpriteCommon.h"
#include <array>
#include <random>
#include <string>
namespace engine::graphics {
class ParticleManager;
}

namespace engine::game {
using engine::graphics::Camera;
using engine::graphics::ParticleManager;

using engine::graphics::SpriteCommon;

/**
 * @brief 固定領域の水場を描画し、波紋・きらめき・水しぶきパーティクルを管理するクラス
 * @note 水面のY座標は GetSurfaceY() で公開し、Player::SetWaterLevel() と同期させる
 */
class WaterPool {
public:
    void Initialize(SpriteCommon* spriteCommon);
    /** @brief プレイヤーの移動から波を励起して点列を積分する */
    void Update(const Vector3& playerPosition, bool movementEnabled);
    /** @brief 水より奥の背景を描く前に描画先を準備する */
    void BeginBackground(bool targetsBackBuffer);
    /** @brief 水場の横方向の範囲に含まれるかを返す */
    static bool ContainsX(float worldX);
    /** @brief 水域の外では水中判定が成立しない高さを返す */
    static float PlayerWaterLevel(float worldX);
    void Draw(Camera* camera);

    /**
     * @brief 入水・出水時に呼ぶ水面のY位置で水しぶきパーティクルを発生させる
     * @param position スプラッシュを出すワールド座標（Y以外は使用位置、Yは水面高さへ補正される）
     */
    void EmitSplash(const Vector3& position);

    /** @brief 水面のY座標を返す（Player::SetWaterLevel() に渡して水中判定と同期させる） */
    static float GetSurfaceY();

    /** @brief 指定グループへ水しぶきの輪と水滴を共通設定で発生させる */
    static void EmitSplashParticles(ParticleManager* particles, const std::string& group,
        const Vector3& position, std::mt19937& rng);

private:
    SpriteCommon* spriteCommon_ = nullptr;
    ParticleManager* pm_ = nullptr;

    // 波形と描画資源は初期化時に確保して再利用する。
    engine::graphics::WaterSurfaceEffect waterEffect_;
    std::array<float, engine::graphics::WaterSurfaceEffect::kMaxSamples> heights_{};
    std::array<float, engine::graphics::WaterSurfaceEffect::kMaxSamples> velocities_{};
    size_t pointCount_{};
    Vector3 previousPlayerPosition_{};
    bool hasPreviousPlayer_ = false;
    float time_{};

    /** @brief 周囲の点へ距離に応じて速度を加える */
    void AddImpulse(float worldX, float strength);
    /** @brief 点列を補間して実際の水面高さを返す */
    float SurfaceAt(float worldX) const;

    // パーティクルタイマー
    int rippleTimer_ = 0;
    int glintTimer_ = 0;
    int bubbleTimer_ = 0;

    std::mt19937 rippleRng_;
    std::mt19937 glintRng_;
    std::mt19937 bubbleRng_;
    std::mt19937 splashRng_;

    // fovY=0.45, dist=24 のカメラで Z=0 面に映る可視半幅・半高（GameConstants と共有）
    static constexpr float kHalfW = gameConstants::kCameraHalfW;
    static constexpr float kHalfH = gameConstants::kCameraHalfH;
};

} // namespace engine::game
