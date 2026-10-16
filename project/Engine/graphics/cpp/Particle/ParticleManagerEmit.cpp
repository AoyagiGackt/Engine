/**
 * @file ParticleManagerEmit.cpp
 * @brief ParticleManagerの各種パーティクル生成関数（Emit系）を実装するファイル
 * @note ParticleManager.cppからの分割ファイルクラス自体はParticleManagerのまま、定義の置き場所だけを分けている
 */
#include "ParticleManager.h"
#include "EngineAssert.h"
#include "GameConstants.h"
#include "GraphicsTuning.h"
#include "TextureManager.h"
#include <algorithm>
#include <cmath>
#include <d3dx12.h>
#include <numbers>
#include <random>
using namespace engine;
using namespace engine::graphics;

using namespace Microsoft::WRL;

namespace {
constexpr Vector4 kWhite = { 1.0f, 1.0f, 1.0f, 1.0f };
constexpr Vector3 kStill = { 0.0f, 0.0f, 0.0f };
constexpr float kTwoPi = 2.0f * std::numbers::pi_v<float>;

// 斬撃の剣閃: 太く淡い残光 + 白に寄せた細い芯 + 斬線に沿って両端へ抜ける光片
constexpr float kSlashGlowLifeTime = 0.30f;
constexpr float kSlashGlowAlpha = 0.35f;
constexpr float kSlashGlowLength = 2.0f; // 半径に掛ける倍率
constexpr float kSlashGlowWidth = 0.55f;
constexpr float kSlashCoreLifeTime = 0.20f;
constexpr float kSlashCoreWhiten = 0.6f; // 芯の色を白へ寄せる割合
constexpr float kSlashCoreLength = 1.9f;
constexpr float kSlashCoreWidth = 0.18f;
constexpr float kSlashShardLifeTime = 0.25f;
constexpr int kSlashShardCount = 4;
constexpr float kSlashShardSpeedBase = 3.0f; // 半径に掛ける速さ（先頭の光片）
constexpr float kSlashShardSpeedGain = 3.0f; // 最後の光片で足す速さ
constexpr float kSlashShardAlphaBase = 0.8f;
constexpr float kSlashShardAlphaFade = 0.3f;
constexpr float kSlashShardLength = 0.6f;
constexpr float kSlashShardWidth = 0.05f;

// 一斉配置のばらまき範囲
constexpr float kBurstSpreadXZ = 20.0f;

// ヒット時の星形: ランダムな向き・長さの細い光片
constexpr int kHitStarCount = 8;
constexpr float kHitStarWidth = 0.04f;
constexpr float kHitStarLengthMin = 0.15f;
constexpr float kHitStarLengthMax = 0.7f;
constexpr float kHitStarSpeedMin = 0.5f;
constexpr float kHitStarSpeedMax = 2.5f;
constexpr float kHitStarLifeMin = 0.2f;
constexpr float kHitStarLifeMax = 0.5f;

/** @brief 斬線の層の設定（graphics_tuning.json の slash.～。既定値は上の定数） */
struct SlashPreset {
    float glowLifeTime, glowAlpha, glowLength, glowWidth;
    float coreLifeTime, coreWhiten, coreLength, coreWidth;
    float shardLifeTime;
    int shardCount;
    float shardSpeedBase, shardSpeedGain, shardAlphaBase, shardAlphaFade, shardLength, shardWidth;
};

const SlashPreset& LoadedSlashPreset()
{
    constexpr int kMinShardCount = 2; // 先頭と最後の光片で速さを補間するため2本以上
    constexpr int kMaxShardCount = 32;
    static const SlashPreset preset = {
        GraphicsTuning::Positive("slash.glowLifeTime", kSlashGlowLifeTime),
        GraphicsTuning::Float("slash.glowAlpha", kSlashGlowAlpha, 0.0f, 1.0f),
        GraphicsTuning::Float("slash.glowLength", kSlashGlowLength),
        GraphicsTuning::Float("slash.glowWidth", kSlashGlowWidth),
        GraphicsTuning::Positive("slash.coreLifeTime", kSlashCoreLifeTime),
        GraphicsTuning::Float("slash.coreWhiten", kSlashCoreWhiten, 0.0f, 1.0f),
        GraphicsTuning::Float("slash.coreLength", kSlashCoreLength),
        GraphicsTuning::Float("slash.coreWidth", kSlashCoreWidth),
        GraphicsTuning::Positive("slash.shardLifeTime", kSlashShardLifeTime),
        GraphicsTuning::Int("slash.shardCount", kSlashShardCount, kMinShardCount, kMaxShardCount),
        GraphicsTuning::Float("slash.shardSpeedBase", kSlashShardSpeedBase),
        GraphicsTuning::Float("slash.shardSpeedGain", kSlashShardSpeedGain),
        GraphicsTuning::Float("slash.shardAlphaBase", kSlashShardAlphaBase, 0.0f, 1.0f),
        GraphicsTuning::Float("slash.shardAlphaFade", kSlashShardAlphaFade, 0.0f, 1.0f),
        GraphicsTuning::Float("slash.shardLength", kSlashShardLength),
        GraphicsTuning::Float("slash.shardWidth", kSlashShardWidth),
    };
    return preset;
}

/** @brief 命中のきらめきの設定（graphics_tuning.json の hitStar.～。既定値は上の定数） */
struct HitStarPreset {
    int count;
    float width, lengthMin, lengthMax, speedMin, speedMax, lifeMin, lifeMax;
};

const HitStarPreset& LoadedHitStarPreset()
{
    static constexpr int kMaxHitStarCount = 64;
    static const HitStarPreset preset = [] {
        HitStarPreset p {
            GraphicsTuning::Int("hitStar.count", kHitStarCount, 0, kMaxHitStarCount),
            GraphicsTuning::Float("hitStar.width", kHitStarWidth),
            GraphicsTuning::Float("hitStar.lengthMin", kHitStarLengthMin),
            GraphicsTuning::Float("hitStar.lengthMax", kHitStarLengthMax),
            GraphicsTuning::Float("hitStar.speedMin", kHitStarSpeedMin),
            GraphicsTuning::Float("hitStar.speedMax", kHitStarSpeedMax),
            GraphicsTuning::Positive("hitStar.lifeMin", kHitStarLifeMin),
            GraphicsTuning::Positive("hitStar.lifeMax", kHitStarLifeMax),
        };
        // 一様分布の下限と上限が逆にならないようにする
        p.lengthMax = (std::max)(p.lengthMax, p.lengthMin);
        p.speedMax = (std::max)(p.speedMax, p.speedMin);
        p.lifeMax = (std::max)(p.lifeMax, p.lifeMin);
        return p;
    }();
    return preset;
}

Vector3 Uniform(float scale)
{
    return { scale, scale, scale };
}
} // namespace

