/**
 * @file ParticleManagerEmit.cpp
 * @brief ParticleManagerの各種パーティクル生成関数（Emit系）を実装するファイル
 * @note ParticleManager の粒の発生だけを ParticleManager.cpp から分けて置いている（クラスは ParticleManager のまま）
 */
#include "ParticleManager.h"
#include "EngineAssert.h"
#include "EngineConstants.h"
#include "GraphicsTuning.h"
#include "TextureManager.h"
#include <algorithm>
#include <cmath>
#include <d3dx12.h>
#include <numbers>
#include <random>

using namespace Microsoft::WRL;

namespace engine::graphics {
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

/** @brief 斬線の描画パラメーターを設定から読んで返す（初回だけ読む） */
const SlashPreset& LoadedSlashPreset() {
    constexpr int kMinShardCount = 2; // 先頭と最後の光片で速さを補間するため2本以上
    constexpr int kMaxShardCount = 32;
    static const SlashPreset kPreset = {
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
    return kPreset;
}

/** @brief 命中のきらめきの設定（graphics_tuning.json の hitStar.～。既定値は上の定数） */
struct HitStarPreset {
    int count;
    float width, lengthMin, lengthMax, speedMin, speedMax, lifeMin, lifeMax;
};

/** @brief きらめきの描画パラメーターを設定から読んで返す（初回だけ読む） */
const HitStarPreset& LoadedHitStarPreset() {
    static constexpr int kMaxHitStarCount = 64;
    static const HitStarPreset kPreset = [] {
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
    return kPreset;
}

/** @brief 3要素すべてが scale のベクトルを返す */
Vector3 Uniform(float scale) {
    return { scale, scale, scale };
}
} // namespace

/** @brief 空きスロットへ1粒書き込む（空きが無ければ何もせず false） */
bool ParticleManager::SpawnParticle(ParticleGroup& group, const ParticleSpawn& spawn) {
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

/** @brief 白・寿命1秒・大きさ1で1粒放出する */
void ParticleManager::Emit(const std::string& name,
    const Vector3& position,
    const Vector3& velocity) {
    EmitWithColor(name, position, velocity, kWhite, 1.0f, 1.0f);
}

/** @brief 色・寿命・スケールを指定して1粒放出する */
void ParticleManager::EmitWithColor(const std::string& name,
    const Vector3& position,
    const Vector3& velocity,
    const Vector4& color,
    float lifeTime,
    float scale,
    bool flicker) {
    ENGINE_ASSERT(particleGroups_.contains(name));
    SpawnParticle(particleGroups_[name], { .position = position, .velocity = velocity, .color = color, .scale = Uniform(scale),
        .lifeTime = lifeTime, .motion = flicker ? ParticleMotion::kFlicker : ParticleMotion::kLinear });
}

/** @brief 横長の楕円形スケールで1粒放出する（斬撃の残光等に使用） */
void ParticleManager::EmitEllipse(const std::string& name,
    const Vector3& position,
    const Vector3& velocity,
    const Vector4& color,
    float lifeTime,
    float scaleX,
    float scaleY) {
    ENGINE_ASSERT(particleGroups_.contains(name));
    SpawnParticle(particleGroups_[name], { .position = position, .velocity = velocity, .color = color,
        .scale = { scaleX, scaleY, 1.0f }, .lifeTime = lifeTime });
}

/** @brief 斬撃の剣閃（残光＋芯＋斬線に沿って抜ける光片）を放出する */
void ParticleManager::EmitSlash(const std::string& name,
    const Vector3& position,
    float angle,
    const Vector4& color,
    float radius) {
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

/** @brief 発生し直す前に全スロットを空にし、先頭 activeCount 個以外を空き一覧へ戻す */
void ParticleManager::ResetSlotsForEmit(ParticleGroup& group, uint32_t activeCount) {
    // 前の状態を消し、生きている粒を0にする
    memset(group.particleUploadData, 0, sizeof(GPUParticleState) * ParticleGroup::kNumMaxInstance);
    group.slotExpiry.fill(0.0f);
    group.aliveCount = 0;
    // 使わない後ろのスロットを、番号の大きい順に空き一覧へ積む（取り出す時に小さい番号から使われる）
    group.freeList.clear();
    for (uint32_t i = ParticleGroup::kNumMaxInstance; i-- > activeCount;) {
        group.freeList.push_back(i);
    }
    group.pendingSlots.clear();
}

/** @brief 指定半径・高さにランダム配置し、寿命が尽きると自動再配置するループエフェクトを開始する */
void ParticleManager::EmitScatterLoop(const std::string& name,
    const Vector3& center, float radius,
    uint32_t count, const Vector4& color,
    float lifeTimeMin, float lifeTimeMax, float scale) {
    // 登録済みのグループに、持てる数までの粒を出す
    ENGINE_ASSERT(particleGroups_.contains(name));
    ParticleGroup& group = particleGroups_[name];
    count = (std::min)(count, ParticleGroup::kNumMaxInstance);

    // 位置・寿命・速度のばらつき
    static std::mt19937 sRng { std::random_device { }() };
    std::uniform_real_distribution<float> xzDist(-radius, radius);
    std::uniform_real_distribution<float> yDist(0.0f, kScatterHeight);
    std::uniform_real_distribution<float> lifeDist(lifeTimeMin, lifeTimeMax);
    std::uniform_real_distribution<float> velXZDist(-kScatterDriftXZ, kScatterDriftXZ);
    std::uniform_real_distribution<float> velYDist(kScatterRiseMin, kScatterRiseMax);

    // 全スロットを空にしてから、寿命の途中から始まる粒を範囲内にばら撒く
    ResetSlotsForEmit(group, count);

    for (uint32_t i = 0; i < count; ++i) {
        // 寿命の途中から始めて、最初から全体に行き渡った状態に見せる
        float lifeTime = lifeDist(sRng);
        std::uniform_real_distribution<float> timeDist(0.0f, lifeTime);
        float currentTime = timeDist(sRng);

        GPUParticleState& p = group.particleUploadData[i];
        p.position = { center.x + xzDist(sRng), center.y + yDist(sRng), center.z + xzDist(sRng) };
        p.lifeTime = lifeTime;
        p.velocity = { velXZDist(sRng), velYDist(sRng), velXZDist(sRng) };
        p.currentTime = currentTime;
        p.color = color;
        p.scale = Uniform(scale);
        p.rotateZ = 0.0f;
        p.alive = 1;
        p.curveFlag = static_cast<uint32_t>(ParticleMotion::kLinear);

        group.slotExpiry[i] = group.groupTime + (lifeTime - currentTime) + kSlotExpiryMargin;
        group.aliveCount++;
    }

    // 次の描画で全スロットを GPU へ送り直す
    group.needsInit = true;

    // 寿命が尽きた粒を同じ設定で出し直すよう覚えておく
    group.autoRespawn = true;
    group.respawnConfig = { center, radius, lifeTimeMin, lifeTimeMax, color, scale, count };
}

/** @brief 指定数のパーティクルをランダム位置に一斉配置する（環境パーティクル等） */
void ParticleManager::EmitBurst(const std::string& name,
    const Vector3& position,
    const Vector4& color,
    uint32_t count,
    float lifeTime,
    float scale,
    bool flicker) {
    ENGINE_ASSERT(particleGroups_.contains(name));
    ParticleGroup& group = particleGroups_[name];

    count = (std::min)(count, ParticleGroup::kNumMaxInstance);

    // 全スロットをリセット（re-emit 時に前の状態を消す）
    ResetSlotsForEmit(group, count);

    static std::mt19937 sRng { std::random_device { }() };
    std::uniform_real_distribution<float> distXZ(-kBurstSpreadXZ, kBurstSpreadXZ);
    std::uniform_real_distribution<float> distY(0.0f, kScatterHeight);
    for (uint32_t i = 0; i < count; ++i) {
        GPUParticleState& p = group.particleUploadData[i];
        // ランダムにばら撒く（初期化時と同様の範囲）
        const Vector3 randOffset = { distXZ(sRng), distY(sRng), distXZ(sRng) };

        p.position = { position.x + randOffset.x, position.y + randOffset.y, position.z + randOffset.z };
        p.lifeTime = lifeTime;
        p.velocity = kStill;
        p.currentTime = 0.0f;
        p.color = color;
        p.scale = Uniform(scale);
        p.rotateZ = 0.0f;
        p.alive = 1;
        p.curveFlag = static_cast<uint32_t>(flicker ? ParticleMotion::kFlicker : ParticleMotion::kLinear);
        group.slotExpiry[i] = group.groupTime + lifeTime + kSlotExpiryMargin;
        group.aliveCount++;
    }

    // 書き込んだスロットを、次の更新で GPU へ送る対象にする
    group.needsInit = true;
    for (uint32_t i = 0; i < count; ++i) {
        group.pendingSlots.push_back(i);
    }
}

/** @brief ヒット時の星形エフェクト用にランダム方向・スケールで複数粒放出する */
void ParticleManager::EmitHitStar(const std::string& name,
    const Vector3& position,
    const Vector4& color) {
    ENGINE_ASSERT(particleGroups_.contains(name));
    ParticleGroup& group = particleGroups_[name];

    static std::default_random_engine sEngine { std::random_device { }() };
    std::uniform_real_distribution<float> rotDist(-std::numbers::pi_v<float>, std::numbers::pi_v<float>);
    const HitStarPreset& preset = LoadedHitStarPreset();
    std::uniform_real_distribution<float> lengthDist(preset.lengthMin, preset.lengthMax);
    std::uniform_real_distribution<float> speedDist(preset.speedMin, preset.speedMax);
    std::uniform_real_distribution<float> lifeDist(preset.lifeMin, preset.lifeMax);

    for (int i = 0; i < preset.count; ++i) {
        const float velAngle = rotDist(sEngine);
        const float speed = speedDist(sEngine) * engineConstants::kFrameDeltaTime;
        const bool spawned = SpawnParticle(group, {
                                                      .position = position,
                                                      .velocity = { std::cos(velAngle) * speed, std::sin(velAngle) * speed, 0.0f },
                                                      .color = color,
                                                      .scale = { preset.width, lengthDist(sEngine), 1.0f },
                                                      .lifeTime = lifeDist(sEngine),
                                                      .rotateZ = rotDist(sEngine),
                                                  });
        if (!spawned) {
            break;
        }
    }
}

/** @brief 初速を与えて重力落下するパーティクルを1粒放出する（curveFlag=3） */
void ParticleManager::EmitGravity(const std::string& name, const Vector3& position,
    const Vector3& velocity, const Vector4& color,
    float lifeTime, float scale) {
    ENGINE_ASSERT(particleGroups_.contains(name));
    SpawnParticle(particleGroups_[name], { .position = position, .velocity = velocity, .color = color, .scale = Uniform(scale),
        .lifeTime = lifeTime, .motion = ParticleMotion::kGravity });
}

/** @brief 均等角度で放射状にリングエフェクトを放出する（ヒットストップ等） */
void ParticleManager::EmitRing(const std::string& name, const Vector3& position,
    float speed, const Vector4& color,
    uint32_t count, float lifeTime, float scale) {
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

/** @brief 残像1粒を放出する毎フレーム呼び続けることでトレイルを形成する */
void ParticleManager::EmitTrail(const std::string& name, const Vector3& position,
    const Vector4& color, float scale, float lifeTime) {
    ENGINE_ASSERT(particleGroups_.contains(name));
    SpawnParticle(particleGroups_[name], { .position = position, .velocity = kStill, .color = color, .scale = Uniform(scale),
        .lifeTime = lifeTime, .motion = ParticleMotion::kShrinkFade });
}

//  Update: CS ディスパッチ（フェーズ分割版）

} // namespace engine::graphics
