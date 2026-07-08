#include "MeleeCombo.h"
#include "Easing.h"
#include "Weapon.h"
#include <algorithm>
using namespace engine;
using namespace engine::game;

// ============================================================
//  コンボ定義テーブル
//  DMCを参考に「素早い初段→重い締め」「武器ごとに役割が違う」を意識した配分:
//   Sword      … バランス4段。何でもできる基準武器
//   Dagger     … 超高速5段。1発は軽いが手数と前進で押し切る
//   Hammer     … 溜めて2段。発生は遅いが吹き飛ばしが桁違い、空中は叩き落とし
//   Spear      … リーチ突き3段。距離を保ったまま刺す
//   Greatsword … 超重量3段。全部に長いヒットストップ、締めは特大
//   Scythe     … 広範囲3段。打ち上げ性能が全武器最強の空中コンボ武器
//   Axe        … 荒々しい3段。締めの大車輪で纏めて吹き飛ばす
// ============================================================
namespace {

constexpr float kDeg = 3.14159265f / 180.0f;

// ── Sword（カタナ）: 袈裟斬り→返し→突き→回転斬り ──────────────────
constexpr MeleeAttackDef kSwordGround[] = {
    { "swd1", 1.0f, 0.38f, 0.14f, 0.22f, 0.55f, 1.0f,  0.16f, 0.05f, false,  4, 1.6f, true, { 0, 0,  100 * kDeg }, { 0, 0,  -80 * kDeg } },
    { "swd2", 1.0f, 0.38f, 0.13f, 0.22f, 0.55f, 1.0f,  0.16f, 0.05f, false,  4, 1.6f, true, { 0, 0, -100 * kDeg }, { 0, 0,   80 * kDeg } },
    { "swd3", 1.2f, 0.42f, 0.15f, 0.26f, 1.20f, 1.15f, 0.26f, 0.03f, false,  5, 1.7f, true, { -70 * kDeg, 0, 0 }, {  70 * kDeg, 0, 0 } },
    { "swd4", 1.8f, 0.58f, 0.20f, 0.42f, 0.70f, 1.2f,  0.45f, 0.12f, false,  8, 1.4f, true, { 0, 0,  170 * kDeg }, { 0, 0, -170 * kDeg } },
};
constexpr MeleeAttackDef kSwordAir[] = {
    { "swd_a1", 1.0f, 0.34f, 0.12f, 0.20f, 0.0f, 1.0f, 0.08f, 0.10f, false, 4, 1.7f, true, { 0, 0,  90 * kDeg }, { 0, 0, -90 * kDeg } },
    { "swd_a2", 1.3f, 0.44f, 0.15f, 0.30f, 0.0f, 1.0f, 0.20f, 0.12f, false, 6, 1.6f, true, { 0, 0, -120 * kDeg }, { 0, 0, 120 * kDeg } },
};
constexpr MeleeAttackDef kSwordLauncher =
    { "swd_lau", 1.3f, 0.50f, 0.16f, 0.34f, 0.45f, 1.1f, 0.05f, 0.34f, true, 8, 1.5f, true, { 0, 0, -130 * kDeg }, { 0, 0, 130 * kDeg } };

// ── Dagger: 高速5段、締めはすれ違い斬り ─────────────────────────────
constexpr MeleeAttackDef kDaggerGround[] = {
    { "dag1", 1.0f, 0.22f, 0.07f, 0.12f, 0.45f, 1.0f, 0.08f, 0.03f, false, 2, 2.2f, true, { 0, 0,  70 * kDeg }, { 0, 0, -60 * kDeg } },
    { "dag2", 1.0f, 0.22f, 0.07f, 0.12f, 0.45f, 1.0f, 0.08f, 0.03f, false, 2, 2.2f, true, { 0, 0, -70 * kDeg }, { 0, 0,  60 * kDeg } },
    { "dag3", 1.0f, 0.22f, 0.07f, 0.12f, 0.45f, 1.0f, 0.08f, 0.03f, false, 2, 2.2f, true, { -50 * kDeg, 0, 0 }, {  50 * kDeg, 0, 0 } },
    { "dag4", 1.0f, 0.22f, 0.07f, 0.12f, 0.45f, 1.0f, 0.08f, 0.03f, false, 2, 2.2f, true, { 0, 0,  80 * kDeg }, { 0, 0, -70 * kDeg } },
    { "dag5", 1.7f, 0.34f, 0.10f, 0.24f, 1.80f, 1.2f, 0.30f, 0.08f, false, 6, 2.0f, true, { 0, 0, -140 * kDeg }, { 0, 0, 140 * kDeg } },
};
constexpr MeleeAttackDef kDaggerAir[] = {
    { "dag_a1", 1.0f, 0.20f, 0.06f, 0.11f, 0.0f, 1.0f, 0.05f, 0.10f, false, 2, 2.3f, true, { 0, 0,  70 * kDeg }, { 0, 0, -60 * kDeg } },
    { "dag_a2", 1.0f, 0.20f, 0.06f, 0.11f, 0.0f, 1.0f, 0.05f, 0.10f, false, 2, 2.3f, true, { 0, 0, -70 * kDeg }, { 0, 0,  60 * kDeg } },
    { "dag_a3", 1.4f, 0.30f, 0.09f, 0.20f, 0.0f, 1.0f, 0.14f, 0.12f, false, 4, 2.1f, true, { -60 * kDeg, 0, 0 }, {  60 * kDeg, 0, 0 } },
};
constexpr MeleeAttackDef kDaggerLauncher =
    { "dag_lau", 1.1f, 0.36f, 0.11f, 0.24f, 0.40f, 1.0f, 0.03f, 0.27f, true, 6, 2.0f, true, { 0, 0, -110 * kDeg }, { 0, 0, 110 * kDeg } };

// ── Hammer: 溜めの2段、空中は叩き落とし ─────────────────────────────
constexpr MeleeAttackDef kHammerGround[] = {
    { "ham1", 1.0f, 0.70f, 0.32f, 0.46f, 0.50f, 1.0f, 0.30f, 0.10f, false,  8, 1.0f, false, { 0, 0, -160 * kDeg }, { 0, 0, 110 * kDeg } },
    { "ham2", 1.4f, 0.85f, 0.38f, 0.62f, 0.60f, 1.1f, 0.50f, 0.26f, false, 12, 0.9f, false, { 0, 0,  150 * kDeg }, { 0, 0, -120 * kDeg } },
};
constexpr MeleeAttackDef kHammerAir[] = {
    // 叩き落とし: knockY が負 → 浮いた敵を地面へ叩きつける
    { "ham_a1", 1.3f, 0.55f, 0.22f, 0.40f, 0.0f, 1.0f, 0.12f, -0.30f, false, 8, 1.1f, false, { 0, 0, -150 * kDeg }, { 0, 0, 130 * kDeg } },
};
constexpr MeleeAttackDef kHammerLauncher =
    { "ham_lau", 1.2f, 0.75f, 0.34f, 0.55f, 0.45f, 1.0f, 0.08f, 0.40f, true, 10, 1.0f, false, { 0, 0, 140 * kDeg }, { 0, 0, -140 * kDeg } };

// ── Spear: リーチを活かした突き3段 ──────────────────────────────────
constexpr MeleeAttackDef kSpearGround[] = {
    { "spr1", 1.0f, 0.32f, 0.11f, 0.19f, 0.70f, 1.15f, 0.14f, 0.02f, false, 3, 1.9f, true, { -80 * kDeg, 0, 0 }, {  75 * kDeg, 0, 0 } },
    { "spr2", 1.0f, 0.32f, 0.11f, 0.19f, 0.70f, 1.15f, 0.14f, 0.02f, false, 3, 1.9f, true, { -85 * kDeg, 0, 0 }, {  80 * kDeg, 0, 0 } },
    { "spr3", 1.5f, 0.50f, 0.18f, 0.34f, 0.50f, 1.30f, 0.40f, 0.10f, false, 7, 1.5f, true, { 0, 0,  160 * kDeg }, { 0, 0, -150 * kDeg } },
};
constexpr MeleeAttackDef kSpearAir[] = {
    { "spr_a1", 1.0f, 0.30f, 0.10f, 0.19f, 0.0f, 1.15f, 0.08f, 0.10f, false, 3, 1.9f, true, { -80 * kDeg, 0, 0 }, { 75 * kDeg, 0, 0 } },
    { "spr_a2", 1.3f, 0.40f, 0.13f, 0.28f, 0.0f, 1.20f, 0.18f, 0.12f, false, 5, 1.7f, true, { 0, 0, 140 * kDeg }, { 0, 0, -130 * kDeg } },
};
constexpr MeleeAttackDef kSpearLauncher =
    { "spr_lau", 1.2f, 0.48f, 0.15f, 0.32f, 0.55f, 1.15f, 0.04f, 0.30f, true, 7, 1.6f, true, { 60 * kDeg, 0, 0 }, { -110 * kDeg, 0, 0 } };

// ── Greatsword（クレイモア）: 超重量3段 ─────────────────────────────
constexpr MeleeAttackDef kGreatswordGround[] = {
    { "gsw1", 1.0f, 0.65f, 0.30f, 0.44f, 0.60f, 1.10f, 0.35f, 0.08f, false,  8, 1.0f, true, { 0, 0,  150 * kDeg }, { 0, 0, -120 * kDeg } },
    { "gsw2", 1.1f, 0.60f, 0.28f, 0.42f, 0.55f, 1.10f, 0.22f, 0.22f, false,  9, 1.0f, true, { 0, 0, -150 * kDeg }, { 0, 0,  120 * kDeg } },
    { "gsw3", 2.0f, 0.90f, 0.42f, 0.66f, 0.80f, 1.25f, 0.60f, 0.15f, false, 14, 0.9f, true, { 0, 0, -175 * kDeg }, { 0, 0,  160 * kDeg } },
};
constexpr MeleeAttackDef kGreatswordAir[] = {
    // 落下斬り: 自分ごと振り下ろす一撃
    { "gsw_a1", 1.6f, 0.60f, 0.24f, 0.44f, 0.0f, 1.15f, 0.25f, -0.20f, false, 10, 1.0f, true, { 0, 0, -160 * kDeg }, { 0, 0, 140 * kDeg } },
};
constexpr MeleeAttackDef kGreatswordLauncher =
    { "gsw_lau", 1.4f, 0.70f, 0.32f, 0.52f, 0.50f, 1.15f, 0.10f, 0.36f, true, 12, 0.95f, true, { 0, 0, -150 * kDeg }, { 0, 0, 150 * kDeg } };

// ── Scythe（鎌）: 広範囲3段、打ち上げ最強 ───────────────────────────
constexpr MeleeAttackDef kScytheGround[] = {
    { "scy1", 1.0f, 0.45f, 0.16f, 0.26f, 0.50f, 1.35f, 0.18f, 0.06f, false, 4, 1.5f, true, { 0, 0,  130 * kDeg }, { 0, 0, -110 * kDeg } },
    { "scy2", 1.0f, 0.45f, 0.15f, 0.26f, 0.50f, 1.35f, 0.18f, 0.06f, false, 4, 1.5f, true, { 0, 0, -130 * kDeg }, { 0, 0,  110 * kDeg } },
    { "scy3", 1.6f, 0.70f, 0.24f, 0.50f, 0.60f, 1.50f, 0.35f, 0.14f, false, 9, 1.3f, true, { 0, 0,  175 * kDeg }, { 0, 0, -175 * kDeg } },
};
constexpr MeleeAttackDef kScytheAir[] = {
    { "scy_a1", 1.0f, 0.38f, 0.13f, 0.22f, 0.0f, 1.35f, 0.08f, 0.11f, false, 4, 1.6f, true, { 0, 0,  120 * kDeg }, { 0, 0, -100 * kDeg } },
    { "scy_a2", 1.0f, 0.38f, 0.13f, 0.22f, 0.0f, 1.35f, 0.08f, 0.11f, false, 4, 1.6f, true, { 0, 0, -120 * kDeg }, { 0, 0,  100 * kDeg } },
    { "scy_a3", 1.5f, 0.55f, 0.18f, 0.38f, 0.0f, 1.45f, 0.22f, 0.13f, false, 7, 1.4f, true, { 0, 0,  170 * kDeg }, { 0, 0, -170 * kDeg } },
};
constexpr MeleeAttackDef kScytheLauncher =
    { "scy_lau", 1.1f, 0.42f, 0.13f, 0.28f, 0.50f, 1.30f, 0.02f, 0.42f, true, 9, 1.6f, true, { 0, 0, -140 * kDeg }, { 0, 0, 140 * kDeg } };

// ── Axe（両刃斧）: 荒々しい3段、締めは大車輪 ────────────────────────
constexpr MeleeAttackDef kAxeGround[] = {
    { "axe1", 1.0f, 0.50f, 0.20f, 0.32f, 0.80f, 1.0f, 0.25f, 0.06f, false,  6, 1.3f, true, { 0, 0,  140 * kDeg }, { 0, 0, -110 * kDeg } },
    { "axe2", 1.1f, 0.50f, 0.18f, 0.32f, 0.60f, 1.0f, 0.25f, 0.06f, false,  6, 1.3f, true, { 0, 0, -140 * kDeg }, { 0, 0,  110 * kDeg } },
    { "axe3", 1.7f, 0.80f, 0.28f, 0.56f, 0.90f, 1.2f, 0.50f, 0.12f, false, 10, 1.1f, true, { 0, 0, -180 * kDeg }, { 0, 0,  180 * kDeg } },
};
constexpr MeleeAttackDef kAxeAir[] = {
    { "axe_a1", 1.1f, 0.42f, 0.15f, 0.28f, 0.0f, 1.0f, 0.12f, 0.10f, false, 5, 1.4f, true, { 0, 0,  130 * kDeg }, { 0, 0, -110 * kDeg } },
    { "axe_a2", 1.5f, 0.55f, 0.20f, 0.40f, 0.0f, 1.1f, 0.25f, -0.16f, false, 8, 1.2f, true, { 0, 0, -160 * kDeg }, { 0, 0, 140 * kDeg } },
};
constexpr MeleeAttackDef kAxeLauncher =
    { "axe_lau", 1.2f, 0.55f, 0.20f, 0.38f, 0.55f, 1.0f, 0.06f, 0.32f, true, 8, 1.2f, true, { 0, 0, -130 * kDeg }, { 0, 0, 130 * kDeg } };

template <int N, int M>
constexpr MeleeComboSet MakeSet(const MeleeAttackDef (&ground)[N], const MeleeAttackDef (&air)[M],
                                const MeleeAttackDef& launcher) {
    return { ground, N, air, M, &launcher };
}

constexpr MeleeComboSet kSwordSet      = MakeSet(kSwordGround,      kSwordAir,      kSwordLauncher);
constexpr MeleeComboSet kDaggerSet     = MakeSet(kDaggerGround,     kDaggerAir,     kDaggerLauncher);
constexpr MeleeComboSet kHammerSet     = MakeSet(kHammerGround,     kHammerAir,     kHammerLauncher);
constexpr MeleeComboSet kSpearSet      = MakeSet(kSpearGround,      kSpearAir,      kSpearLauncher);
constexpr MeleeComboSet kGreatswordSet = MakeSet(kGreatswordGround, kGreatswordAir, kGreatswordLauncher);
constexpr MeleeComboSet kScytheSet     = MakeSet(kScytheGround,     kScytheAir,     kScytheLauncher);
constexpr MeleeComboSet kAxeSet        = MakeSet(kAxeGround,        kAxeAir,        kAxeLauncher);

} // namespace

