#include "FallingFloorBehavior.h"
#include "ParticleManager.h"
#include <algorithm>
#include <cmath>
#include <random>
using namespace engine::graphics;
namespace engine::game {

static constexpr float kFallFloorWarningEnd = 0.6f;
static constexpr float kFallFloorRespawnStart = 3.5f;
static constexpr float kFallFloorCycleEnd = 4.0f;
static constexpr float kFallFloorCrackLeadTime = 0.28f; // 崩落の何秒前からひび割れの砂ぼこりを出すか
static constexpr float kMinMotionSpeed = 0.01f; // 速度0で周期が止まらないようにする下限

// 砂ぼこりの演出
static constexpr Vector4 kDustColor = { 0.8f, 0.72f, 0.56f, 0.75f };
// 崩落の瞬間
static constexpr float kCollapseDebrisSpreadX = 1.8f;
static constexpr float kCollapseDebrisRiseMin = 0.6f;
static constexpr float kCollapseDebrisRiseMax = 2.0f;
static constexpr int kCollapseDebrisCount = 14;
static constexpr float kCollapseDebrisLifetime = 0.55f;
static constexpr float kCollapseDebrisSizeMin = 0.14f;
static constexpr float kCollapseDebrisSizeRange = 0.1f;
static constexpr float kCollapseRingSpeed = 1.2f;
static constexpr float kCollapseRingAlpha = 0.5f;
static constexpr int kCollapseRingCount = 14;
static constexpr float kCollapseRingLifetime = 0.5f;
static constexpr float kCollapseRingSize = 0.2f;
// 復帰の瞬間
static constexpr int kRespawnDustCount = 6;
static constexpr float kRespawnDustRiseMin = 0.2f;
static constexpr float kRespawnDustRiseRange = 0.2f;
static constexpr float kRespawnDustLifetime = 0.3f;
static constexpr float kRespawnDustSize = 0.12f;
// 崩落直前のひび割れ
static constexpr float kCrackDustChance = 0.35f; // 1フレームあたりの噴出確率
static constexpr float kCrackDustSurfaceOffset = 0.02f;
static constexpr float kCrackDustRiseMin = 0.3f;
static constexpr float kCrackDustRiseRange = 0.3f;
static constexpr float kCrackDustLifetime = 0.35f;
static constexpr float kCrackDustSizeMin = 0.08f;
static constexpr float kCrackDustSizeRange = 0.06f;


// 「fall」の仕掛けは落下後に一定時間消え、元の位置へ戻る動作を繰り返す。
FallingFloorBehavior::Snapshot FallingFloorBehavior::Evaluate(float runtimeTimer, float motionSpeed, float motionAmount)
{
    const float speed = (std::max)(motionSpeed, kMinMotionSpeed);
    const float phase = std::fmod(runtimeTimer * speed, kFallFloorCycleEnd);
    (void)motionAmount;

    Snapshot state;
    state.phase = phase;
    if (phase < kFallFloorWarningEnd) {
        return state;
    }
    if (phase < kFallFloorRespawnStart) {
        // 床だけが高速で下降すると、通常重力のプレイヤーが空中に取り残される。
        // 壊れる床はその場で消し、同じフレームから当たり判定も無効にする。
        state.visible = false;
        state.solid = false;
        return state;
    }
    // 復帰時も途中の高さに当たり判定を出さず、元の位置へ直接戻す。
    return state;
}

static std::mt19937& FallFloorDebrisRng()
{
    static std::mt19937 rng { std::random_device {}() };
    return rng;
}

// 崩落ギミックの床に、崩れる直前のひび割れ粉塵と、崩落/復帰の瞬間の土煙をまとめて出す
// （床が消えるだけだと壊れる床だと気付きにくいので、視覚的な予告と崩落感を補う）
void FallingFloorBehavior::EmitDebris(ParticleManager* pm, const Vector3& worldPos, const Vector3& halfExtent,
    const Snapshot& state, bool& wasSolid)
{
    if (!pm) {
        wasSolid = state.solid;
        return;
    }
    auto& rng = FallFloorDebrisRng();
    std::uniform_real_distribution<float> ox(-halfExtent.x, halfExtent.x);
    std::uniform_real_distribution<float> oz(-halfExtent.z, halfExtent.z);
    std::uniform_real_distribution<float> chance(0.0f, 1.0f);

    if (wasSolid && !state.solid) {
        // 崩落の瞬間：破片が飛び散り、土煙が広がる
        std::uniform_real_distribution<float> vx(-kCollapseDebrisSpreadX, kCollapseDebrisSpreadX);
        std::uniform_real_distribution<float> vy(kCollapseDebrisRiseMin, kCollapseDebrisRiseMax);
        for (int i = 0; i < kCollapseDebrisCount; ++i) {
            const Vector3 p = { worldPos.x + ox(rng), worldPos.y + halfExtent.y, worldPos.z + oz(rng) };
            pm->EmitGravity("land_dust", p, { vx(rng), vy(rng), 0.0f },
                kDustColor, kCollapseDebrisLifetime, kCollapseDebrisSizeMin + chance(rng) * kCollapseDebrisSizeRange);
        }
        pm->EmitRing("land_dust", worldPos + Vector3 { 0.0f, halfExtent.y, 0.0f },
            kCollapseRingSpeed, { kDustColor.x, kDustColor.y, kDustColor.z, kCollapseRingAlpha },
            kCollapseRingCount, kCollapseRingLifetime, kCollapseRingSize);
    } else if (!wasSolid && state.solid) {
        // 復帰の瞬間：着地のような軽い土煙
        for (int i = 0; i < kRespawnDustCount; ++i) {
            const Vector3 p = { worldPos.x + ox(rng), worldPos.y + halfExtent.y, worldPos.z + oz(rng) };
            pm->EmitGravity("land_dust", p, { 0.0f, kRespawnDustRiseMin + chance(rng) * kRespawnDustRiseRange, 0.0f },
                kDustColor, kRespawnDustLifetime, kRespawnDustSize);
        }
    } else if (state.solid && state.phase >= kFallFloorWarningEnd - kFallFloorCrackLeadTime
        && state.phase < kFallFloorWarningEnd && chance(rng) < kCrackDustChance) {
        // 崩落直前：ひびの隙間から砂ぼこりが間欠的に噴く（崩れる床だと気付かせる警告演出）
        const Vector3 p = { worldPos.x + ox(rng), worldPos.y + halfExtent.y + kCrackDustSurfaceOffset, worldPos.z + oz(rng) };
        pm->EmitGravity("land_dust", p, { 0.0f, kCrackDustRiseMin + chance(rng) * kCrackDustRiseRange, 0.0f },
            kDustColor, kCrackDustLifetime, kCrackDustSizeMin + chance(rng) * kCrackDustSizeRange);
    }
    wasSolid = state.solid;
}

}
