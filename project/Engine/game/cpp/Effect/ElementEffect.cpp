/**
 * @file ElementEffect.cpp
 * @brief 武器属性ごとの命中演出と振り中オーラの実装
 */
#include "ElementEffect.h"
#include "ParticleManager.h"
#include <memory>
#include <unordered_map>
using namespace engine;
using namespace engine::graphics;
using namespace engine::game;

namespace {

/** @brief -1〜1 の一様乱数を返す */
float RandomUnit(std::mt19937& rng)
{
    std::uniform_real_distribution<float> unitD(-1.0f, 1.0f);
    return unitD(rng);
}

/** @brief 2分の1の確率で true を返す */
bool CoinFlip(std::mt19937& rng)
{
    return rng() % 2 == 0;
}

/** @brief 水: 跳ね上がる水しぶきと二重の波紋 */
class WaterElementEffect final : public IElementEffect {
public:
    void EmitHit(const ElementEffectContext& ctx, const Vector3& pos, const Vector4& color, float scale) const override
    {
        constexpr int kDropletCount = 14;
        constexpr float kDropletSpreadX = 3.0f;
        constexpr float kDropletRiseMin = 2.5f;
        constexpr float kDropletRiseMax = 5.5f;
        constexpr float kDropletLifetime = 0.6f;
        constexpr float kDropletSize = 0.13f;
        constexpr int kFoamInterval = 3; // この間隔ごとに泡色の粒を混ぜる
        constexpr float kInnerRippleSpeed = 1.6f;
        constexpr int kInnerRippleCount = 22;
        constexpr float kInnerRippleLifetime = 0.45f;
        constexpr float kInnerRippleSize = 0.12f;
        constexpr float kOuterRippleSpeed = 3.0f;
        constexpr int kOuterRippleCount = 26;
        constexpr float kOuterRippleLifetime = 0.35f;
        constexpr float kOuterRippleSize = 0.08f;
        std::uniform_real_distribution<float> vxD(-kDropletSpreadX, kDropletSpreadX);
        std::uniform_real_distribution<float> vyD(kDropletRiseMin, kDropletRiseMax);
        for (int i = 0; i < kDropletCount; ++i) {
            ctx.particles.EmitGravity("hit_spark", pos, { vxD(ctx.rng), vyD(ctx.rng), 0.0f },
                i % kFoamInterval == 0 ? kFoamColor : color, kDropletLifetime, kDropletSize * scale);
        }
        ctx.particles.EmitRing("hit_ring", pos, kInnerRippleSpeed * scale, color,
            kInnerRippleCount, kInnerRippleLifetime, kInnerRippleSize);
        ctx.particles.EmitRing("hit_ring", pos, kOuterRippleSpeed * scale, kFoamColor,
            kOuterRippleCount, kOuterRippleLifetime, kOuterRippleSize);
    }

    void EmitAura(const ElementEffectContext& ctx, const Vector3& pos, const Vector4& color) const override
    {
        // 水滴が弧を描いて落ちる
        constexpr float kDropSpeedX = 1.2f;
        constexpr float kDropRise = 1.0f;
        constexpr float kDropLifetime = 0.45f;
        constexpr float kDropSize = 0.08f;
        ctx.particles.EmitGravity("hit_spark", pos, { RandomUnit(ctx.rng) * kDropSpeedX, kDropRise, 0.0f },
            color, kDropLifetime, kDropSize);
    }

private:
    static constexpr Vector4 kFoamColor = { 0.85f, 0.97f, 1.0f, 1.0f };
};

/** @brief 炎: 上へ噴き上がる火の粉 */
class FireElementEffect final : public IElementEffect {
public:
    void EmitHit(const ElementEffectContext& ctx, const Vector3& pos, const Vector4& color, float scale) const override
    {
        constexpr int kEmberCount = 10;
        constexpr int kEmberColumns = 5; // 横方向に並べる列数
        constexpr int kEmberCenterColumn = 2;
        constexpr float kEmberColumnSpacing = 0.65f;
        constexpr float kEmberRiseBase = 3.2f;
        constexpr int kEmberRiseSteps = 3;
        constexpr float kEmberRiseStep = 0.9f;
        constexpr Vector4 kEmberSubColor = { 1.0f, 0.55f, 0.05f, 1.0f };
        constexpr float kEmberLifetime = 0.5f;
        constexpr float kEmberSize = 0.18f;
        for (int i = 0; i < kEmberCount; ++i) {
            const float side = static_cast<float>((i % kEmberColumns) - kEmberCenterColumn) * kEmberColumnSpacing;
            ctx.particles.EmitGravity("hit_spark", pos, { side, kEmberRiseBase + (i % kEmberRiseSteps) * kEmberRiseStep, 0.0f },
                i % 2 == 0 ? color : kEmberSubColor, kEmberLifetime, kEmberSize * scale);
        }
    }

