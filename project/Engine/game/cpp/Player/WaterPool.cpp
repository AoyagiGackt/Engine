/**
 * @file WaterPool.cpp
 * @brief WaterPoolのプレイヤーの操作、戦闘、状態遷移に関する具体的な処理を実装するファイル
 */
#include "WaterPool.h"
#include "ParticleManager.h"
using namespace engine;
using namespace engine::graphics;
using namespace engine::game;

namespace {
// 水の色
constexpr Vector4 kDeepWaterColor = { 0.03f, 0.18f, 0.58f, 0.94f };
constexpr Vector4 kSurfaceWaterColor = { 0.20f, 0.58f, 0.96f, 0.68f };
constexpr float kSurfaceLayerDepth = 1.5f; // 水面から明るい層を重ねる深さ

// 水面リップル
constexpr int kRippleIntervalFrames = 25;
constexpr float kRippleEdgeMargin = 1.5f;
constexpr float kRippleSpeed = 0.9f;
constexpr Vector4 kRippleColor = { 0.55f, 0.82f, 1.0f, 0.55f };
constexpr int kRippleCount = 6;
constexpr float kRippleLifetime = 0.7f;
constexpr float kRippleSize = 0.08f;

// 水面のきらめき
constexpr int kGlintIntervalFrames = 4;
constexpr float kGlintEdgeMargin = 1.0f;
constexpr Vector4 kGlintColor = { 0.88f, 0.98f, 1.0f, 1.0f };
constexpr float kGlintLifetime = 0.2f;
constexpr float kGlintSize = 0.1f;

// 水中を漂う光の筋
constexpr int kCausticIntervalFrames = 8;
constexpr float kCausticEdgeMargin = 0.5f;
constexpr float kCausticBottomMargin = 0.3f;
constexpr float kCausticTopMargin = 0.7f;
constexpr float kCausticDriftX = 0.2f;
constexpr float kCausticRiseSpeed = 0.4f;
constexpr Vector4 kCausticColor = { 0.8f, 1.0f, 0.88f, 0.4f };
constexpr float kCausticLifetime = 2.0f;
constexpr float kCausticSize = 0.28f;

// 水底から上る気泡
constexpr int kBubbleIntervalFrames = 12;
constexpr float kBubbleEdgeMargin = 0.5f;
constexpr float kBubbleBottomMargin = 0.2f;
constexpr float kBubbleTopMargin = 1.0f;
constexpr float kBubbleRiseSpeed = 0.9f;
constexpr Vector4 kBubbleColor = { 0.7f, 0.9f, 1.0f, 0.45f };
constexpr float kBubbleSize = 0.07f;

// 着水しぶき
constexpr float kSplashRingSpeed = 2.2f;
constexpr Vector4 kSplashRingColor = { 0.6f, 0.86f, 1.0f, 0.9f };
constexpr int kSplashRingCount = 8;
constexpr float kSplashRingLifetime = 0.4f;
constexpr float kSplashRingSize = 0.12f;
constexpr float kSplashDropSpreadX = 2.5f;
constexpr float kSplashDropRiseMin = 2.0f;
constexpr float kSplashDropRiseMax = 4.5f;
constexpr int kSplashDropCount = 5;
constexpr Vector4 kSplashDropColor = { 0.55f, 0.82f, 1.0f, 0.9f };
constexpr float kSplashDropLifetime = 0.5f;
constexpr float kSplashDropSize = 0.1f;
}

void WaterPool::Initialize(SpriteCommon* spriteCommon)
{
    spriteCommon_ = spriteCommon;
    pm_ = ParticleManager::GetInstance();

    // RNG シード（MSVC はクラス定義内で std::random_device{}() を使えないため初期化はここで行う）
    std::random_device rd;
    rippleRng_ = std::mt19937(rd());
    glintRng_ = std::mt19937(rd());
    causticRng_ = std::mt19937(rd());
    bubbleRng_ = std::mt19937(rd());
    splashRng_ = std::mt19937(rd());

    // パーティクルグループ登録
    pm_->CreateParticleGroup("water_ripple", "Resources/Effects/circle2.png");
    pm_->CreateParticleGroup("water_glint", "Resources/Effects/circle2.png");
    pm_->CreateParticleGroup("water_caustic", "Resources/Effects/circle2.png");
    pm_->CreateParticleGroup("water_bubble", "Resources/Effects/circle2.png");
    pm_->CreateParticleGroup("water_splash", "Resources/Effects/circle2.png");
    pm_->SetAdditiveBlend("water_ripple", false);
    pm_->SetAdditiveBlend("water_glint", true);
    pm_->SetAdditiveBlend("water_caustic", true);
    pm_->SetAdditiveBlend("water_bubble", false);
    pm_->SetAdditiveBlend("water_splash", false);

    // 本体スプライト（深い部分 暗い青）
    waterSprite_ = std::make_unique<Sprite>();
    waterSprite_->Initialize(spriteCommon_, "Resources/white.png");
    waterSprite_->SetColor(kDeepWaterColor);

    // 水面グラデーション層（明るいシアン、上部のみ）
    waterSpriteTop_ = std::make_unique<Sprite>();
    waterSpriteTop_->Initialize(spriteCommon_, "Resources/white.png");
    waterSpriteTop_->SetColor(kSurfaceWaterColor);
}