bool ParticleManager::SpawnParticle(ParticleGroup& group, const ParticleSpawn& spawn)
{
    const uint32_t slot = AllocateSlot(group);
    if (slot == UINT32_MAX) {
        return false;
    }
    GPUParticleState& p = group.particleUploadData[slot];
    p.position = spawn.position;
    p.lifeTime = spawn.lifeTime;
    p.velocity = spawn.velocity;
    p.currentTime = 0.0f;
    p.color = spawn.color;
    p.scale = spawn.scale;
    p.rotateZ = spawn.rotateZ;
    p.alive = 1;
    p.curveFlag = static_cast<uint32_t>(spawn.motion);

    group.slotExpiry[slot] = group.groupTime + spawn.lifeTime + kSlotExpiryMargin;
    group.aliveCount++;
    group.pendingSlots.push_back(slot);
    return true;
}

// ══════════════════════════════════════════════════════
// パーティクル生成
// ══════════════════════════════════════════════════════

void ParticleManager::Emit(const std::string& name,
    const Vector3& position,
    const Vector3& velocity)
{
    EmitWithColor(name, position, velocity, kWhite, 1.0f, 1.0f);
}

void ParticleManager::EmitWithColor(const std::string& name,
    const Vector3& position,
    const Vector3& velocity,
    const Vector4& color,
    float lifeTime,
    float scale,
    bool flicker)
{
    ENGINE_ASSERT(particleGroups_.contains(name));
    SpawnParticle(particleGroups_[name], { .position = position, .velocity = velocity, .color = color, .scale = Uniform(scale),
        .lifeTime = lifeTime, .motion = flicker ? ParticleMotion::Flicker : ParticleMotion::Linear });
}

void ParticleManager::EmitEllipse(const std::string& name,
    const Vector3& position,
    const Vector3& velocity,
    const Vector4& color,
    float lifeTime,
    float scaleX,
    float scaleY)
{
    ENGINE_ASSERT(particleGroups_.contains(name));
    SpawnParticle(particleGroups_[name], { .position = position, .velocity = velocity, .color = color,
        .scale = { scaleX, scaleY, 1.0f }, .lifeTime = lifeTime });
}