    void EmitAura(const ElementEffectContext& ctx, const Vector3& pos, const Vector4& color) const override
    {
        // 火の粉がふわりと昇る
        constexpr Vector4 kEmberColor = { 1.0f, 0.6f, 0.1f, 1.0f };
        constexpr float kEmberRiseMin = 0.8f;
        constexpr float kEmberRiseMax = 2.0f;
        constexpr float kEmberDriftX = 0.4f;
        constexpr float kEmberLifetime = 0.4f;
        constexpr float kEmberSize = 0.1f;
        std::uniform_real_distribution<float> riseD(kEmberRiseMin, kEmberRiseMax);
        ctx.particles.EmitWithColor("hit_spark", pos, { RandomUnit(ctx.rng) * kEmberDriftX, riseD(ctx.rng), 0.0f },
            CoinFlip(ctx.rng) ? color : kEmberColor, kEmberLifetime, kEmberSize, true);
    }
};

/** @brief 雷: 白い芯を持つ高速の十字放電 */
class LightningElementEffect final : public IElementEffect {
public:
    void EmitHit(const ElementEffectContext& ctx, const Vector3& pos, const Vector4& color, float scale) const override
    {
        constexpr float kBoltSpeedX = 2.0f;
        constexpr float kHorizontalBoltLifetime = 0.16f;
        constexpr float kHorizontalBoltLength = 3.4f;
        constexpr float kHorizontalBoltThickness = 0.12f;
        constexpr Vector4 kBoltCoreColor = { 1.0f, 1.0f, 0.85f, 1.0f };
        constexpr float kVerticalBoltLifetime = 0.12f;
        constexpr float kVerticalBoltThickness = 0.14f;
        constexpr float kVerticalBoltLength = 3.0f;
        constexpr float kBoltRingSpeed = 4.5f;
        constexpr int kBoltRingCount = 8;
        constexpr float kBoltRingLifetime = 0.14f;
        constexpr float kBoltRingSize = 0.1f;
        ctx.particles.EmitEllipse("hit_spark", pos, { ctx.facingX * kBoltSpeedX, 0.0f, 0.0f }, color,
            kHorizontalBoltLifetime, kHorizontalBoltLength * scale, kHorizontalBoltThickness);
        ctx.particles.EmitEllipse("hit_spark", pos, { 0.0f, 1.0f, 0.0f }, kBoltCoreColor,
            kVerticalBoltLifetime, kVerticalBoltThickness, kVerticalBoltLength * scale);
        ctx.particles.EmitRing("hit_ring", pos, kBoltRingSpeed, color, kBoltRingCount, kBoltRingLifetime, kBoltRingSize);
    }

