/**
 * @file WaterPool.cpp
 * @brief 水場の水面の表示（WaterPool）の実装
 */
#include "WaterPool.h"
#include "CombatTuning.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include "SrvManager.h"
#include "SceneVisualTuning.h"
#include "ParticleManager.h"
using namespace engine::graphics;

namespace engine::game {
namespace {
// 点列の積分と配列参照に用いる単位値。
constexpr size_t kFirst = 0;
constexpr size_t kNext = 1;
constexpr int kSubsteps = 4;
constexpr float kZero = 0.0f;
constexpr float kOne = 1.0f;
constexpr float kTwo = 2.0f;
constexpr float kMinWidth = 0.001f;
constexpr float kMaxPlayerDelta = 0.5f;
constexpr float kMaxWaveSpeed = 4.0f;
constexpr float kMovementThreshold = 0.0001f;
/** @brief 環境演出のフレーム間隔を進め、発生時だけタイマーをリセットする */
bool IsEmissionDue(int& timer, int intervalFrames) {
    if (++timer < intervalFrames) {
        return false;
    }
    timer = 0;
    return true;
}

/** @brief 範囲の順序を揃え、設定値を変更しても有効な乱数分布を作る */
std::uniform_real_distribution<float> RandomRange(float minimum, float maximum) {
    return std::uniform_real_distribution<float>((std::min)(minimum, maximum), (std::max)(minimum, maximum));
}
}

/** @brief 水面の描画資源と環境粒子を用意する */
void WaterPool::Initialize(SpriteCommon* spriteCommon) {
    spriteCommon_ = spriteCommon;
    pm_ = ParticleManager::GetInstance();

    // RNG シード（MSVC はクラス定義内で std::random_device{}() を使えないため初期化はここで行う）
    std::random_device rd;
    rippleRng_ = std::mt19937(rd());
    glintRng_ = std::mt19937(rd());
    bubbleRng_ = std::mt19937(rd());
    splashRng_ = std::mt19937(rd());

    /** @brief 水場の粒子グループ名と加算合成の有効指定 */
    struct WaterParticleGroup {
        const char* name;
        bool additive;
    };
    constexpr WaterParticleGroup kGroups[] = {
        { "water_ripple", false }, { "water_glint", true },
        { "water_bubble", false }, { "water_splash", false },
    };
    // テクスチャの登録と合成方式は同じ定義表に従って揃える。
    for (const auto& group : kGroups) {
        pm_->CreateParticleGroup(group.name, "Resources/Effects/circle2.png");
        pm_->SetAdditiveBlend(group.name, group.additive);
    }

    // GPU資源と固定長の波形をこの時点で用意する。
    waterEffect_.Initialize(spriteCommon_->GetDxCommon(), SrvManager::GetInstance());
    pointCount_ = static_cast<size_t>(SceneVisualTuning::Water().wavePointCount);
    heights_.fill(kZero);
    velocities_.fill(kZero);
    hasPreviousPlayer_ = false;
    time_ = kZero;
}

/** @brief 水面のさざ波を一定間隔で出す */
void WaterPool::Update(const Vector3& playerPosition, bool movementEnabled) {
    const auto& style = SceneVisualTuning::Water();
    constexpr float kStep = gameConstants::kFrameDeltaTime / static_cast<float>(kSubsteps);
    time_ += gameConstants::kFrameDeltaTime;
    // 停止中やテレポート時の差分が強い波にならないよう移動量を制限する。
    if (hasPreviousPlayer_ && movementEnabled && ContainsX(playerPosition.x) && playerPosition.y < GetSurfaceY()) {
        const float deltaX = std::clamp(playerPosition.x - previousPlayerPosition_.x, -kMaxPlayerDelta, kMaxPlayerDelta);
        const float deltaY = std::clamp(playerPosition.y - previousPlayerPosition_.y, -kMaxPlayerDelta, kMaxPlayerDelta);
        if (std::abs(deltaX) + std::abs(deltaY) > kMovementThreshold) {
            AddImpulse(playerPosition.x, -(std::abs(deltaX) + std::abs(deltaY)) * style.waveMoveImpulse);
        }
    }
    previousPlayerPosition_ = playerPosition;
    hasPreviousPlayer_ = true;
    // 近傍の高さは同じ時刻の値を参照し、速度と高さを分けて更新する。
    const float spacing = (std::max)(CombatTune().waterMaxX - CombatTune().waterMinX, kMinWidth) / static_cast<float>(pointCount_ - kNext);
    for (int substep = kFirst; substep < kSubsteps; ++substep) {
        for (size_t index = kNext; index + kNext < pointCount_; ++index) {
            const float worldX = CombatTune().waterMinX + static_cast<float>(index) * spacing;
            const float adjacent = heights_[index - kNext] + heights_[index + kNext] - kTwo * heights_[index];
            const float ambient = std::sin(worldX * style.waveAmbientFrequency + time_ * style.waveAmbientSpeed) * style.waveAmbientStrength;
            const float acceleration = -style.waveSpring * heights_[index] + style.waveSpread * adjacent - style.waveDamping * velocities_[index] + ambient;
            velocities_[index] = std::clamp(velocities_[index] + acceleration * kStep, -kMaxWaveSpeed, kMaxWaveSpeed);
        }
        for (size_t index = kNext; index + kNext < pointCount_; ++index) {
            const float nextHeight = heights_[index] + velocities_[index] * kStep;
            heights_[index] = std::clamp(nextHeight, -style.waveMaxHeight, style.waveMaxHeight);
            if (nextHeight != heights_[index]) {
                velocities_[index] = kZero;
            }
        }
    }
    // 水面リップル
    if (IsEmissionDue(rippleTimer_, SceneVisualTuning::Water().rippleIntervalFrames)) {
        auto rx = RandomRange(CombatTune().waterMinX + SceneVisualTuning::Water().rippleEdgeMargin, CombatTune().waterMaxX - SceneVisualTuning::Water().rippleEdgeMargin);
        const float rippleX = rx(rippleRng_);
        pm_->EmitRing("water_ripple",
            { rippleX, SurfaceAt(rippleX), kZero },
            SceneVisualTuning::Water().rippleSpeed, SceneVisualTuning::Water().rippleColor, SceneVisualTuning::Water().rippleCount, SceneVisualTuning::Water().rippleLifetime, SceneVisualTuning::Water().rippleSize);
    }

    // 水面グリント・きらめき
    if (IsEmissionDue(glintTimer_, SceneVisualTuning::Water().glintIntervalFrames)) {
        auto gx = RandomRange(CombatTune().waterMinX + SceneVisualTuning::Water().glintEdgeMargin, CombatTune().waterMaxX - SceneVisualTuning::Water().glintEdgeMargin);
        const float glintX = gx(glintRng_);
        pm_->EmitWithColor("water_glint",
            { glintX, SurfaceAt(glintX), kZero },
            { 0.0f, 0.0f, 0.0f },
            SceneVisualTuning::Water().glintColor,
            SceneVisualTuning::Water().glintLifetime, SceneVisualTuning::Water().glintSize);
    }

    // 環境気泡（水底から水面へ）
    if (IsEmissionDue(bubbleTimer_, SceneVisualTuning::Water().bubbleIntervalFrames)) {
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

/** @brief 背景の描画先を水の合成経路に合わせて準備する */
void WaterPool::BeginBackground(bool targetsBackBuffer) {
    waterEffect_.BeginBackground(targetsBackBuffer);
}

/** @brief 点列とカメラを渡して背景に水を合成する */
void WaterPool::Draw(Camera* camera) {
    const auto& style = SceneVisualTuning::Water();
    const auto& cam = camera->GetTranslate();
    WaterSurfaceEffect::Parameters parameters{};
    parameters.bounds = { CombatTune().waterMinX, CombatTune().waterMaxX, GetSurfaceY(), CombatTune().waterBottomY };
    parameters.view = { cam.x, cam.y, kHalfW, kHalfH };
    parameters.animation = { time_, static_cast<float>(pointCount_), style.refractionStrength, style.refractionSpeed };
    parameters.optical = { style.causticScale, style.causticFlowSpeed, style.causticStrength, style.foamWidth };
    parameters.deepColor = style.deepWaterColor;
    parameters.surfaceColor = style.surfaceWaterColor;
    parameters.detail = { style.surfaceLayerDepth, style.foamStrength, kZero, kZero };
    for (size_t index = kFirst; index < pointCount_; ++index) {
        parameters.heights[index].x = heights_[index];
    }
    waterEffect_.Update(parameters);
    waterEffect_.Draw();
}

/** @brief 水域の横幅にプレイヤーが含まれるかを調べる */
bool WaterPool::ContainsX(float worldX) {
    return worldX >= CombatTune().waterMinX && worldX <= CombatTune().waterMaxX;
}

/** @brief 横幅の外ではプレイヤーの水中判定を無効にする */
float WaterPool::PlayerWaterLevel(float worldX) {
    return ContainsX(worldX) ? GetSurfaceY() : -(std::numeric_limits<float>::max)();
}

/** @brief 距離で減衰する衝撃を固定端以外の点へ加える */
void WaterPool::AddImpulse(float worldX, float strength) {
    const auto& style = SceneVisualTuning::Water();
    const float radius = style.waveImpulseRadius;
    const float spacing = (std::max)(CombatTune().waterMaxX - CombatTune().waterMinX, kMinWidth) / static_cast<float>(pointCount_ - kNext);
    for (size_t index = kNext; index + kNext < pointCount_; ++index) {
        const float distance = std::abs(CombatTune().waterMinX + static_cast<float>(index) * spacing - worldX);
        const float weight = (std::max)(kZero, kOne - distance / radius);
        velocities_[index] = std::clamp(velocities_[index] + strength * weight, -kMaxWaveSpeed, kMaxWaveSpeed);
    }
}

/** @brief 粒子の発生位置を隣り合う水面の点から補間する */
float WaterPool::SurfaceAt(float worldX) const {
    const float coordinate = std::clamp((worldX - CombatTune().waterMinX) /
        (std::max)(CombatTune().waterMaxX - CombatTune().waterMinX, kMinWidth), kZero, kOne) * static_cast<float>(pointCount_ - kNext);
    const size_t index = (std::min)(static_cast<size_t>(coordinate), pointCount_ - kNext - kNext);
    const float ratio = coordinate - static_cast<float>(index);
    return GetSurfaceY() + heights_[index] * (kOne - ratio) + heights_[index + kNext] * ratio;
}

/** @brief 入水・出水時に呼ぶ水面のY位置で水しぶきパーティクルを発生させる */
void WaterPool::EmitSplash(const Vector3& position) {
    const auto& style = SceneVisualTuning::Water();
    if (position.x < CombatTune().waterMinX - style.waveImpulseRadius || position.x > CombatTune().waterMaxX + style.waveImpulseRadius) {
        return;
    }
    const float splashX = std::clamp(position.x, CombatTune().waterMinX, CombatTune().waterMaxX);
    AddImpulse(splashX, -style.waveSplashImpulse);
    EmitSplashParticles(pm_, "water_splash", { splashX, SurfaceAt(splashX), kZero }, splashRng_);
}

/** @brief 水中判定と描画で共有する水面高さを設定から取得する */
float WaterPool::GetSurfaceY() {
    return CombatTune().waterSurfaceY;
}

/** @brief 水面の輪と重力で落ちる水滴を同じ手順で発生させる */
void WaterPool::EmitSplashParticles(ParticleManager* particles, const std::string& group,
    const Vector3& position, std::mt19937& rng) {
    const auto& style = SceneVisualTuning::Water();
    particles->EmitRing(group, position, style.splashRingSpeed, style.splashRingColor,
        style.splashRingCount, style.splashRingLifetime, style.splashRingSize);
    auto velocityX = RandomRange(-style.splashDropSpreadX, style.splashDropSpreadX);
    auto velocityY = RandomRange(style.splashDropRiseMin, style.splashDropRiseMax);
    for (int i = 0; i < style.splashDropCount; ++i) {
        particles->EmitGravity(group, position, { velocityX(rng), velocityY(rng), 0.0f },
            style.splashDropColor, style.splashDropLifetime, style.splashDropSize);
    }
}

} // namespace engine::game
