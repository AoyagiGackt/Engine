/** @file FallingFloorBehavior.cpp
 * @brief 崩れる床の時間設定と演出レシピに従って表示・衝突・粒子を評価する
 */
#include "FallingFloorBehavior.h"
#include "CombatTuning.h"
#include "JsonFieldTable.h"
#include "JsonHelper.h"
#include "HitEffectLibrary.h"
#include "ParticleManager.h"
#include <algorithm>
#include <cmath>
#include <random>
using namespace engine::graphics;
namespace engine::game {

namespace {
/** @brief 崩れる床から出る破片・粉塵・リングの演出レシピ */
struct FallingFloorEffects {
    Vector4 dustColor = { 0.8f, 0.72f, 0.56f, 0.75f };
    float collapseDebrisSpreadX = 1.8f;
    float collapseDebrisRiseMin = 0.6f;
    float collapseDebrisRiseMax = 2.0f;
    int collapseDebrisCount = 14;
    float collapseDebrisLifetime = 0.55f;
    float collapseDebrisSizeMin = 0.14f;
    float collapseDebrisSizeRange = 0.1f;
    float collapseRingSpeed = 1.2f;
    float collapseRingAlpha = 0.5f;
    int collapseRingCount = 14;
    float collapseRingLifetime = 0.5f;
    float collapseRingSize = 0.2f;
    int respawnDustCount = 6;
    float respawnDustRiseMin = 0.2f;
    float respawnDustRiseRange = 0.2f;
    float respawnDustLifetime = 0.3f;
    float respawnDustSize = 0.12f;
    float crackDustChance = 0.35f;
    float crackDustSurfaceOffset = 0.02f;
    float crackDustRiseMin = 0.3f;
    float crackDustRiseRange = 0.3f;
    float crackDustLifetime = 0.35f;
    float crackDustSizeMin = 0.08f;
    float crackDustSizeRange = 0.06f;
};

/** @brief hit_effects.jsonから床の演出レシピを初回に読み込む */
const FallingFloorEffects& FloorEffects() {
    static const FallingFloorEffects effects = [] {
        FallingFloorEffects result;
        // hit_effects.json の fallingFloor 節の項目名と書き込み先メンバの対応表（初回だけ読む）
        constexpr JsonField<FallingFloorEffects> kFields[] = {
            { "fallingFloor", "dustColor", &FallingFloorEffects::dustColor },
            { "fallingFloor", "collapseDebrisSpreadX", &FallingFloorEffects::collapseDebrisSpreadX },
            { "fallingFloor", "collapseDebrisRiseMin", &FallingFloorEffects::collapseDebrisRiseMin },
            { "fallingFloor", "collapseDebrisRiseMax", &FallingFloorEffects::collapseDebrisRiseMax },
            { "fallingFloor", "collapseDebrisCount", &FallingFloorEffects::collapseDebrisCount },
            { "fallingFloor", "collapseDebrisLifetime", &FallingFloorEffects::collapseDebrisLifetime },
            { "fallingFloor", "collapseDebrisSizeMin", &FallingFloorEffects::collapseDebrisSizeMin },
            { "fallingFloor", "collapseDebrisSizeRange", &FallingFloorEffects::collapseDebrisSizeRange },
            { "fallingFloor", "collapseRingSpeed", &FallingFloorEffects::collapseRingSpeed },
            { "fallingFloor", "collapseRingAlpha", &FallingFloorEffects::collapseRingAlpha },
            { "fallingFloor", "collapseRingCount", &FallingFloorEffects::collapseRingCount },
            { "fallingFloor", "collapseRingLifetime", &FallingFloorEffects::collapseRingLifetime },
            { "fallingFloor", "collapseRingSize", &FallingFloorEffects::collapseRingSize },
            { "fallingFloor", "respawnDustCount", &FallingFloorEffects::respawnDustCount },
            { "fallingFloor", "respawnDustRiseMin", &FallingFloorEffects::respawnDustRiseMin },
            { "fallingFloor", "respawnDustRiseRange", &FallingFloorEffects::respawnDustRiseRange },
            { "fallingFloor", "respawnDustLifetime", &FallingFloorEffects::respawnDustLifetime },
            { "fallingFloor", "respawnDustSize", &FallingFloorEffects::respawnDustSize },
            { "fallingFloor", "crackDustChance", &FallingFloorEffects::crackDustChance },
            { "fallingFloor", "crackDustSurfaceOffset", &FallingFloorEffects::crackDustSurfaceOffset },
            { "fallingFloor", "crackDustRiseMin", &FallingFloorEffects::crackDustRiseMin },
            { "fallingFloor", "crackDustRiseRange", &FallingFloorEffects::crackDustRiseRange },
            { "fallingFloor", "crackDustLifetime", &FallingFloorEffects::crackDustLifetime },
            { "fallingFloor", "crackDustSizeMin", &FallingFloorEffects::crackDustSizeMin },
            { "fallingFloor", "crackDustSizeRange", &FallingFloorEffects::crackDustSizeRange },
        };
        ReadJsonFields(JsonHelper::Load(HitEffectLibrary::kConfigPath), kFields, result);
        return result;
    }();
    return effects;
}
} // namespace

/** @brief 周期内の時刻から床の表示と衝突の有効状態を評価する */
FallingFloorBehavior::Snapshot FallingFloorBehavior::Evaluate(float runtimeTimer, float motionSpeed, float motionAmount) {
    const FallingFloorTiming timing = CombatTuning::GetInstance()->FloorTiming();
    const float speed = (std::max)(motionSpeed, timing.minimumSpeed);
    const float phase = std::fmod(runtimeTimer * speed, timing.cycleEnd);
    (void)motionAmount;

    Snapshot state;
    state.phase = phase;
    if (phase < timing.warningEnd) {
        return state;
    }
    if (phase < timing.respawnStart) {
        // 床だけが高速で下降すると、通常重力のプレイヤーが空中に取り残される。
        // 壊れる床はその場で消し、同じフレームから当たり判定も無効にする。
        state.visible = false;
        state.solid = false;
        return state;
    }
    // 復帰時も途中の高さに当たり判定を出さず、元の位置へ直接戻す。
    return state;
}

/** @brief 落ちる床の破片に使う乱数生成器を返す */
static std::mt19937& FallFloorDebrisRng() {
    static std::mt19937 sRng { std::random_device {}() };
    return sRng;
}

// 崩落ギミックの床に、崩れる直前のひび割れ粉塵と、崩落/復帰の瞬間の土煙をまとめて出す
// （床が消えるだけだと壊れる床だと気付きにくいので、視覚的な予告と崩落感を補う）
void FallingFloorBehavior::EmitDebris(ParticleManager* pm, const Vector3& worldPos, const Vector3& halfExtent,
    const Snapshot& state, bool& wasSolid) {
    // パーティクルを出せない時も、崩れた/戻った の切り替わりだけは覚えておく
    if (!pm) {
        wasSolid = state.solid;
        return;
    }
    const FallingFloorTiming timing = CombatTuning::GetInstance()->FloorTiming();
    const FallingFloorEffects& effects = FloorEffects();
    auto& rng = FallFloorDebrisRng();
    // 土煙は床の上面のどこかからランダムに出す
    std::uniform_real_distribution<float> ox(-halfExtent.x, halfExtent.x);
    std::uniform_real_distribution<float> oz(-halfExtent.z, halfExtent.z);
    std::uniform_real_distribution<float> chance(0.0f, 1.0f);

    if (wasSolid && !state.solid) {
        // 崩落の瞬間：破片が飛び散り、土煙が広がる
        std::uniform_real_distribution<float> vx(-effects.collapseDebrisSpreadX, effects.collapseDebrisSpreadX);
        std::uniform_real_distribution<float> vy(effects.collapseDebrisRiseMin, effects.collapseDebrisRiseMax);
        for (int i = 0; i < effects.collapseDebrisCount; ++i) {
            const Vector3 p = { worldPos.x + ox(rng), worldPos.y + halfExtent.y, worldPos.z + oz(rng) };
            pm->EmitGravity("land_dust", p, { vx(rng), vy(rng), 0.0f },
                effects.dustColor, effects.collapseDebrisLifetime, effects.collapseDebrisSizeMin + chance(rng) * effects.collapseDebrisSizeRange);
        }
        pm->EmitRing("land_dust", worldPos + Vector3 { 0.0f, halfExtent.y, 0.0f },
            effects.collapseRingSpeed, { effects.dustColor.x, effects.dustColor.y, effects.dustColor.z, effects.collapseRingAlpha },
            effects.collapseRingCount, effects.collapseRingLifetime, effects.collapseRingSize);
    } else if (!wasSolid && state.solid) {
        // 復帰の瞬間：着地のような軽い土煙
        for (int i = 0; i < effects.respawnDustCount; ++i) {
            const Vector3 p = { worldPos.x + ox(rng), worldPos.y + halfExtent.y, worldPos.z + oz(rng) };
            pm->EmitGravity("land_dust", p, { 0.0f, effects.respawnDustRiseMin + chance(rng) * effects.respawnDustRiseRange, 0.0f },
                effects.dustColor, effects.respawnDustLifetime, effects.respawnDustSize);
        }
    } else if (state.solid && state.phase >= timing.warningEnd - timing.crackLeadTime
        && state.phase < timing.warningEnd && chance(rng) < effects.crackDustChance) {
        // 崩落直前：ひびの隙間から砂ぼこりが間欠的に噴く（崩れる床だと気付かせる警告演出）
        const Vector3 p = { worldPos.x + ox(rng), worldPos.y + halfExtent.y + effects.crackDustSurfaceOffset, worldPos.z + oz(rng) };
        pm->EmitGravity("land_dust", p, { 0.0f, effects.crackDustRiseMin + chance(rng) * effects.crackDustRiseRange, 0.0f },
            effects.dustColor, effects.crackDustLifetime, effects.crackDustSizeMin + chance(rng) * effects.crackDustSizeRange);
    }
    // 次のフレームで切り替わりを見分けるため、今の状態を覚えておく
    wasSolid = state.solid;
}

}