    void EmitAura(const ElementEffectContext& ctx, const Vector3& pos, const Vector4& color) const override
    {
        // 細い放電が一瞬だけ走る
        constexpr float kSparkLifetime = 0.06f;
        constexpr float kSparkLength = 0.6f;
        constexpr float kSparkThickness = 0.05f;
        if (CoinFlip(ctx.rng)) { // 毎フレームだとうるさいので約半分のフレームだけ
            ctx.particles.EmitEllipse("hit_spark", pos, { 0.0f, 0.0f, 0.0f }, color, kSparkLifetime, kSparkLength, kSparkThickness);
        }
    }
};

/** @brief 氷: 扇状に飛ぶ破片と薄い冷気の輪 */
class IceElementEffect final : public IElementEffect {
public:
    void EmitHit(const ElementEffectContext& ctx, const Vector3& pos, const Vector4& color, float scale) const override
    {
        constexpr int kShardCount = 12;
        constexpr int kShardColumns = 7;
        constexpr int kShardCenterColumn = 3;
        constexpr float kShardColumnSpacing = 0.75f;
        constexpr float kShardRiseBase = 2.0f;
        constexpr int kShardRiseSteps = 4;
        constexpr float kShardRiseStep = 0.8f;
        constexpr float kShardLifetime = 0.65f;
        constexpr float kShardSize = 0.14f;
        constexpr float kFrostRingSpeed = 1.3f;
        constexpr Vector4 kFrostRingColor = { 0.65f, 0.95f, 1.0f, 0.75f };
        constexpr int kFrostRingCount = 20;
        constexpr float kFrostRingLifetime = 0.5f;
        constexpr float kFrostRingSize = 0.12f;
        for (int i = 0; i < kShardCount; ++i) {
            const float vx = static_cast<float>((i % kShardColumns) - kShardCenterColumn) * kShardColumnSpacing;
            ctx.particles.EmitGravity("hit_spark", pos, { vx, kShardRiseBase + (i % kShardRiseSteps) * kShardRiseStep, 0.0f },
                color, kShardLifetime, kShardSize * scale);
        }
        ctx.particles.EmitRing("hit_ring", pos, kFrostRingSpeed, kFrostRingColor, kFrostRingCount, kFrostRingLifetime, kFrostRingSize);
    }

    void EmitAura(const ElementEffectContext& ctx, const Vector3& pos, const Vector4&) const override
    {
        // 冷気の粒がゆっくり沈む
        constexpr Vector4 kFrostColor = { 0.8f, 0.97f, 1.0f, 0.85f };
        constexpr float kFrostDriftX = 0.3f;
        constexpr float kFrostFall = -0.5f;
        constexpr float kFrostLifetime = 0.5f;
        constexpr float kFrostSize = 0.09f;
        ctx.particles.EmitWithColor("hit_spark", pos, { RandomUnit(ctx.rng) * kFrostDriftX, kFrostFall, 0.0f },
            kFrostColor, kFrostLifetime, kFrostSize);
    }
};

/** @brief 重力: 密度の違う同心円と明滅する重い核 */
class GravityElementEffect final : public IElementEffect {
public:
    void EmitHit(const ElementEffectContext& ctx, const Vector3& pos, const Vector4& color, float scale) const override
    {
        constexpr float kInnerRingSpeed = 0.8f;
        constexpr int kInnerRingCount = 24;
        constexpr float kInnerRingLifetime = 0.55f;
        constexpr float kInnerRingSize = 0.28f;
        constexpr float kOuterRingSpeed = 2.0f;
        constexpr Vector4 kOuterRingColor = { 0.35f, 0.08f, 0.6f, 0.8f };
        constexpr int kOuterRingCount = 18;
        constexpr float kOuterRingLifetime = 0.38f;
        constexpr float kOuterRingSize = 0.18f;
        constexpr Vector4 kCoreColor = { 0.95f, 0.7f, 1.0f, 1.0f };
        constexpr float kCoreLifetime = 0.35f;
        constexpr float kCoreSize = 0.8f;
        ctx.particles.EmitRing("hit_ring", pos, kInnerRingSpeed, color, kInnerRingCount, kInnerRingLifetime, kInnerRingSize * scale);
        ctx.particles.EmitRing("hit_ring", pos, kOuterRingSpeed, kOuterRingColor, kOuterRingCount, kOuterRingLifetime, kOuterRingSize);
        ctx.particles.EmitWithColor("hit_spark", pos, { 0.0f, 0.0f, 0.0f },
            kCoreColor, kCoreLifetime, kCoreSize * scale, true);
    }

