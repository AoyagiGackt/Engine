#include "WaterPool.h"
#include "ParticleManager.h"
using namespace engine;
using namespace engine::graphics;
using namespace engine::game;

void WaterPool::Initialize(SpriteCommon* spriteCommon)
{
    spriteCommon_ = spriteCommon;
    pm_ = ParticleManager::GetInstance();

    // RNG シード（MSVC はクラス定義内で std::random_device{}() を使えないため初期化はここで行う）
    std::random_device rd;
    rippleRng_  = std::mt19937(rd());
    glintRng_   = std::mt19937(rd());
    causticRng_ = std::mt19937(rd());
    bubbleRng_  = std::mt19937(rd());
    splashRng_  = std::mt19937(rd());

    // --- パーティクルグループ登録 ---
    pm_->CreateParticleGroup("water_ripple",  "Resources/circle2.png");
    pm_->CreateParticleGroup("water_glint",   "Resources/circle2.png");
    pm_->CreateParticleGroup("water_caustic", "Resources/circle2.png");
    pm_->CreateParticleGroup("water_bubble",  "Resources/circle2.png");
    pm_->CreateParticleGroup("water_splash",  "Resources/circle2.png");
    pm_->SetAdditiveBlend("water_ripple",  false);
    pm_->SetAdditiveBlend("water_glint",   true);
    pm_->SetAdditiveBlend("water_caustic", true);
    pm_->SetAdditiveBlend("water_bubble",  false);
    pm_->SetAdditiveBlend("water_splash",  false);

    // --- 本体スプライト（深い部分：暗い青） ---
    waterSprite_ = std::make_unique<Sprite>();
    waterSprite_->Initialize(spriteCommon_, "Resources/white.png");
    waterSprite_->SetColor({ 0.04f, 0.22f, 0.62f, 0.72f });

    // --- 水面グラデーション層（明るいシアン、上部のみ） ---
    waterSpriteTop_ = std::make_unique<Sprite>();
    waterSpriteTop_->Initialize(spriteCommon_, "Resources/white.png");
    waterSpriteTop_->SetColor({ 0.28f, 0.62f, 0.95f, 0.30f });
}

void WaterPool::Update()
{
    // 水面リップル（25フレームごと）
    if (++rippleTimer_ >= 25) {
        rippleTimer_ = 0;
        std::uniform_real_distribution<float> rx(kPoolX0 + 1.5f, kPoolX1 - 1.5f);
        pm_->EmitRing("water_ripple",
            { rx(rippleRng_), kPoolTop, 0.0f },
            0.9f, { 0.55f, 0.82f, 1.0f, 0.55f }, 6, 0.7f, 0.08f);
    }

    // 水面グリント・きらめき（4フレームごと）
    if (++glintTimer_ >= 4) {
        glintTimer_ = 0;
        std::uniform_real_distribution<float> gx(kPoolX0 + 1.0f, kPoolX1 - 1.0f);
        pm_->EmitWithColor("water_glint",
            { gx(glintRng_), kPoolTop, 0.0f },
            { 0.0f, 0.0f, 0.0f },
            { 0.88f, 0.98f, 1.0f, 1.0f },
            0.2f, 0.1f);
    }

    // コースティクス（水中を漂う光の筋、8フレームごと）
    if (++causticTimer_ >= 8) {
        causticTimer_ = 0;
        std::uniform_real_distribution<float> cx(kPoolX0 + 0.5f, kPoolX1 - 0.5f);
        std::uniform_real_distribution<float> cy(kPoolBottom + 0.3f, kPoolTop - 0.7f);
        std::uniform_real_distribution<float> cvx(-0.2f, 0.2f);
        float py = cy(causticRng_);
        pm_->EmitWithColor("water_caustic",
            { cx(causticRng_), py, 0.0f },
            { cvx(causticRng_), 0.4f, 0.0f },
            { 0.8f, 1.0f, 0.88f, 0.4f },
            2.0f, 0.28f);
    }

    // 環境気泡（水底から水面へ、12フレームごと）
    if (++bubbleTimer_ >= 12) {
        bubbleTimer_ = 0;
        std::uniform_real_distribution<float> bx(kPoolX0 + 0.5f, kPoolX1 - 0.5f);
        std::uniform_real_distribution<float> by(kPoolBottom + 0.2f, kPoolTop - 1.0f);
        float py = by(bubbleRng_);
        float life = (kPoolTop - py) / 0.9f;
        pm_->EmitWithColor("water_bubble",
            { bx(bubbleRng_), py, 0.0f },
            { 0.0f, 0.9f, 0.0f },
            { 0.7f, 0.9f, 1.0f, 0.45f },
            life, 0.07f);
    }
}

void WaterPool::Draw(Camera* camera)
{
    const Vector3& cam = camera->GetTranslate();

    float sx0  = (kPoolX0     - cam.x) / kHalfW * 640.0f + 640.0f;
    float sx1  = (kPoolX1     - cam.x) / kHalfW * 640.0f + 640.0f;
    float sy0  = -(kPoolTop    - cam.y) / kHalfH * 360.0f + 360.0f;  // 水面（上端）
    float sy1  = -(kPoolBottom - cam.y) / kHalfH * 360.0f + 360.0f;  // 水底（下端）
    float syMid= -((kPoolTop - 1.5f) - cam.y) / kHalfH * 360.0f + 360.0f; // 水面から1.5u下

    // 本体（深い暗青：全深度）
    waterSprite_->SetPosition({ sx0, sy0 });
    waterSprite_->SetSize({ sx1 - sx0, sy1 - sy0 });
    waterSprite_->Update();
    waterSprite_->Draw();

    // グラデーション層（明るいシアン：水面付近のみ）
    waterSpriteTop_->SetPosition({ sx0, sy0 });
    waterSpriteTop_->SetSize({ sx1 - sx0, syMid - sy0 });
    waterSpriteTop_->Update();
    waterSpriteTop_->Draw();
}

void WaterPool::EmitSplash(const Vector3& position)
{
    Vector3 sp = { position.x, kPoolTop, 0.0f };

    pm_->EmitRing("water_splash",
        sp, 2.2f, { 0.6f, 0.86f, 1.0f, 0.9f }, 8, 0.4f, 0.12f);

    std::uniform_real_distribution<float> vx(-2.5f, 2.5f);
    std::uniform_real_distribution<float> vy( 2.0f, 4.5f);
    for (int i = 0; i < 5; ++i) {
        pm_->EmitGravity("water_splash",
            sp, { vx(splashRng_), vy(splashRng_), 0.0f },
            { 0.55f, 0.82f, 1.0f, 0.9f }, 0.5f, 0.1f);
    }
}