const MeleeComboSet& engine::game::GetMeleeComboSet(WeaponType type)
{
    switch (type) {
    case WeaponType::Dagger:     return kDaggerSet;
    case WeaponType::Hammer:     return kHammerSet;
    case WeaponType::Spear:      return kSpearSet;
    case WeaponType::Greatsword: return kGreatswordSet;
    case WeaponType::Scythe:     return kScytheSet;
    case WeaponType::Axe:        return kAxeSet;
    default:                     return kSwordSet;
    }
}

// ============================================================
//  MeleeComboController
// ============================================================

const MeleeAttackDef* MeleeComboController::NextStep(bool launcherInput, bool airborne,
                                                     int& outIdx, bool& outLauncher) const
{
    const MeleeComboSet& set = GetMeleeComboSet(type_);
    outLauncher = false;

    // 地上での下入力は打ち上げ技（コンボの途中からでも割り込める）
    if (launcherInput && !airborne && set.launcher != nullptr) {
        outIdx      = 0;
        outLauncher = true;
        return set.launcher;
    }

    const MeleeAttackDef* table = airborne ? set.air : set.ground;
    const int             count = airborne ? set.airCount : set.groundCount;
    if (count <= 0) { return nullptr; }

    // 継続条件: 攻撃中か猶予中で、地上/空中モードが同じなら次の段へ（末尾は先頭へ戻る）
    bool continuing = (active_ != nullptr || chainGraceTimer_ > 0.0f)
                   && !launcherMode_ && (airMode_ == airborne);
    outIdx = continuing ? (stepIdx_ + 1) % count : 0;
    return &table[outIdx];
}