    void EmitAura(const ElementEffectContext& ctx, const Vector3& pos, const Vector4& color) const override
    {
        // 武器へ吸い込まれる重い粒
        constexpr float kPullRadius = 0.8f;
        constexpr float kPullSpeed = 2.0f;
        constexpr float kPullLifetime = 0.3f;
        constexpr float kPullSize = 0.12f;
        const Vector3 offset = { RandomUnit(ctx.rng) * kPullRadius, RandomUnit(ctx.rng) * kPullRadius, 0.0f };
        ctx.particles.EmitWithColor("hit_spark", { pos.x + offset.x, pos.y + offset.y, pos.z },
            { -offset.x * kPullSpeed, -offset.y * kPullSpeed, 0.0f }, color, kPullLifetime, kPullSize);
    }
};

/** @brief 血: 深紅の斬線と重い飛沫 */
class BloodElementEffect final : public IElementEffect {
public:
    void EmitHit(const ElementEffectContext& ctx, const Vector3& pos, const Vector4& color, float scale) const override
    {
        constexpr float kSlashAngleRight = 0.45f;
        constexpr float kSlashAngleLeft = 2.69f;
        constexpr float kSlashRadius = 1.8f;
        constexpr int kSplatterCount = 9;
        constexpr float kSplatterSpeedBase = 0.5f;
        constexpr float kSplatterSpeedStep = 0.25f;
        constexpr float kSplatterRiseBase = 1.0f;
        constexpr int kSplatterRiseSteps = 4;
        constexpr float kSplatterRiseStep = 0.7f;
        constexpr float kSplatterLifetime = 0.55f;
        constexpr float kSplatterSize = 0.2f;
        const float dir = ctx.facingX;
        ctx.particles.EmitSlash("sword_slash", pos, dir > 0.0f ? kSlashAngleRight : kSlashAngleLeft, color, kSlashRadius * scale);
        for (int i = 0; i < kSplatterCount; ++i) {
            ctx.particles.EmitGravity("hit_spark", pos,
                { -dir * (kSplatterSpeedBase + i * kSplatterSpeedStep), kSplatterRiseBase + (i % kSplatterRiseSteps) * kSplatterRiseStep, 0.0f },
                color, kSplatterLifetime, kSplatterSize);
        }
    }

    void EmitAura(const ElementEffectContext& ctx, const Vector3& pos, const Vector4& color) const override
    {
        // 血の滴が刃から垂れる
        constexpr float kDripSpeedX = 0.6f;
        constexpr float kDripLifetime = 0.5f;
        constexpr float kDripSize = 0.1f;
        ctx.particles.EmitGravity("hit_spark", pos, { RandomUnit(ctx.rng) * kDripSpeedX, 0.0f, 0.0f },
            color, kDripLifetime, kDripSize);
    }
};

/** @brief 虚無: 暗紫の二重リングと不規則に明滅する粒子 */
class VoidElementEffect final : public IElementEffect {
public:
    void EmitHit(const ElementEffectContext& ctx, const Vector3& pos, const Vector4& color, float scale) const override
    {
        constexpr float kInnerRingSpeed = 1.4f;
        constexpr int kInnerRingCount = 28;
        constexpr float kInnerRingLifetime = 0.65f;
        constexpr float kInnerRingSize = 0.2f;
        constexpr float kOuterRingSpeed = 3.0f;
        constexpr int kOuterRingCount = 16;
        constexpr float kOuterRingLifetime = 0.3f;
        constexpr float kOuterRingSize = 0.15f;
        constexpr int kMoteCount = 7;
        constexpr int kMoteColumnsX = 3;
        constexpr int kMoteColumnsY = 4;
        constexpr float kMoteSpeedX = 1.4f;
        constexpr float kMoteSpeedY = 1.1f;
        constexpr float kMoteLifetime = 0.45f;
        constexpr float kMoteSize = 0.2f;
        ctx.particles.EmitRing("hit_ring", pos, kInnerRingSpeed, color, kInnerRingCount, kInnerRingLifetime, kInnerRingSize * scale);
        ctx.particles.EmitRing("hit_ring", pos, kOuterRingSpeed, kShadowColor, kOuterRingCount, kOuterRingLifetime, kOuterRingSize);
        for (int i = 0; i < kMoteCount; ++i) {
            ctx.particles.EmitWithColor("hit_spark", pos,
                { static_cast<float>((i % kMoteColumnsX) - 1) * kMoteSpeedX, static_cast<float>((i % kMoteColumnsY) - 1) * kMoteSpeedY, 0.0f },
                color, kMoteLifetime, kMoteSize, true);
        }
    }

