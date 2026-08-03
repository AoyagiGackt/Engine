/**
 * @file GunCombo.cpp
 * @brief GunComboのプレイヤーの操作、戦闘、状態遷移に関する具体的な処理を実装するファイル
 */
#include "GunCombo.h"
#include "Easing.h"
#include "JsonHelper.h"
#include "Logger.h"
#include "Weapon.h"
#include <algorithm>
#include <array>
#include <unordered_map>
using namespace engine;
using namespace engine::game;

//  射撃コンボ定義テーブル
//  近接（MeleeCombo.cpp）と同じ思想で銃ごとに役割が違うを意識した配分
//   Pistol  … 軽快4段。テンポよく撃ち続ける基準銃、締めは2連射
//   Magnum  … 重い2段。溜めてから撃つ大口径、締めは打ち上げ
//   SMG     … 超高速6段。1発は軽いが2発ずつばらまき、締めは突進スプレー
//   Shotgun … 散弾3段。初段は反動バックステップ、締めは踏み込みゼロ距離
//   Railgun … チャージ2段。長い構えから貫通の一撃、全銃最長射程
namespace {

constexpr float kDeg = 3.14159265f / 180.0f;

// ── Pistol（ハンドガン）  右→左→右のトリプルタップ + 2連射締め ─────
constexpr GunShotDef kPistolSteps[] = {
    { "pst1", 1.0f, 0.20f, 0.05f, 0.10f, 1, 0.0f, 1.0f, 0.06f, 0.00f, false, 2, 0.00f, { 0, 0, 8 * kDeg }, { -22 * kDeg, 0, 0 } },
    { "pst2", 1.0f, 0.20f, 0.05f, 0.10f, 1, 0.0f, 1.0f, 0.06f, 0.00f, false, 2, 0.00f, { 0, 0, -8 * kDeg }, { -22 * kDeg, 0, 0 } },
    { "pst3", 1.1f, 0.22f, 0.06f, 0.11f, 1, 0.0f, 1.0f, 0.08f, 0.00f, false, 3, 0.00f, { 0, 0, 10 * kDeg }, { -28 * kDeg, 0, 0 } },
    { "pst4", 1.5f, 0.34f, 0.10f, 0.24f, 2, 4.0f, 1.1f, 0.20f, 0.04f, false, 5, 0.25f, { 12 * kDeg, 0, 0 }, { -45 * kDeg, 0, 0 } },
};

// ── Magnum（マグナム）  溜めて撃つ重量2段、締めは打ち上げ ───────────
constexpr GunShotDef kMagnumSteps[] = {
    { "mag1", 2.2f, 0.55f, 0.26f, 0.40f, 1, 0.0f, 1.2f, 0.40f, 0.00f, false, 9, -0.15f, { 14 * kDeg, 0, 0 }, { -70 * kDeg, 0, 0 } },
    { "mag2", 3.0f, 0.70f, 0.30f, 0.52f, 1, 0.0f, 1.2f, 0.25f, 0.30f, true, 12, 0.20f, { 16 * kDeg, 0, -12 * kDeg }, { -90 * kDeg, 0, 0 } },
};

// ── SMG（マシンピストル）  2発ずつの超高速5段 + 突進スプレー ────────
constexpr GunShotDef kSmgSteps[] = {
    { "smg1", 0.45f, 0.11f, 0.03f, 0.055f, 2, 8.0f, 0.9f, 0.03f, 0.00f, false, 1, 0.08f, { 0, 0, 5 * kDeg }, { -8 * kDeg, 0, 0 } },
    { "smg2", 0.45f, 0.11f, 0.03f, 0.055f, 2, 8.0f, 0.9f, 0.03f, 0.00f, false, 1, 0.08f, { 0, 0, -5 * kDeg }, { -8 * kDeg, 0, 0 } },
    { "smg3", 0.45f, 0.11f, 0.03f, 0.055f, 2, 8.0f, 0.9f, 0.03f, 0.00f, false, 1, 0.08f, { 0, 0, 5 * kDeg }, { -8 * kDeg, 0, 0 } },
    { "smg4", 0.45f, 0.11f, 0.03f, 0.055f, 2, 8.0f, 0.9f, 0.03f, 0.00f, false, 1, 0.08f, { 0, 0, -5 * kDeg }, { -8 * kDeg, 0, 0 } },
    { "smg5", 0.50f, 0.12f, 0.03f, 0.060f, 2, 10.0f, 0.9f, 0.05f, 0.00f, false, 2, 0.10f, { 0, 0, 6 * kDeg }, { -10 * kDeg, 0, 0 } },
    { "smg6", 1.2f, 0.45f, 0.16f, 0.34f, 6, 30.0f, 1.0f, 0.22f, 0.05f, false, 5, 0.50f, { 0, 22 * kDeg, 0 }, { -12 * kDeg, -22 * kDeg, 0 } },
};

// ── Shotgun（ショットガン）  反動後退→踏み込み→ゼロ距離打ち上げ ────
constexpr GunShotDef kShotgunSteps[] = {
    { "sht1", 1.6f, 0.45f, 0.12f, 0.30f, 6, 22.0f, 0.60f, 0.35f, 0.00f, false, 6, -0.35f, { 6 * kDeg, 0, 0 }, { -48 * kDeg, 0, 0 } },
    { "sht2", 1.8f, 0.45f, 0.14f, 0.32f, 6, 22.0f, 0.65f, 0.35f, 0.10f, false, 7, 0.55f, { 8 * kDeg, 0, 0 }, { -52 * kDeg, 0, 0 } },
    { "sht3", 2.6f, 0.65f, 0.20f, 0.48f, 8, 30.0f, 0.55f, 0.65f, 0.32f, true, 11, 0.90f, { 12 * kDeg, 0, 0 }, { -75 * kDeg, 0, 0 } },
};

// ── Railgun（レールガン）  長い構えからの貫通2段、締めは打ち上げ ────
constexpr GunShotDef kRailgunSteps[] = {
    { "rail1", 3.5f, 0.85f, 0.45f, 0.62f, 1, 0.0f, 3.0f, 0.55f, 0.00f, false, 13, -0.20f, { 18 * kDeg, 0, 0 }, { -55 * kDeg, 0, 0 } },
    { "rail2", 4.5f, 0.95f, 0.40f, 0.75f, 1, 0.0f, 3.0f, 0.35f, 0.35f, true, 15, 0.15f, { 20 * kDeg, 0, 10 * kDeg }, { -80 * kDeg, 0, -15 * kDeg } },
};

constexpr GunComboSet kPistolSet = MakeComboArray(kPistolSteps);
constexpr GunComboSet kMagnumSet = MakeComboArray(kMagnumSteps);
constexpr GunComboSet kSmgSet = MakeComboArray(kSmgSteps);
constexpr GunComboSet kShotgunSet = MakeComboArray(kShotgunSteps);
constexpr GunComboSet kRailgunSet = MakeComboArray(kRailgunSteps);

// MeleeCombo.cpp と同じ仕組み既定値はこの上のテーブル、Resources/Config/combos.json の "shots" で個別上書き可能
struct RuntimeGunComboSet {
    std::vector<GunShotDef> steps;
    GunComboSet view { };
};

RuntimeGunComboSet CopyComboSet(const GunComboSet& source)
{
    RuntimeGunComboSet result;
    result.steps.assign(source.data, source.data + source.count);
    return result;
}

void RefreshView(RuntimeGunComboSet& set)
{
    set.view = { set.steps.data(), static_cast<int>(set.steps.size()) };
}

void ApplyShotOverride(GunShotDef& shot, const nlohmann::json& data)
{
    shot.damageMult = (std::max)(data.value("damageMult", shot.damageMult), 0.0f);
    shot.duration = (std::max)(data.value("duration", shot.duration), 0.05f);
    shot.shotTime = std::clamp(data.value("shotTime", shot.shotTime), 0.0f, shot.duration);
    shot.cancelTime = std::clamp(data.value("cancelTime", shot.cancelTime), shot.shotTime, shot.duration);
    shot.rangeMult = (std::max)(data.value("rangeMult", shot.rangeMult), 0.0f);
    shot.knockX = data.value("knockX", shot.knockX);
    shot.knockY = data.value("knockY", shot.knockY);
    shot.hitStop = (std::max)(data.value("hitStop", shot.hitStop), 0);
    shot.moveDist = data.value("moveDist", shot.moveDist);
}

std::array<RuntimeGunComboSet, 5>& GetRuntimeComboSets()
{
    static std::array<RuntimeGunComboSet, 5> sets = [] {
        std::array<RuntimeGunComboSet, 5> result;
        result[0] = CopyComboSet(kPistolSet);
        result[1] = CopyComboSet(kMagnumSet);
        result[2] = CopyComboSet(kSmgSet);
        result[3] = CopyComboSet(kShotgunSet);
        result[4] = CopyComboSet(kRailgunSet);

        std::unordered_map<std::string, GunShotDef*> shots;
        for (RuntimeGunComboSet& set : result) {
            for (GunShotDef& shot : set.steps) {
                shots[shot.id] = &shot;
            }
        }

        const nlohmann::json root = JsonHelper::Load("Resources/Config/combos.json");
        for (const auto& data : root.value("shots", nlohmann::json::array())) {
            const std::string id = data.value("id", "");
            const auto found = shots.find(id);
            if (found == shots.end()) {
                Logger::LogWarning("Unknown combo shot id: " + id);
                continue;
            }
            ApplyShotOverride(*found->second, data);
        }

        for (RuntimeGunComboSet& set : result) {
            RefreshView(set);
        }
        return result;
    }();
    return sets;
}

} // namespace