bool MeleeComboController::TryAttack(WeaponType type, bool launcherInput, bool airborne)
{
    // 武器が切り替わっていたらコンボは仕切り直し
    if (active_ != nullptr && type != type_) { Reset(); }
    type_ = type;

    if (active_ != nullptr) {
        if (timer_ >= active_->cancelTime) {
            // キャンセル可能時間を過ぎていれば即座に次の段へ
            int  idx      = 0;
            bool launcher = false;
            const MeleeAttackDef* next = NextStep(launcherInput, airborne, idx, launcher);
            if (next == nullptr) { return false; }
            StartStep(next, idx, airborne, launcher);
        } else {
            // まだ振っている最中 → 先行入力としてバッファし、cancelTime に自動発動
            buffered_         = true;
            bufferedLauncher_ = launcherInput;
            bufferedAir_      = airborne;
        }
        return true;
    }

    int  idx      = 0;
    bool launcher = false;
    const MeleeAttackDef* next = NextStep(launcherInput, airborne, idx, launcher);
    if (next == nullptr) { return false; }
    StartStep(next, idx, airborne, launcher);
    return true;
}

void MeleeComboController::StartStep(const MeleeAttackDef* def, int tableIdx, bool airMode, bool isLauncher)
{
    active_       = def;
    stepIdx_      = tableIdx;
    stepDisplay_  = isLauncher ? 0 : tableIdx + 1;
    airMode_      = airMode;
    launcherMode_ = isLauncher;
    timer_        = 0.0f;
    hitDone_      = false;
    justStarted_  = true;
    buffered_     = false;
    chainGraceTimer_ = 0.0f;
}