void ParticleManager::EmitSlash(const std::string& name,
    const Vector3& position,
    float angle,
    const Vector4& color,
    float radius)
{
    ENGINE_ASSERT(particleGroups_.contains(name));
    ParticleGroup& group = particleGroups_[name];

    auto emitOne = [&](const Vector3& velocity, const Vector4& c, float lifeTime, float scaleX, float scaleY) {
        SpawnParticle(group, { .position = position, .velocity = velocity, .color = c,
            .scale = { scaleX, scaleY, 1.0f }, .lifeTime = lifeTime, .rotateZ = angle });
    };

    // 残光（太く淡い層）と芯（細く白に寄せた層）を重ねる
    const SlashPreset& preset = LoadedSlashPreset();
    const float keep = 1.0f - preset.coreWhiten;
    const Vector4 glow = { color.x, color.y, color.z, color.w * preset.glowAlpha };
    const Vector4 core = { color.x * keep + preset.coreWhiten, color.y * keep + preset.coreWhiten,
        color.z * keep + preset.coreWhiten, color.w };
    emitOne(kStill, glow, preset.glowLifeTime, radius * preset.glowLength, radius * preset.glowWidth);
    emitOne(kStill, core, preset.coreLifeTime, radius * preset.coreLength, radius * preset.coreWidth);

    // 斬線に沿って両端へ交互に抜ける光片（後ろの光片ほど速く、薄く）
    const Vector3 dir = { std::cos(angle), std::sin(angle), 0.0f };
    for (int i = 0; i < preset.shardCount; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(preset.shardCount - 1);
        const float sign = (i % 2 == 0) ? 1.0f : -1.0f;
        const float speed = radius * (preset.shardSpeedBase + preset.shardSpeedGain * t) * sign;
        const Vector4 c = { color.x, color.y, color.z, color.w * (preset.shardAlphaBase - t * preset.shardAlphaFade) };
        emitOne({ dir.x * speed, dir.y * speed, 0.0f }, c, preset.shardLifeTime,
            radius * preset.shardLength, radius * preset.shardWidth);
    }
}

void ParticleManager::EmitScatterLoop(const std::string& name,
    const Vector3& center, float radius,
    uint32_t count, const Vector4& color,
    float lifeTimeMin, float lifeTimeMax, float scale)
{
    ENGINE_ASSERT(particleGroups_.contains(name));
    ParticleGroup& group = particleGroups_[name];
    count = (std::min)(count, ParticleGroup::kNumMaxInstance);

    static std::mt19937 rng { std::random_device { }() };
    std::uniform_real_distribution<float> xzDist(-radius, radius);
    std::uniform_real_distribution<float> yDist(0.0f, kScatterHeight);
    std::uniform_real_distribution<float> lifeDist(lifeTimeMin, lifeTimeMax);
    std::uniform_real_distribution<float> velXZDist(-kScatterDriftXZ, kScatterDriftXZ);
    std::uniform_real_distribution<float> velYDist(kScatterRiseMin, kScatterRiseMax);

    memset(group.particleUploadData, 0, sizeof(GPUParticleState) * ParticleGroup::kNumMaxInstance);
    group.slotExpiry.fill(0.0f);
    group.aliveCount = 0;
    group.freeList.clear();
    for (uint32_t i = ParticleGroup::kNumMaxInstance; i-- > count;) {
        group.freeList.push_back(i);
    }
    group.pendingSlots.clear();

    for (uint32_t i = 0; i < count; ++i) {
        // 寿命の途中から始めて、最初から全体に行き渡った状態に見せる
        float lifeTime = lifeDist(rng);
        std::uniform_real_distribution<float> timeDist(0.0f, lifeTime);
        float currentTime = timeDist(rng);

        GPUParticleState& p = group.particleUploadData[i];
        p.position = { center.x + xzDist(rng), center.y + yDist(rng), center.z + xzDist(rng) };
        p.lifeTime = lifeTime;
        p.velocity = { velXZDist(rng), velYDist(rng), velXZDist(rng) };
        p.currentTime = currentTime;
        p.color = color;
        p.scale = Uniform(scale);
        p.rotateZ = 0.0f;
        p.alive = 1;
        p.curveFlag = static_cast<uint32_t>(ParticleMotion::Linear);

        group.slotExpiry[i] = group.groupTime + (lifeTime - currentTime) + kSlotExpiryMargin;
        group.aliveCount++;
    }

    group.needsInit = true;

    group.autoRespawn = true;
    group.respawnConfig = { center, radius, lifeTimeMin, lifeTimeMax, color, scale, count };
}

