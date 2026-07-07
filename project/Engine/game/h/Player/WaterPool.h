#pragma once
#include "Camera.h"
#include "GameConstants.h"
#include "Sprite.h"
#include "SpriteCommon.h"
#include <memory>
#include <random>
namespace engine::graphics { class ParticleManager; }

namespace engine::game {
using engine::graphics::Camera;
using engine::graphics::Sprite;
using engine::graphics::SpriteCommon;
using engine::graphics::ParticleManager;

class WaterPool {
public:
    void Initialize(SpriteCommon* spriteCommon);
    void Update();
    void Draw(Camera* camera);

    // プレイヤーの入水・出水時に呼ぶ（水面でスプラッシュ発生）
    void EmitSplash(const Vector3& position);

    /** @brief 水面のY座標を返す（Player::SetWaterLevel() に渡して水中判定と同期させる） */
    static constexpr float GetSurfaceY() { return kPoolTop; }

private:
    SpriteCommon*           spriteCommon_ = nullptr;
    ParticleManager*        pm_           = nullptr;

    // 水の本体（暗い深海色）
    std::unique_ptr<Sprite> waterSprite_;
    // 水面付近のグラデーション層（明るい浅瀬色）
    std::unique_ptr<Sprite> waterSpriteTop_;

    // パーティクルタイマー
    int rippleTimer_  = 0;
    int glintTimer_   = 0;
    int causticTimer_ = 0;
    int bubbleTimer_  = 0;

    std::mt19937 rippleRng_;
    std::mt19937 glintRng_;
    std::mt19937 causticRng_;
    std::mt19937 bubbleRng_;
    std::mt19937 splashRng_;

    // プール領域
    static constexpr float kPoolX0     =  3.0f;
    static constexpr float kPoolX1     = 27.0f;
    static constexpr float kPoolTop    =  3.0f;
    static constexpr float kPoolBottom = -0.6f;

    // fovY=0.45, dist=30 のカメラで Z=0 面に映る可視半幅・半高（GameConstants と共有）
    static constexpr float kHalfW = GameConstants::kCameraHalfW;
    static constexpr float kHalfH = GameConstants::kCameraHalfH;
};

} // namespace engine::game