    void EmitAura(const ElementEffectContext& ctx, const Vector3& pos, const Vector4& color) const override
    {
        // 暗い粒が明滅しながら漂う
        constexpr float kMoteDrift = 0.4f;
        constexpr float kMoteLifetime = 0.45f;
        constexpr float kMoteSize = 0.13f;
        ctx.particles.EmitWithColor("hit_spark", pos, { RandomUnit(ctx.rng) * kMoteDrift, RandomUnit(ctx.rng) * kMoteDrift, 0.0f },
            CoinFlip(ctx.rng) ? color : kShadowColor, kMoteLifetime, kMoteSize, true);
    }

private:
    static constexpr Vector4 kShadowColor = { 0.12f, 0.02f, 0.24f, 0.9f };
};

/** @brief 風: 交差する風刃と外へ抜ける軽い渦 */
class WindElementEffect final : public IElementEffect {
public:
    void EmitHit(const ElementEffectContext& ctx, const Vector3& pos, const Vector4& color, float scale) const override
    {
        constexpr float kBladeAngle = 0.55f;
        constexpr float kMainBladeRadius = 2.0f;
        constexpr Vector4 kSubBladeColor = { 0.75f, 1.0f, 0.65f, 0.8f };
        constexpr float kSubBladeRadius = 1.7f;
        constexpr float kVortexRingSpeed = 3.8f;
        constexpr int kVortexRingCount = 18;
        constexpr float kVortexRingLifetime = 0.28f;
        constexpr float kVortexRingSize = 0.1f;
        ctx.particles.EmitSlash("sword_slash", pos, kBladeAngle, color, kMainBladeRadius * scale);
        ctx.particles.EmitSlash("sword_slash", pos, -kBladeAngle, kSubBladeColor, kSubBladeRadius * scale);
        ctx.particles.EmitRing("hit_ring", pos, kVortexRingSpeed, color, kVortexRingCount, kVortexRingLifetime, kVortexRingSize);
    }

    void EmitAura(const ElementEffectContext& ctx, const Vector3& pos, const Vector4& color) const override
    {
        // 振りの向きへ抜ける風の筋
        constexpr float kStreakSpeed = 3.0f;
        constexpr float kStreakLifetime = 0.15f;
        constexpr float kStreakLength = 0.5f;
        constexpr float kStreakThickness = 0.04f;
        ctx.particles.EmitEllipse("hit_spark", pos, { ctx.facingX * kStreakSpeed, 0.0f, 0.0f }, color,
            kStreakLifetime, kStreakLength, kStreakThickness);
    }
};

/** @brief 属性名からエフェクトを引く表を作る */
std::unordered_map<std::string, std::unique_ptr<IElementEffect>> BuildRegistry()
{
    std::unordered_map<std::string, std::unique_ptr<IElementEffect>> registry;
    registry.emplace("Water", std::make_unique<WaterElementEffect>());
    registry.emplace("Fire", std::make_unique<FireElementEffect>());
    registry.emplace("Lightning", std::make_unique<LightningElementEffect>());
    registry.emplace("Ice", std::make_unique<IceElementEffect>());
    registry.emplace("Gravity", std::make_unique<GravityElementEffect>());
    registry.emplace("Blood", std::make_unique<BloodElementEffect>());
    registry.emplace("Void", std::make_unique<VoidElementEffect>());
    registry.emplace("Wind", std::make_unique<WindElementEffect>());
    return registry;
}

} // namespace

const IElementEffect* IElementEffect::Find(const std::string& element)
{
    static const auto registry = BuildRegistry();
    const auto it = registry.find(element);
    return it != registry.end() ? it->second.get() : nullptr;
}