void ParticleManager::EmitBurst(const std::string& name,
    const Vector3& position,
    const Vector4& color,
    uint32_t count,
    float lifeTime,
    float scale,
    bool flicker)
{
    ENGINE_ASSERT(particleGroups_.contains(name));
    ParticleGroup& group = particleGroups_[name];

    count = (std::min)(count, ParticleGroup::kNumMaxInstance);

    // 全スロットをリセット（re-emit 時に前の状態を消す）
    memset(group.particleUploadData, 0,
        sizeof(GPUParticleState) * ParticleGroup::kNumMaxInstance);
    group.slotExpiry.fill(0.0f);
    group.aliveCount = 0;
    group.freeList.clear();
    for (uint32_t i = ParticleGroup::kNumMaxInstance; i-- > count;) {
        group.freeList.push_back(i);
    }

    static std::mt19937 rng { std::random_device { }() };
    std::uniform_real_distribution<float> distXZ(-kBurstSpreadXZ, kBurstSpreadXZ);
    std::uniform_real_distribution<float> distY(0.0f, kScatterHeight);
    for (uint32_t i = 0; i < count; ++i) {
        GPUParticleState& p = group.particleUploadData[i];
        // ランダムにばら撒く（初期化時と同様の範囲）
        const Vector3 randOffset = { distXZ(rng), distY(rng), distXZ(rng) };

        p.position = { position.x + randOffset.x, position.y + randOffset.y, position.z + randOffset.z };
        p.lifeTime = lifeTime;
        p.velocity = kStill;
        p.currentTime = 0.0f;
        p.color = color;
        p.scale = Uniform(scale);
        p.rotateZ = 0.0f;
        p.alive = 1;
        p.curveFlag = static_cast<uint32_t>(flicker ? ParticleMotion::Flicker : ParticleMotion::Linear);
        group.slotExpiry[i] = group.groupTime + lifeTime + kSlotExpiryMargin;
        group.aliveCount++;
    }

    group.needsInit = true;
    group.pendingSlots.clear();
    for (uint32_t i = 0; i < count; ++i) {
        group.pendingSlots.push_back(i);
    }
}

void ParticleManager::EmitHitStar(const std::string& name,
    const Vector3& position,
    const Vector4& color)
{
    ENGINE_ASSERT(particleGroups_.contains(name));
    ParticleGroup& group = particleGroups_[name];

    static std::default_random_engine engine { std::random_device { }() };
    std::uniform_real_distribution<float> rotDist(-std::numbers::pi_v<float>, std::numbers::pi_v<float>);
    const HitStarPreset& preset = LoadedHitStarPreset();
    std::uniform_real_distribution<float> lengthDist(preset.lengthMin, preset.lengthMax);
    std::uniform_real_distribution<float> speedDist(preset.speedMin, preset.speedMax);
    std::uniform_real_distribution<float> lifeDist(preset.lifeMin, preset.lifeMax);

    for (int i = 0; i < preset.count; ++i) {
        const float velAngle = rotDist(engine);
        const float speed = speedDist(engine) * GameConstants::kFrameDeltaTime;
        const bool spawned = SpawnParticle(group, {
                                                      .position = position,
                                                      .velocity = { std::cos(velAngle) * speed, std::sin(velAngle) * speed, 0.0f },
                                                      .color = color,
                                                      .scale = { preset.width, lengthDist(engine), 1.0f },
                                                      .lifeTime = lifeDist(engine),
                                                      .rotateZ = rotDist(engine),
                                                  });
        if (!spawned) {
            break;
        }
    }
}

void ParticleManager::EmitGravity(const std::string& name, const Vector3& position,
    const Vector3& velocity, const Vector4& color,
    float lifeTime, float scale)
{
    ENGINE_ASSERT(particleGroups_.contains(name));
    SpawnParticle(particleGroups_[name], { .position = position, .velocity = velocity, .color = color, .scale = Uniform(scale),
        .lifeTime = lifeTime, .motion = ParticleMotion::Gravity });
}

void ParticleManager::EmitRing(const std::string& name, const Vector3& position,
    float speed, const Vector4& color,
    uint32_t count, float lifeTime, float scale)
{
    ENGINE_ASSERT(particleGroups_.contains(name));
    ParticleGroup& group = particleGroups_[name];
    count = (std::min)(count, ParticleGroup::kNumMaxInstance);

    for (uint32_t i = 0; i < count; ++i) {
        // 均等な角度で放射状に飛ばす
        const float angle = kTwoPi * static_cast<float>(i) / static_cast<float>(count);
        const bool spawned = SpawnParticle(group, { .position = position,
            .velocity = { std::cos(angle) * speed, std::sin(angle) * speed, 0.0f },
            .color = color, .scale = Uniform(scale), .lifeTime = lifeTime });
        if (!spawned) {
            break;
        }
    }
}

void ParticleManager::EmitTrail(const std::string& name, const Vector3& position,
    const Vector4& color, float scale, float lifeTime)
{
    ENGINE_ASSERT(particleGroups_.contains(name));
    SpawnParticle(particleGroups_[name], { .position = position, .velocity = kStill, .color = color, .scale = Uniform(scale),
        .lifeTime = lifeTime, .motion = ParticleMotion::ShrinkFade });
}

//  Update: CS ディスパッチ（フェーズ分割版）
