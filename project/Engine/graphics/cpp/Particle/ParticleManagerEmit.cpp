/**
 * @file ParticleManagerEmit.cpp
 * @brief ParticleManagerの各種パーティクル生成関数（Emit系）を実装するファイル
 * @note ParticleManager.cppからの分割ファイルクラス自体はParticleManagerのまま、定義の置き場所だけを分けている
 */
#include "ParticleManager.h"
#include "EngineAssert.h"
#include "GameConstants.h"
#include "TextureManager.h"
#include <cmath>
#include <d3dx12.h>
#include <numbers>
#include <random>
using namespace engine;
using namespace engine::graphics;

using namespace Microsoft::WRL;
// ══════════════════════════════════════════════════════
// パーティクル生成
// ══════════════════════════════════════════════════════

void ParticleManager::Emit(const std::string& name,
    const Vector3& position,
    const Vector3& velocity)
{
    EmitWithColor(name, position, velocity, { 1.0f, 1.0f, 1.0f, 1.0f }, 1.0f, 1.0f);
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
    ParticleGroup& group = particleGroups_[name];

    uint32_t slot = AllocateSlot(group);

    if (slot == UINT32_MAX) {
        return;
    }

    GPUParticleState& p = group.particleUploadData[slot];
    p.position = position;
    p.lifeTime = lifeTime;
    p.velocity = velocity;
    p.currentTime = 0.0f;
    p.color = color;
    p.scale = { scale, scale, scale };
    p.rotateZ = 0.0f;
    p.alive = 1;
    p.curveFlag = flicker ? 2u : 0u;

    group.slotExpiry[slot] = group.groupTime + lifeTime + 0.1f;
    group.aliveCount++;
    group.pendingSlots.push_back(slot);
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
    ParticleGroup& group = particleGroups_[name];

    uint32_t slot = AllocateSlot(group);

    if (slot == UINT32_MAX) {
        return;
    }

    GPUParticleState& p = group.particleUploadData[slot];
    p.position = position;
    p.lifeTime = lifeTime;
    p.velocity = velocity;
    p.currentTime = 0.0f;
    p.color = color;
    p.scale = { scaleX, scaleY, 1.0f };
    p.rotateZ = 0.0f;
    p.alive = 1;
    p.curveFlag = 0;

    group.slotExpiry[slot] = group.groupTime + lifeTime + 0.1f;
    group.aliveCount++;
    group.pendingSlots.push_back(slot);
}

