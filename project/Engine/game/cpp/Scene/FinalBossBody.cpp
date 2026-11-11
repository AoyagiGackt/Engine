/**
 * @file FinalBossBody.cpp
 * @brief ラスボスの体の補助（立てる範囲・向き・距離・攻撃の判定）と、撃ち出す剣の動き
 * @note 技の中身は FinalBossMoves.cpp に置く
 */
#include "FinalBossBattle.h"
#include "FinalBossTuning.h"
#include "HitEffectLibrary.h"
#include "Player.h"
#include <algorithm>
#include <cmath>

namespace engine::game {
namespace {
constexpr float kStrikeHalfDepth = 0.5f; ///< 攻撃の判定の奥行きの半分（プレイヤーと同じ z=0 の線の上）
constexpr float kPhantomBackOffset = 0.9f; ///< 剣を並べる、ボスの背後への距離
constexpr float kArcEdgeScale = 2.0f; ///< 並びの端（u=±0.5）で持ち上げを0にする倍率
constexpr float kPhantomArc = 0.6f; ///< 並べた剣の真ん中を持ち上げる高さ
constexpr float kPhantomAimHeight = 0.2f; ///< 狙うプレイヤーの体の高さ

/** @brief 調整値の短い呼び名 */
const FinalBossTuning& Tune() {
    return FinalBossTuningStore::Get();
}
} // namespace

// ══════════════════════════════════════════════════════
// ボスの体の補助
// ══════════════════════════════════════════════════════

/** @brief ボスの X を闘技場の端で止める */
float FinalBossBattle::ClampBossX(float x) const {
    float minX = 0.0f;
    float maxX = 0.0f;
    ArenaLimits(minX, maxX);
    return std::clamp(x, minX, maxX);
}

/** @brief プレイヤーへ向く向き（+1/-1） */
float FinalBossBattle::DirectionToPlayer() const {
    return links_.player->GetPosition().x >= bossX_ ? 1.0f : -1.0f;
}

/** @brief プレイヤーとボスの距離（X 軸の上） */
float FinalBossBattle::DistanceToPlayer() const {
    return std::abs(links_.player->GetPosition().x - bossX_);
}

/** @brief 後半戦の速さの倍率 */
float FinalBossBattle::SpeedMult() const {
    return rageStarted_ ? Tune().rageSpeedMult : 1.0f;
}

/** @brief ボスの攻撃を当てる（範囲の X は from〜to、高さは centerY の上下） */
bool FinalBossBattle::StrikeAlong(float fromX, float toX, float centerY, int damage) {
    const float halfHeight = Tune().hitHalfHeight;
    const AABB box = { { (std::min)(fromX, toX), centerY - halfHeight, -kStrikeHalfDepth },
        { (std::max)(fromX, toX), centerY + halfHeight, kStrikeHalfDepth } };
    const Vector3 effectPosition = { (fromX + toX) * 0.5f, centerY, 0.0f };
    return links_.strikePlayer && links_.strikePlayer(box, effectPosition, damage);
}


// ══════════════════════════════════════════════════════
// 撃ち出す剣
// ══════════════════════════════════════════════════════

/** @brief 撃ち出す剣を並べる（ボスの背後の上に弧を描いて並べ、少しずつ間を空けて撃つ） */
void FinalBossBattle::SummonPhantoms(int count) {
    const FinalBossTuning& t = Tune();
    const int usable = (std::min)(count, static_cast<int>(phantoms_.size()));
    const HitEffectPlayer fx(links_.particles, links_.shaker, links_.rng);
    for (int i = 0; i < usable; ++i) {
        // 並びの中での位置（-0.5〜0.5）。横（奥行き）へ広げ、真ん中を高くする
        const float u = usable > 1 ? static_cast<float>(i) / static_cast<float>(usable - 1) - 0.5f : 0.0f;
        PhantomBlade& blade = phantoms_[static_cast<size_t>(i)];
        blade.position = { bossX_ - bossFacing_ * kPhantomBackOffset,
            t.shadowBaseY + t.phantomHeight + (1.0f - std::abs(u) * kArcEdgeScale) * kPhantomArc,
            u * t.phantomSpread };
        blade.velocity = { };
        blade.delay = t.phantomSummonSeconds + static_cast<float>(i) * t.phantomInterval;
        blade.life = t.phantomLifetime;
        blade.active = true;
        blade.launched = false;
        blade.object->SetPosition(blade.position);
        fx.Ring("lastboss_phantom", blade.position);
    }
}

/** @brief 並べた剣を進め、撃ち出し、プレイヤーへ当てる */
void FinalBossBattle::UpdatePhantoms(float deltaTime) {
    const FinalBossTuning& t = Tune();
    const Vector3 player = links_.player->GetPosition();
    const Vector3 aim = { player.x, player.y + kPhantomAimHeight, 0.0f };
    const HitEffectPlayer fx(links_.particles, links_.shaker, links_.rng);
    // まだ撃っていない剣があれば、狙っているプレイヤーの足元に印を出す
    const bool aiming = std::any_of(phantoms_.begin(), phantoms_.end(), [](const PhantomBlade& blade) {
        return blade.active && !blade.launched;
    });
    if (aiming) {
        telegraph_.Circle(player.x, FloorY(), t.telegraphAimRadius, TelegraphColor());
    }
    for (PhantomBlade& blade : phantoms_) {
        if (!blade.active) {
            continue;
        }
        if (!blade.launched) {
            // 並んでいる間はプレイヤーへ切っ先を向けて待ち、順番が来たら撃ち出す
            blade.delay -= deltaTime;
            if (blade.delay <= 0.0f) {
                blade.launched = true;
                const Vector3 toAim = { aim.x - blade.position.x, aim.y - blade.position.y, aim.z - blade.position.z };
                const float length = std::sqrt(toAim.x * toAim.x + toAim.y * toAim.y + toAim.z * toAim.z);
                blade.velocity = length > 0.0f ? toAim * (t.phantomSpeed / length) : Vector3 { };
                fx.Ring("lastboss_phantom_fire", blade.position);
            }
        } else {
            blade.position = blade.position + blade.velocity * deltaTime;
            blade.life -= deltaTime;
            // プレイヤーと同じ線の上へ来たら当たり判定を取る
            const float half = t.phantomHalfSize;
            const AABB box = { { blade.position.x - half, blade.position.y - half, -kStrikeHalfDepth },
                { blade.position.x + half, blade.position.y + half, kStrikeHalfDepth } };
            const bool nearLine = std::abs(blade.position.z) <= kStrikeHalfDepth;
            if (nearLine && links_.strikePlayer && links_.strikePlayer(box, blade.position, t.phantomDamage)) {
                fx.Sparks("lastboss_phantom_hit", blade.position);
                blade.active = false;
            } else if (blade.life <= 0.0f) {
                blade.active = false;
            }
        }
        // 切っ先（刃の +Y）を飛ぶ向き、並んでいる間はプレイヤーへ向ける
        const Vector3 heading = blade.launched ? blade.velocity
            : Vector3 { aim.x - blade.position.x, aim.y - blade.position.y, 0.0f };
        blade.object->SetPosition(blade.position);
        blade.object->SetRotation({ 0.0f, 0.0f, std::atan2(-heading.x, heading.y) });
    }
}

/** @brief 予兆の印の色（調整値の色を時計で明滅させる） */
Vector4 FinalBossBattle::TelegraphColor() const {
    const FinalBossTuning& t = Tune();
    const float pulse = 1.0f - t.telegraphPulseDepth * (0.5f + 0.5f * std::sin(telegraphClock_ * t.telegraphPulseSpeed));
    return { t.telegraphColor.x, t.telegraphColor.y, t.telegraphColor.z, t.telegraphColor.w * pulse };
}

/** @brief 並べた剣がすべて消えたか */
bool FinalBossBattle::PhantomsCleared() const {
    return std::none_of(phantoms_.begin(), phantoms_.end(), [](const PhantomBlade& blade) {
        return blade.active;
    });
}

} // namespace engine::game