void WaterPool::Update()
{
    // 水面リップル
    if (++rippleTimer_ >= kRippleIntervalFrames) {
        rippleTimer_ = 0;
        std::uniform_real_distribution<float> rx(kPoolX0 + kRippleEdgeMargin, kPoolX1 - kRippleEdgeMargin);
        pm_->EmitRing("water_ripple",
            { rx(rippleRng_), kPoolTop, 0.0f },
            kRippleSpeed, kRippleColor, kRippleCount, kRippleLifetime, kRippleSize);
    }

    // 水面グリント・きらめき
    if (++glintTimer_ >= kGlintIntervalFrames) {
        glintTimer_ = 0;
        std::uniform_real_distribution<float> gx(kPoolX0 + kGlintEdgeMargin, kPoolX1 - kGlintEdgeMargin);
        pm_->EmitWithColor("water_glint",
            { gx(glintRng_), kPoolTop, 0.0f },
            { 0.0f, 0.0f, 0.0f },
            kGlintColor,
            kGlintLifetime, kGlintSize);
    }

    // コースティクス（水中を漂う光の筋）
    if (++causticTimer_ >= kCausticIntervalFrames) {
        causticTimer_ = 0;
        std::uniform_real_distribution<float> cx(kPoolX0 + kCausticEdgeMargin, kPoolX1 - kCausticEdgeMargin);
        std::uniform_real_distribution<float> cy(kPoolBottom + kCausticBottomMargin, kPoolTop - kCausticTopMargin);
        std::uniform_real_distribution<float> cvx(-kCausticDriftX, kCausticDriftX);
        float py = cy(causticRng_);
        pm_->EmitWithColor("water_caustic",
            { cx(causticRng_), py, 0.0f },
            { cvx(causticRng_), kCausticRiseSpeed, 0.0f },
            kCausticColor,
            kCausticLifetime, kCausticSize);
    }

    // 環境気泡（水底から水面へ）
    if (++bubbleTimer_ >= kBubbleIntervalFrames) {
        bubbleTimer_ = 0;
        std::uniform_real_distribution<float> bx(kPoolX0 + kBubbleEdgeMargin, kPoolX1 - kBubbleEdgeMargin);
        std::uniform_real_distribution<float> by(kPoolBottom + kBubbleBottomMargin, kPoolTop - kBubbleTopMargin);
        float py = by(bubbleRng_);
        float life = (kPoolTop - py) / kBubbleRiseSpeed; // ちょうど水面で消える寿命
        pm_->EmitWithColor("water_bubble",
            { bx(bubbleRng_), py, 0.0f },
            { 0.0f, kBubbleRiseSpeed, 0.0f },
            kBubbleColor,
            life, kBubbleSize);
    }
}

void WaterPool::Draw(Camera* camera)
{
    const Vector3& cam = camera->GetTranslate();
    const float centerX = GameConstants::kScreenCenterX;
    const float centerY = GameConstants::kScreenCenterY;

    float sx0 = (kPoolX0 - cam.x) / kHalfW * centerX + centerX;
    float sx1 = (kPoolX1 - cam.x) / kHalfW * centerX + centerX;
    float sy0 = -(kPoolTop - cam.y) / kHalfH * centerY + centerY; // 水面（上端）
    float sy1 = -(kPoolBottom - cam.y) / kHalfH * centerY + centerY; // 水底（下端）
    float syMid = -((kPoolTop - kSurfaceLayerDepth) - cam.y) / kHalfH * centerY + centerY; // 明るい層の下端

    // 本体（深い暗青 全深度）
    waterSprite_->SetPosition({ sx0, sy0 });
    waterSprite_->SetSize({ sx1 - sx0, sy1 - sy0 });
    waterSprite_->Update();
    waterSprite_->Draw();

    // グラデーション層（明るいシアン 水面付近のみ）
    waterSpriteTop_->SetPosition({ sx0, sy0 });
    waterSpriteTop_->SetSize({ sx1 - sx0, syMid - sy0 });
    waterSpriteTop_->Update();
    waterSpriteTop_->Draw();
}

void WaterPool::EmitSplash(const Vector3& position)
{
    Vector3 sp = { position.x, kPoolTop, 0.0f };

    pm_->EmitRing("water_splash",
        sp, kSplashRingSpeed, kSplashRingColor, kSplashRingCount, kSplashRingLifetime, kSplashRingSize);

    std::uniform_real_distribution<float> vx(-kSplashDropSpreadX, kSplashDropSpreadX);
    std::uniform_real_distribution<float> vy(kSplashDropRiseMin, kSplashDropRiseMax);
    for (int i = 0; i < kSplashDropCount; ++i) {
        pm_->EmitGravity("water_splash",
            sp, { vx(splashRng_), vy(splashRng_), 0.0f },
            kSplashDropColor, kSplashDropLifetime, kSplashDropSize);
    }
}
