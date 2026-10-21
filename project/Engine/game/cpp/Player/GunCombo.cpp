/**
 * @file GunCombo.cpp
 * @brief 銃の種類ごとの射撃コンボの表と、射撃コンボの進行（GunComboController）の実装
 */
#include "GunCombo.h"
#include "Easing.h"
#include "GameConstants.h"
#include "JsonFieldTable.h"
#include "JsonHelper.h"
#include "Logger.h"
#include "Weapon.h"
#include <algorithm>
#include <array>
#include <unordered_map>

//  射撃コンボ定義テーブル
//  近接（MeleeCombo.cpp）と同じ思想で銃ごとに役割が違うを意識した配分
//   Pistol  … 軽快4段。テンポよく撃ち続ける基準銃、締めは2連射
//   Magnum  … 重い2段。溜めてから撃つ大口径、締めは打ち上げ
//   SMG     … 超高速6段。1発は軽いが2発ずつばらまき、締めは突進スプレー
//   Shotgun … 散弾3段。初段は反動バックステップ、締めは踏み込みゼロ距離
//   Railgun … チャージ2段。長い構えから貫通の一撃、全銃最長射程
namespace engine::game {
namespace {

constexpr float kDeg = GameConstants::kDegToRad;
constexpr float kMinShotDuration = 0.05f; // JSONで0以下を書かれても段が進むようにする下限

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

// GunType の並び順に、各銃のコンボの元データを持つ
constexpr std::array<const GunComboSet*, kGunTypeCount> kSourceSets = {
    &kPistolSet, // Pistol
    &kMagnumSet, // Magnum
    &kSmgSet, // SMG
    &kShotgunSet, // Shotgun
    &kRailgunSet, // Railgun
};

// MeleeCombo.cpp と同じ仕組み既定値はこの上のテーブル、Resources/Config/combos.json の "shots" で個別上書き可能
struct RuntimeGunComboSet {
    std::vector<GunShotDef> steps;
    GunComboSet view { };
};

/** @brief 銃のコンボ定義を写し、写した側を指すように参照を作り直す */
RuntimeGunComboSet CopyComboSet(const GunComboSet& source)
{
    RuntimeGunComboSet result;
    result.steps.assign(source.data, source.data + source.count);
    return result;
}

/** @brief 段の一覧を指す参照（view）を段の配列に合わせて作り直す */
void RefreshView(RuntimeGunComboSet& set)
{
    set.view = { set.steps.data(), static_cast<int>(set.steps.size()) };
}

// combos.json の "shots" で上書きできる項目
constexpr JsonField<GunShotDef> kShotOverrideFields[] = {
    { nullptr, "damageMult", &GunShotDef::damageMult },
    { nullptr, "duration", &GunShotDef::duration },
    { nullptr, "shotTime", &GunShotDef::shotTime },
    { nullptr, "cancelTime", &GunShotDef::cancelTime },
    { nullptr, "rangeMult", &GunShotDef::rangeMult },
    { nullptr, "knockX", &GunShotDef::knockX },
    { nullptr, "knockY", &GunShotDef::knockY },
    { nullptr, "hitStop", &GunShotDef::hitStop },
    { nullptr, "moveDist", &GunShotDef::moveDist },
};

/** @brief JSONの上書き値を射撃の定義へ当てる */
void ApplyShotOverride(GunShotDef& shot, const nlohmann::json& data)
{
    ReadJsonFields(data, kShotOverrideFields, shot);
    // 手で書いた値でも段が進むよう、時間の前後関係と下限を整える
    shot.damageMult = (std::max)(shot.damageMult, 0.0f);
    shot.duration = (std::max)(shot.duration, kMinShotDuration);
    shot.shotTime = std::clamp(shot.shotTime, 0.0f, shot.duration);
    shot.cancelTime = std::clamp(shot.cancelTime, shot.shotTime, shot.duration);
    shot.rangeMult = (std::max)(shot.rangeMult, 0.0f);
    shot.hitStop = (std::max)(shot.hitStop, 0);
}

/** @brief 実行時に使う銃のコンボの表を返す（JSONの上書きを当てられるよう書き換え可能） */
std::array<RuntimeGunComboSet, kGunTypeCount>& GetRuntimeComboSets()
{
    // view は各要素の配列を指すので、最終的な置き場所（この static）の上で直接組み立てる
    static std::array<RuntimeGunComboSet, kGunTypeCount> sets;
    static const bool built = [] {
        std::array<RuntimeGunComboSet, kGunTypeCount>& result = sets;
        for (size_t i = 0; i < kGunTypeCount; ++i)
        {
            result[i] = CopyComboSet(*kSourceSets[i]);
        }

        std::unordered_map<std::string, GunShotDef*> shots;
        for (RuntimeGunComboSet& set : result)
        {
            for (GunShotDef& shot : set.steps)
            {
                shots[shot.id] = &shot;
            }
        }

        const nlohmann::json root = JsonHelper::Load("Resources/Config/combos.json");
        JsonHelper::VisitNamedOverrides(root, "shots", shots,
            [](GunShotDef* shot, const nlohmann::json& data) {
                ApplyShotOverride(*shot, data);
            },
            [](const std::string& id) {
                Logger::LogWarning("Unknown combo shot id: " + id);
            });

        for (RuntimeGunComboSet& set : result)
        {
            RefreshView(set);
        }
        return true;
    }();
    (void)built;
    return sets;
}

} // namespace

/** @brief 銃の種類に合ったコンボの定義を返す */
const GunComboSet& engine::game::GetGunComboSet(GunType type)
{
    auto& sets = GetRuntimeComboSets();
    const size_t index = static_cast<size_t>(type);
    return sets[index < sets.size() ? index : 0].view;
}

//  GunComboController

/** @brief 射撃入力を新しい段の開始・段送り・先行入力として受け付ける。銃の変更時はコンボをリセットする */
bool GunComboController::TryShoot(GunType type)
{
    // 銃が切り替わっていたらコンボは仕切り直し
    if (active_ != nullptr && type != type_)
    {
        Reset();
    }
    type_ = type;

    const GunComboSet& set = GetGunComboSet(type_);
    if (set.count <= 0)
    {
        return false;
    }

    if (active_ != nullptr)
    {
        if (timer_ >= active_->cancelTime)
        {
            // キャンセル可能時間を過ぎていれば即座に次の段へ（末尾は先頭へ戻る）
            StartStep(&set[(stepIdx_ + 1) % set.count], (stepIdx_ + 1) % set.count);
        }
        else
        {
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

/** @brief 指定した段を始め、経過時間と命中済みの印を消す */
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

/** @brief 1フレーム進める（発砲発火・先行入力の消化・モーション終了処理） */
void GunComboController::Update(float dt)
{
    justShot_ = false;
    justStarted_ = false;
    moveDelta_ = 0.0f;

    // 段が無い時は受付猶予だけを減らして終わる
    const std::optional<float> previousTime = AdvanceComboTime(active_ != nullptr, dt, timer_, chainGraceTimer_);
    if (!previousTime)
    {
        return;
    }
    const float prev = *previousTime;

    // 前後移動（shotTime までに moveDist を移動し切る。負なら反動の後退）
    if (prev < active_->shotTime && active_->moveDist != 0.0f)
    {
        moveDelta_ = active_->moveDist * Easing::ProgressDuring(prev, timer_, active_->shotTime);
    }

    // 発砲発火
    if (!shotDone_ && timer_ >= active_->shotTime)
    {
        shotDone_ = true;
        justShot_ = true;
    }

    // 先行入力の消化
    if (buffered_ && timer_ >= active_->cancelTime)
    {
        const GunComboSet& set = GetGunComboSet(type_);
        int idx = (stepIdx_ + 1) % set.count;
        StartStep(&set[idx], idx);
        return;
    }

    // モーション終了（猶予内に次の入力が来ればコンボ継続）
    if (timer_ >= active_->duration)
    {
        chainGraceTimer_ = kChainGrace;
        active_ = nullptr;
    }
}

/** @brief コンボを強制的に打ち切る（被弾・銃切替・乱舞開始時など） */
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

/** @brief 現在の銃回転オフセットを返す（構え→リコイル→構え直しの補間） */
Vector3 GunComboController::GetPoseOffset() const
{
    if (active_ == nullptr)
    {
        return { 0.0f, 0.0f, 0.0f };
    }

    auto lerp3 = [](const Vector3& a, const Vector3& b, float t) -> Vector3 {
        return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
    };
    const Vector3 neutral = { 0.0f, 0.0f, 0.0f };

    // 構え → （発砲の瞬間にリコイルへスナップ）→ 構え直し の3相
    if (timer_ < active_->shotTime)
    {
        // 構え  発砲タイミングへ向けて銃を構えていく
        float t = Easing::EaseOutQuad(timer_ / active_->shotTime);
        return lerp3(neutral, active_->poseFrom, t);
    }
    // リコイル  発砲の瞬間に poseTo へ跳ね、duration までに構え直す
    float t = Easing::EaseOutCubic(
        (std::min)((timer_ - active_->shotTime) / (active_->duration - active_->shotTime), 1.0f));
    return lerp3(active_->poseTo, neutral, t);
}

} // namespace engine::game