void MeleeComboController::Update(float dt)
{
    justHit_     = false;
    justStarted_ = false;
    lungeDelta_  = 0.0f;

    if (active_ == nullptr) {
        chainGraceTimer_ = (std::max)(chainGraceTimer_ - dt, 0.0f);
        return;
    }

    float prev = timer_;
    timer_ += dt;

    // 前進（hitTime までに lungeDist を移動し切る。空中は制御を残すため前進しない）
    if (!airMode_ && prev < active_->hitTime && active_->lungeDist > 0.0f) {
        float t0 = prev / active_->hitTime;
        float t1 = (std::min)(timer_ / active_->hitTime, 1.0f);
        lungeDelta_ = active_->lungeDist * (t1 - t0);
    }

    // ヒット発火
    if (!hitDone_ && timer_ >= active_->hitTime) {
        hitDone_ = true;
        justHit_ = true;
    }

    // 先行入力の消化
    if (buffered_ && timer_ >= active_->cancelTime) {
        int  idx      = 0;
        bool launcher = false;
        const MeleeAttackDef* next = NextStep(bufferedLauncher_, bufferedAir_, idx, launcher);
        if (next != nullptr) {
            StartStep(next, idx, bufferedAir_, launcher);
            return;
        }
        buffered_ = false;
    }

    // モーション終了
    if (timer_ >= active_->duration) {
        // 打ち上げ技の後は仕切り直し、通常段は猶予内なら次の段へ繋がる
        chainGraceTimer_ = launcherMode_ ? 0.0f : kChainGrace_;
        active_ = nullptr;
    }
}

