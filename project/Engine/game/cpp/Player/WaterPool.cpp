/**
 * @file WaterPool.cpp
 * @brief 水場の水面の表示（WaterPool）の実装
 */
#include "WaterPool.h"
#include "CombatTuning.h"
#include <algorithm>
#include "SceneVisualTuning.h"
#include "ParticleManager.h"
using namespace engine::graphics;

namespace engine::game {
namespace {
/** @brief 環境演出のフレーム間隔を進め、発生時だけタイマーをリセットする */
bool IsEmissionDue(int& timer, int intervalFrames)
{
    if (++timer < intervalFrames)
    {
        return false;
    }
    timer = 0;
    return true;
}

/** @brief 範囲の順序を揃え、設定値を変更しても有効な乱数分布を作る */
std::uniform_real_distribution<float> RandomRange(float minimum, float maximum)
{
    return std::uniform_real_distribution<float>((std::min)(minimum, maximum), (std::max)(minimum, maximum));
}
}

/** @brief 水面のスプライトを用意する */
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

    /** @brief 水場の粒子グループ名と加算合成の有効指定 */
    struct WaterParticleGroup {
        const char* name;
        bool additive;
    };
    constexpr WaterParticleGroup kGroups[] = {
        { "water_ripple", false }, { "water_glint", true }, { "water_caustic", true },
        { "water_bubble", false }, { "water_splash", false },
    };
    // テクスチャの登録と合成方式は同じ定義表に従って揃える。
    for (const auto& group : kGroups)
    {
        pm_->CreateParticleGroup(group.name, "Resources/Effects/circle2.png");
        pm_->SetAdditiveBlend(group.name, group.additive);
    }

    // 本体スプライト（深い部分 暗い青）
    waterSprite_ = std::make_unique<Sprite>();
    waterSprite_->Initialize(spriteCommon_, "Resources/white.png");
    waterSprite_->SetColor(SceneVisualTuning::Water().deepWaterColor);

    // 水面グラデーション層（明るいシアン、上部のみ）
    waterSpriteTop_ = std::make_unique<Sprite>();
    waterSpriteTop_->Initialize(spriteCommon_, "Resources/white.png");
    waterSpriteTop_->SetColor(SceneVisualTuning::Water().surfaceWaterColor);
}

/** @brief 水面のさざ波を一定間隔で出す */
void WaterPool::Update()
{
    // 水面リップル
    if (IsEmissionDue(rippleTimer_, SceneVisualTuning::Water().rippleIntervalFrames))
    {
        auto rx = RandomRange(CombatTune().waterMinX + SceneVisualTuning::Water().rippleEdgeMargin, CombatTune().waterMaxX - SceneVisualTuning::Water().rippleEdgeMargin);
        pm_->EmitRing("water_ripple",
            { rx(rippleRng_), CombatTune().waterSurfaceY, 0.0f },
            SceneVisualTuning::Water().rippleSpeed, SceneVisualTuning::Water().rippleColor, SceneVisualTuning::Water().rippleCount, SceneVisualTuning::Water().rippleLifetime, SceneVisualTuning::Water().rippleSize);
    }

    // 水面グリント・きらめき
    if (IsEmissionDue(glintTimer_, SceneVisualTuning::Water().glintIntervalFrames))
    {
        auto gx = RandomRange(CombatTune().waterMinX + SceneVisualTuning::Water().glintEdgeMargin, CombatTune().waterMaxX - SceneVisualTuning::Water().glintEdgeMargin);
        pm_->EmitWithColor("water_glint",
            { gx(glintRng_), CombatTune().waterSurfaceY, 0.0f },
            { 0.0f, 0.0f, 0.0f },
            SceneVisualTuning::Water().glintColor,
            SceneVisualTuning::Water().glintLifetime, SceneVisualTuning::Water().glintSize);
    }

    // コースティクス（水中を漂う光の筋）
    if (IsEmissionDue(causticTimer_, SceneVisualTuning::Water().causticIntervalFrames))
    {
        auto cx = RandomRange(CombatTune().waterMinX + SceneVisualTuning::Water().causticEdgeMargin, CombatTune().waterMaxX - SceneVisualTuning::Water().causticEdgeMargin);
        auto cy = RandomRange(CombatTune().waterBottomY + SceneVisualTuning::Water().causticBottomMargin, CombatTune().waterSurfaceY - SceneVisualTuning::Water().causticTopMargin);
        auto cvx = RandomRange(-SceneVisualTuning::Water().causticDriftX, SceneVisualTuning::Water().causticDriftX);
        float py = cy(causticRng_);
        pm_->EmitWithColor("water_caustic",
            { cx(causticRng_), py, 0.0f },
            { cvx(causticRng_), SceneVisualTuning::Water().causticRiseSpeed, 0.0f },
            SceneVisualTuning::Water().causticColor,
            SceneVisualTuning::Water().causticLifetime, SceneVisualTuning::Water().causticSize);
    }

    // 環境気泡（水底から水面へ）
    if (IsEmissionDue(bubbleTimer_, SceneVisualTuning::Water().bubbleIntervalFrames))
    {
        auto bx = RandomRange(CombatTune().waterMinX + SceneVisualTuning::Water().bubbleEdgeMargin, CombatTune().waterMaxX - SceneVisualTuning::Water().bubbleEdgeMargin);
        auto by = RandomRange(CombatTune().waterBottomY + SceneVisualTuning::Water().bubbleBottomMargin, CombatTune().waterSurfaceY - SceneVisualTuning::Water().bubbleTopMargin);
        float py = by(bubbleRng_);
        float life = (CombatTune().waterSurfaceY - py) / SceneVisualTuning::Water().bubbleRiseSpeed; // ちょうど水面で消える寿命
        pm_->EmitWithColor("water_bubble",
            { bx(bubbleRng_), py, 0.0f },
            { 0.0f, SceneVisualTuning::Water().bubbleRiseSpeed, 0.0f },
            SceneVisualTuning::Water().bubbleColor,
            life, SceneVisualTuning::Water().bubbleSize);
    }
}