const GunComboSet& engine::game::GetGunComboSet(GunType type)
{
    auto& sets = GetRuntimeComboSets();
    switch (type) {
    case GunType::Magnum:
        return sets[1].view;
    case GunType::SMG:
        return sets[2].view;
    case GunType::Shotgun:
        return sets[3].view;
    case GunType::Railgun:
        return sets[4].view;
    default:
        return sets[0].view;
    }
}

//  GunComboController

bool GunComboController::TryShoot(GunType type)
{
    // 銃が切り替わっていたらコンボは仕切り直し
    if (active_ != nullptr && type != type_) {
        Reset();
    }
    type_ = type;

    const GunComboSet& set = GetGunComboSet(type_);
    if (set.count <= 0) {
        return false;
    }

    if (active_ != nullptr) {
        if (timer_ >= active_->cancelTime) {
            // キャンセル可能時間を過ぎていれば即座に次の段へ（末尾は先頭へ戻る）
            StartStep(&set[(stepIdx_ + 1) % set.count], (stepIdx_ + 1) % set.count);
        } else {
            // まだ撃っている最中 → 先行入力としてバッファし、cancelTime に自動発動
            buffered_ = true;
        }
        return true;
    }

    // 猶予中なら次の段から、そうでなければ最初から
    int idx = (chainGraceTimer_ > 0.0f) ? (stepIdx_ + 1) % set.count : 0;
    StartStep(&set[idx], idx);
    return true;
}