void ParticleManager::EmitSlash(const std::string& name,
    const Vector3& position,
    float angle,
    const Vector4& color,
    float radius)
{
    ENGINE_ASSERT(particleGroups_.contains(name));
    ParticleGroup& group = particleGroups_[name];

    constexpr float kGlowLifeTime = 0.30f;
    constexpr float kCoreLifeTime = 0.20f;
    constexpr float kShardLifeTime = 0.25f;
    constexpr int kShardCount = 4;

    auto emitOne = [&](const Vector3& velocity, const Vector4& c, float lifeTime,
                       float scaleX, float scaleY) {
        uint32_t slot = AllocateSlot(group);
        if (slot == UINT32_MAX) {
            return;
        }

        GPUParticleState& p = group.particleUploadData[slot];
        p.position = position;
        p.lifeTime = lifeTime;
        p.velocity = velocity;
        p.currentTime = 0.0f;
        p.color = c;
        p.scale = { scaleX, scaleY, 1.0f };
        p.rotateZ = angle;
        p.alive = 1;
        p.curveFlag = 0;

        group.slotExpiry[slot] = group.groupTime + lifeTime + 0.1f;
        group.aliveCount++;
        group.pendingSlots.push_back(slot);
    };

    // 残光（太く淡い層）と芯（細く白に寄せた層）を重ねる
    Vector4 glow = { color.x, color.y, color.z, color.w * 0.35f };
    Vector4 core = { color.x * 0.4f + 0.6f, color.y * 0.4f + 0.6f,
        color.z * 0.4f + 0.6f, color.w };
    emitOne({ 0.0f, 0.0f, 0.0f }, glow, kGlowLifeTime, radius * 2.0f, radius * 0.55f);
    emitOne({ 0.0f, 0.0f, 0.0f }, core, kCoreLifeTime, radius * 1.9f, radius * 0.18f);

    // 斬線に沿って両端へ抜ける光片
    const Vector3 dir = { std::cos(angle), std::sin(angle), 0.0f };
    for (int i = 0; i < kShardCount; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(kShardCount - 1);
        float sign = (i % 2 == 0) ? 1.0f : -1.0f;
        float speed = radius * (3.0f + 3.0f * t) * sign;
        Vector4 c = { color.x, color.y, color.z, color.w * (0.8f - t * 0.3f) };
        emitOne({ dir.x * speed, dir.y * speed, 0.0f }, c, kShardLifeTime,
            radius * 0.6f, radius * 0.05f);
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
    std::uniform_real_distribution<float> yDist(0.0f, 12.0f);
    std::uniform_real_distribution<float> lifeDist(lifeTimeMin, lifeTimeMax);
    std::uniform_real_distribution<float> velXZDist(-0.3f, 0.3f);
    std::uniform_real_distribution<float> velYDist(0.1f, 0.6f);

    memset(group.particleUploadData, 0, sizeof(GPUParticleState) * ParticleGroup::kNumMaxInstance);
    group.slotExpiry.fill(0.0f);
    group.aliveCount = 0;
    group.freeList.clear();
    for (uint32_t i = ParticleGroup::kNumMaxInstance; i-- > count;) {
        group.freeList.push_back(i);
    }
    group.pendingSlots.clear();

    for (uint32_t i = 0; i < count; ++i) {
        float lifeTime = lifeDist(rng);
        std::uniform_real_distribution<float> timeDist(0.0f, lifeTime);
        float currentTime = timeDist(rng);

        GPUParticleState& p = group.particleUploadData[i];
        p.position = { center.x + xzDist(rng), center.y + yDist(rng), center.z + xzDist(rng) };
        p.lifeTime = lifeTime;
        p.velocity = { velXZDist(rng), velYDist(rng), velXZDist(rng) };
        p.currentTime = currentTime;
        p.color = color;
        p.scale = { scale, scale, scale };
        p.rotateZ = 0.0f;
        p.alive = 1;
        p.curveFlag = 0;

        group.slotExpiry[i] = group.groupTime + (lifeTime - currentTime) + 0.1f;
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

    for (uint32_t i = 0; i < count; ++i) {
        GPUParticleState& p = group.particleUploadData[i];
        // ランダムにばら撒く（初期化時と同様の範囲）
        static std::mt19937 rng { std::random_device { }() };
        std::uniform_real_distribution<float> distXZ(-20.0f, 20.0f);
        std::uniform_real_distribution<float> distY(0.0f, 12.0f);
        Vector3 randOffset = { distXZ(rng), distY(rng), distXZ(rng) };

        p.position = { position.x + randOffset.x, position.y + randOffset.y, position.z + randOffset.z };
        p.lifeTime = lifeTime;
        p.velocity = { 0.0f, 0.0f, 0.0f };
        p.currentTime = 0.0f;
        p.color = color;
        p.scale = { scale, scale, scale };
        p.rotateZ = 0.0f;
        p.alive = 1;
        p.curveFlag = flicker ? 2u : 0u;
        group.slotExpiry[i] = group.groupTime + lifeTime + 0.1f;
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
    std::uniform_real_distribution<float> scaleYDist(0.15f, 0.7f);
    std::uniform_real_distribution<float> speedDist(0.5f, 2.5f);
    std::uniform_real_distribution<float> lifeDist(0.2f, 0.5f);

    const int kCount = 8;

    for (int i = 0; i < kCount; ++i) {
        uint32_t slot = AllocateSlot(group);

        if (slot == UINT32_MAX) {
            break;
        }

        float rotAngle = rotDist(engine);
        float velAngle = rotDist(engine);
        float scaleY = scaleYDist(engine);
        float speed = speedDist(engine) * GameConstants::kFrameDeltaTime;
        float lifeTime = lifeDist(engine);

        Vector3 vel = { std::cos(velAngle) * speed, std::sin(velAngle) * speed, 0.0f };

        GPUParticleState& p = group.particleUploadData[slot];
        p.position = position;
        p.lifeTime = lifeTime;
        p.velocity = vel;
        p.currentTime = 0.0f;
        p.color = color;
        p.scale = { 0.04f, scaleY, 1.0f };
        p.rotateZ = rotAngle;
        p.alive = 1;
        p.curveFlag = 0;

        group.slotExpiry[slot] = group.groupTime + lifeTime + 0.1f;
        group.aliveCount++;
        group.pendingSlots.push_back(slot);
    }
}

void ParticleManager::EmitGravity(const std::string& name, const Vector3& position,
    const Vector3& velocity, const Vector4& color,
    float lifeTime, float scale)
{
    ENGINE_ASSERT(particleGroups_.contains(name));
    ParticleGroup& group = particleGroups_[name];

    uint32_t slot = AllocateSlot(group);
    if (slot == UINT32_MAX) {
        return;
    }

    GPUParticleState& p = group.particleUploadData[slot];
    p.position = position;
    p.lifeTime = lifeTime;
    p.velocity = velocity;
    p.currentTime = 0.0f;
    p.color = color;
    p.scale = { scale, scale, scale };
    p.rotateZ = 0.0f;
    p.alive = 1;
    p.curveFlag = 3;

    group.slotExpiry[slot] = group.groupTime + lifeTime + 0.1f;
    group.aliveCount++;
    group.pendingSlots.push_back(slot);
}

void ParticleManager::EmitRing(const std::string& name, const Vector3& position,
    float speed, const Vector4& color,
    uint32_t count, float lifeTime, float scale)
{
    ENGINE_ASSERT(particleGroups_.contains(name));
    ParticleGroup& group = particleGroups_[name];
    count = (std::min)(count, ParticleGroup::kNumMaxInstance);

    const float kTwoPi = 2.0f * std::numbers::pi_v<float>;

    for (uint32_t i = 0; i < count; ++i) {
        uint32_t slot = AllocateSlot(group);
        if (slot == UINT32_MAX) {
            break;
        }

        float angle = kTwoPi * static_cast<float>(i) / static_cast<float>(count);

        GPUParticleState& p = group.particleUploadData[slot];
        p.position = position;
        p.lifeTime = lifeTime;
        p.velocity = { std::cos(angle) * speed, std::sin(angle) * speed, 0.0f };
        p.currentTime = 0.0f;
        p.color = color;
        p.scale = { scale, scale, scale };
        p.rotateZ = 0.0f;
        p.alive = 1;
        p.curveFlag = 0;

        group.slotExpiry[slot] = group.groupTime + lifeTime + 0.1f;
        group.aliveCount++;
        group.pendingSlots.push_back(slot);
    }
}

void ParticleManager::EmitTrail(const std::string& name, const Vector3& position,
    const Vector4& color, float scale, float lifeTime)
{
    ENGINE_ASSERT(particleGroups_.contains(name));
    ParticleGroup& group = particleGroups_[name];

    uint32_t slot = AllocateSlot(group);
    if (slot == UINT32_MAX) {
        return;
    }

    GPUParticleState& p = group.particleUploadData[slot];
    p.position = position;
    p.lifeTime = lifeTime;
    p.velocity = { 0.0f, 0.0f, 0.0f };
    p.currentTime = 0.0f;
    p.color = color;
    p.scale = { scale, scale, scale };
    p.rotateZ = 0.0f;
    p.alive = 1;
    p.curveFlag = 4; // スケール縮小フェードアウト

    group.slotExpiry[slot] = group.groupTime + lifeTime + 0.1f;
    group.aliveCount++;
    group.pendingSlots.push_back(slot);
}

//  Update: CS ディスパッチ（フェーズ分割版）