/** @brief 水面を描画する */
void WaterPool::Draw(Camera* camera)
{
    const Vector3& cam = camera->GetTranslate();
    const float centerX = GameConstants::kScreenCenterX;
    const float centerY = GameConstants::kScreenCenterY;

    float sx0 = (CombatTune().waterMinX - cam.x) / kHalfW * centerX + centerX;
    float sx1 = (CombatTune().waterMaxX - cam.x) / kHalfW * centerX + centerX;
    float sy0 = -(CombatTune().waterSurfaceY - cam.y) / kHalfH * centerY + centerY; // 水面（上端）
    float sy1 = -(CombatTune().waterBottomY - cam.y) / kHalfH * centerY + centerY; // 水底（下端）
    float syMid = -((CombatTune().waterSurfaceY - SceneVisualTuning::Water().surfaceLayerDepth) - cam.y) / kHalfH * centerY + centerY; // 明るい層の下端

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

/** @brief 入水・出水時に呼ぶ水面のY位置で水しぶきパーティクルを発生させる */
void WaterPool::EmitSplash(const Vector3& position)
{
    EmitSplashParticles(pm_, "water_splash", { position.x, CombatTune().waterSurfaceY, 0.0f }, splashRng_);
}

/** @brief 水中判定と描画で共有する水面高さを設定から取得する */
float WaterPool::GetSurfaceY()
{
    return CombatTune().waterSurfaceY;
}

/** @brief 水面の輪と重力で落ちる水滴を同じ手順で発生させる */
void WaterPool::EmitSplashParticles(ParticleManager* particles, const std::string& group,
    const Vector3& position, std::mt19937& rng)
{
    const auto& style = SceneVisualTuning::Water();
    particles->EmitRing(group, position, style.splashRingSpeed, style.splashRingColor,
        style.splashRingCount, style.splashRingLifetime, style.splashRingSize);
    auto velocityX = RandomRange(-style.splashDropSpreadX, style.splashDropSpreadX);
    auto velocityY = RandomRange(style.splashDropRiseMin, style.splashDropRiseMax);
    for (int i = 0; i < style.splashDropCount; ++i)
    {
        particles->EmitGravity(group, position, { velocityX(rng), velocityY(rng), 0.0f },
            style.splashDropColor, style.splashDropLifetime, style.splashDropSize);
    }
}

} // namespace engine::game