void GunComboController::StartStep(const GunShotDef* def, int tableIdx)
{
    active_ = def;
    stepIdx_ = tableIdx;
    stepDisplay_ = tableIdx + 1;
    timer_ = 0.0f;
    shotDone_ = false;
    justStarted_ = true;
    buffered_ = false;
    chainGraceTimer_ = 0.0f;
}

void GunComboController::Update(float dt)
{
    justShot_ = false;
    justStarted_ = false;
    moveDelta_ = 0.0f;

    if (active_ == nullptr) {
        chainGraceTimer_ = (std::max)(chainGraceTimer_ - dt, 0.0f);
        return;
    }

    float prev = timer_;
    timer_ += dt;

    // 前後移動（shotTime までに moveDist を移動し切る。負なら反動の後退）
    if (prev < active_->shotTime && active_->moveDist != 0.0f) {
        float t0 = prev / active_->shotTime;
        float t1 = (std::min)(timer_ / active_->shotTime, 1.0f);
        moveDelta_ = active_->moveDist * (t1 - t0);
    }

    // 発砲発火
    if (!shotDone_ && timer_ >= active_->shotTime) {
        shotDone_ = true;
        justShot_ = true;
    }

    // 先行入力の消化
    if (buffered_ && timer_ >= active_->cancelTime) {
        const GunComboSet& set = GetGunComboSet(type_);
        int idx = (stepIdx_ + 1) % set.count;
        StartStep(&set[idx], idx);
        return;
    }

    // モーション終了（猶予内に次の入力が来ればコンボ継続）
    if (timer_ >= active_->duration) {
        chainGraceTimer_ = kChainGrace_;
        active_ = nullptr;
    }
}

void GunComboController::Reset()
{
    active_ = nullptr;
    stepIdx_ = 0;
    stepDisplay_ = 0;
    timer_ = 0.0f;
    shotDone_ = false;
    justShot_ = false;
    justStarted_ = false;
    buffered_ = false;
    chainGraceTimer_ = 0.0f;
    moveDelta_ = 0.0f;
}

Vector3 GunComboController::GetPoseOffset() const
{
    if (active_ == nullptr) {
        return { 0.0f, 0.0f, 0.0f };
    }

    auto lerp3 = [](const Vector3& a, const Vector3& b, float t) -> Vector3 {
        return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
    };
    const Vector3 neutral = { 0.0f, 0.0f, 0.0f };

    // 構え → （発砲の瞬間にリコイルへスナップ）→ 構え直し の3相
    if (timer_ < active_->shotTime) {
        // 構え  発砲タイミングへ向けて銃を構えていく
        float t = Easing::EaseOutQuad(timer_ / active_->shotTime);
        return lerp3(neutral, active_->poseFrom, t);
    }
    // リコイル  発砲の瞬間に poseTo へ跳ね、duration までに構え直す
    float t = Easing::EaseOutCubic(
        (std::min)((timer_ - active_->shotTime) / (active_->duration - active_->shotTime), 1.0f));
    return lerp3(active_->poseTo, neutral, t);
}