void MeleeComboController::Reset()
{
    active_          = nullptr;
    stepIdx_         = 0;
    stepDisplay_     = 0;
    timer_           = 0.0f;
    hitDone_         = false;
    justHit_         = false;
    justStarted_     = false;
    buffered_        = false;
    chainGraceTimer_ = 0.0f;
    lungeDelta_      = 0.0f;
    launcherMode_    = false;
}

Vector3 MeleeComboController::GetSwingOffset() const
{
    if (active_ == nullptr) { return { 0.0f, 0.0f, 0.0f }; }

    // 振りかぶり → 振り抜き → 構え直し の3相をヒットタイミング基準で組む
    const float windupEnd  = active_->hitTime * 0.6f;               // 振りかぶり完了
    const float strikeEnd  = (std::min)(active_->hitTime * 1.3f, active_->duration * 0.75f); // 振り抜き完了
    const float recoverBeg = active_->duration * 0.8f;              // 構え直し開始

    auto lerp3 = [](const Vector3& a, const Vector3& b, float t) -> Vector3 {
        return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
    };
    const Vector3 neutral = { 0.0f, 0.0f, 0.0f };

    if (timer_ < windupEnd) {
        float t = Easing::EaseOutQuad(timer_ / windupEnd);
        return lerp3(neutral, active_->swingFrom, t);
    }
    if (timer_ < strikeEnd) {
        // 一番目立つ振り抜き。鋭さ重視で先に一気に動かす
        float t = Easing::EaseOutCubic((timer_ - windupEnd) / (strikeEnd - windupEnd));
        return lerp3(active_->swingFrom, active_->swingTo, t);
    }
    if (timer_ < recoverBeg) {
        return active_->swingTo;
    }
    float t = Easing::EaseInOutQuad((std::min)((timer_ - recoverBeg) / (active_->duration - recoverBeg), 1.0f));
    return lerp3(active_->swingTo, neutral, t);
}
