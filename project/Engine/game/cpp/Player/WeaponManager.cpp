/**
 * @file WeaponManager.cpp
 * @brief 武器の一覧の読み込みと、装備・入手の管理（WeaponManager）の実装
 */
#include "WeaponManager.h"
#include "GameRules.h"
#include "JsonFieldTable.h"
#include "JsonHelper.h"
#include "StringUtility.h"
#include <algorithm>
#include <iterator>

namespace engine::game {
namespace {
constexpr float kMinSkillCooldownMult = 0.05f; // 覚醒中に固有技を連打できなくなる下限
constexpr size_t kColorChannels = 4;
constexpr int kMaxEffectBurstCount = 64; // 1ヒットで出す属性粒子の上限（描画が詰まらないように）

// 1つの値で済む項目の対応表（日本語名・種類・色・コマンド表は変換が要るので個別に読む）
constexpr JsonField<RangedWeaponData> kRangedFields[] = {
    { nullptr, "name", &RangedWeaponData::name },
    { nullptr, "damage", &RangedWeaponData::damage },
    { nullptr, "range", &RangedWeaponData::range },
    { nullptr, "attackInterval", &RangedWeaponData::attackInterval },
    { nullptr, "description", &RangedWeaponData::description },
    { nullptr, "element", &RangedWeaponData::element },
};

constexpr JsonField<WeaponData> kWeaponFields[] = {
    { nullptr, "name", &WeaponData::name },
    { nullptr, "damage", &WeaponData::damage },
    { nullptr, "range", &WeaponData::range },
    { nullptr, "attackInterval", &WeaponData::attackInterval },
    { nullptr, "description", &WeaponData::description },
    { nullptr, "knockbackMult", &WeaponData::knockbackMult },
    { nullptr, "element", &WeaponData::element },
    { "effect", "burstCount", &WeaponData::effectBurstCount },
    { "effect", "ringRadius", &WeaponData::effectRingRadius },
};

constexpr JsonField<AwakenedWeaponMods> kAwakenedFields[] = {
    { nullptr, "damageMult", &AwakenedWeaponMods::damageMult },
    { nullptr, "skillRadiusMult", &AwakenedWeaponMods::skillRadiusMult },
    { nullptr, "skillCooldownMult", &AwakenedWeaponMods::skillCooldownMult },
    { nullptr, "knockbackMult", &AwakenedWeaponMods::knockbackMult },
};

/** @brief [R, G, B, A] の配列を読む（足りない成分は out の値のまま） */
void ReadColor(const nlohmann::json& value, float (&out)[kColorChannels])
{
    if (!value.is_array())
    {
        return;
    }
    for (size_t i = 0; i < kColorChannels && i < value.size(); ++i)
    {
        out[i] = value[i].get<float>();
    }
}

constexpr const char* kWeaponDataPath = "Resources/Config/weapons.json";

/** @brief weapons.json の銃の種類名を GunType へ変換する */
GunType ParseGunType(const std::string& type)
{
    /** @brief 銃種と設定ファイル内の識別名の対応 */
    struct NamedGunType {
        const char* name;
        GunType type;
    };
    static constexpr NamedGunType kNames[] = {
        { "Magnum", GunType::Magnum },
        { "SMG", GunType::SMG },
        { "Shotgun", GunType::Shotgun },
        { "Railgun", GunType::Railgun },
    };
    for (const NamedGunType& entry : kNames)
    {
        if (type == entry.name)
        {
            return entry.type;
        }
    }
    return GunType::Pistol;
}

/** @brief JSON配列からコマンド入力の一覧を読む */
std::vector<WeaponCommand> ParseCommands(const nlohmann::json& arr)
{
    std::vector<WeaponCommand> commands;
    for (const auto& c : arr)
    {
        commands.push_back({ c.value("key", ""),
            StringUtility::ConvertString(c.value("desc", "")) });
    }
    return commands;
}

} // namespace

/** @brief シングルトンインスタンスを取得する */
WeaponManager* WeaponManager::GetInstance()
{
    static WeaponManager instance;
    return &instance;
}

/** @brief 画面に出す武器名を返す（日本語のスタイル名。未設定の武器だけ内部名で代用する） */
std::wstring WeaponManager::GetDisplayName(const WeaponData& data)
{
    // 英語の内部名が日本語の表示に混ざらないよう、日本語名があればそちらを優先する
    return data.styleNameJp.empty() ? StringUtility::ConvertString(data.name) : data.styleNameJp;
}

/** @brief weapons.json を読み込んで武器の一覧を作る */
WeaponManager::WeaponManager()
{
    nlohmann::json j = JsonHelper::Load(kWeaponDataPath);

    // 射撃武器一覧（Gキーで循環切り替え、スタイルとは独立に選ぶ）
    for (const auto& r : j.value("rangedWeapons", nlohmann::json::array()))
    {
        RangedWeaponData data {};
        data.type = ParseGunType(r.value("type", ""));
        // 遠距離用の補助武器は、役割が重なる複数の銃から一つに集約する。
        if (data.type != GunType::Pistol)
        {
            continue;
        }
        ReadJsonFields(r, kRangedFields, data);
        data.nameJp = StringUtility::ConvertString(r.value("nameJp", ""));
        data.commands = ParseCommands(r.value("commands", nlohmann::json::array()));

        // 色は白から、属性色は未指定ならマズルの色を引き継ぐ
        std::fill(std::begin(data.color), std::end(data.color), 1.0f);
        ReadColor(r.value("color", nlohmann::json::array()), data.color);
        std::copy(std::begin(data.color), std::end(data.color), std::begin(data.effectColor));
        ReadColor(r.value("effectColor", nlohmann::json::array()), data.effectColor);

        rangedWeapons_.push_back(std::move(data));
    }
    // JSON が空でも GetRanged() が範囲外参照しないよう保険を入れておく
    if (rangedWeapons_.empty())
    {
        constexpr float kFallbackDamage = 12.0f;
        constexpr float kFallbackRange = 15.0f;
        constexpr float kFallbackInterval = 0.18f;
        rangedWeapons_.push_back({ "Handgun", L"ハンドガン", GunType::Pistol,
            kFallbackDamage, kFallbackRange, kFallbackInterval, "", { 1.0f, 1.0f, 1.0f, 1.0f }, { } });
    }

    // スタイル一覧（1〜5キー対応）
    for (const auto& w : j.value("weapons", nlohmann::json::array()))
    {
        WeaponData data {};
        data.type = ParseWeaponTypeName(w.value("type", ""));
        // 本編で使う四つの基本戦闘スタイルだけを読み込む（同時に持てるのはkSlotCount個まで）
        static constexpr WeaponType kCoreWeaponTypes[] = {
            WeaponType::Sword, WeaponType::Spear, WeaponType::Dagger, WeaponType::Hammer,
        };
        const bool isCoreWeapon = std::find(std::begin(kCoreWeaponTypes), std::end(kCoreWeaponTypes), data.type)
            != std::end(kCoreWeaponTypes);
        if (!isCoreWeapon)
        {
            continue;
        }
        ReadJsonFields(w, kWeaponFields, data);
        data.styleNameJp = StringUtility::ConvertString(w.value("styleNameJp", ""));
        data.commands = ParseCommands(w.value("commands", nlohmann::json::array()));
        ReadJsonFields(w.value("awakened", nlohmann::json::object()), kAwakenedFields, data.awakened);
        // 読んだ値を使える範囲に収める
        data.awakened.skillCooldownMult = (std::max)(data.awakened.skillCooldownMult, kMinSkillCooldownMult);
        data.effectBurstCount = std::clamp(data.effectBurstCount, 0, kMaxEffectBurstCount);
        data.effectRingRadius = (std::max)(data.effectRingRadius, 0.0f);

        // 色は黒から、属性色は未指定ならスタイルの色を引き継ぐ
        std::fill(std::begin(data.styleColor), std::end(data.styleColor), 0.0f);
        data.styleColor[kColorChannels - 1] = 1.0f;
        ReadColor(w.value("styleColor", nlohmann::json::array()), data.styleColor);
        std::copy(std::begin(data.styleColor), std::end(data.styleColor), std::begin(data.effectColor));
        ReadColor(w.value("effect", nlohmann::json::object()).value("color", nlohmann::json::array()), data.effectColor);

        weapons_.push_back(std::move(data));
    }

    // スロットは倒した敵から奪って埋めていく想定なので、初期状態は全ロック。
    // ただし何も使えないと詰むため、機動力型（奇術師/Dagger）だけ最初から解放しておく
    unlocked_.assign(weapons_.size(), false);
}

/** @brief 指定したスロットを選択する */
void WeaponManager::SelectSlot(int slot)
{
    if (slot < 0 || slot >= static_cast<int>(slots_.size()) || slots_[slot] < 0)
    {
        return;
    }
    selectedSlot_ = slot;
    index_ = slots_[slot];
}

/** @brief スロットに登録された武器インデックスを返す */
int WeaponManager::GetSlotWeaponIndex(int slot) const
{
    if (slot < 0 || slot >= static_cast<int>(slots_.size()))
    {
        return -1;
    }
    return slots_[slot];
}

/** @brief 次の解放済みスタイルへ切り替える（循環、未解放はスキップ） */
void WeaponManager::SelectNext()
{
    const int count = static_cast<int>(slots_.size());
    for (int step = 1; step <= count; ++step)
    {
        const int slot = (selectedSlot_ + step) % count;
        if (slots_[slot] >= 0)
        {
            SelectSlot(slot);
            return;
        }
    }
}

/** @brief 前の解放済みスタイルへ切り替える（循環、未解放はスキップ） */
void WeaponManager::SelectPrev()
{
    const int count = static_cast<int>(slots_.size());
    for (int step = 1; step <= count; ++step)
    {
        const int slot = ((selectedSlot_ - step) % count + count) % count;
        if (slots_[slot] >= 0)
        {
            SelectSlot(slot);
            return;
        }
    }
}

/** @brief 指定タイプの武器を解放し、そのまま装備する（敵からの武器奪取用） */
bool WeaponManager::Unlock(WeaponType type)
{
    return Acquire(type) == AcquireResult::Added;
}

/** @brief 全解放済み武器を対象に次を選び、現在スロットへ割り当てる */
void WeaponManager::SelectNextUnlockedInCurrentSlot()
{
    SelectUnlockedInCurrentSlot(1);
}

/** @brief 全解放済み武器を対象に前を選び、現在スロットへ割り当てる */
void WeaponManager::SelectPrevUnlockedInCurrentSlot()
{
    SelectUnlockedInCurrentSlot(-1);
}

/** @brief 解放済みの武器を direction（+1 で次、-1 で前）の向きに探し、現在のスロットへ割り当てる */
void WeaponManager::SelectUnlockedInCurrentSlot(int direction)
{
    const int count = static_cast<int>(weapons_.size());
    for (int step = 1; step <= count; ++step)
    {
        // 負の余りにならないよう count を足してから割る
        const int candidate = ((index_ + direction * step) % count + count) % count;
        if (!IsUnlocked(candidate))
        {
            continue;
        }
        if (selectedSlot_ < 0 || selectedSlot_ >= static_cast<int>(slots_.size()))
        {
            selectedSlot_ = 0;
        }
        index_ = candidate;
        slots_[selectedSlot_] = candidate;
        return;
    }
}


/** @brief 武器を空きスロットへ追加し、満杯なら交換待ちにする */
WeaponManager::AcquireResult WeaponManager::Acquire(WeaponType type)
{
    for (size_t i = 0; i < weapons_.size(); ++i)
    {
        if (weapons_[i].type != type)
        {
            continue;
        }
        if (unlocked_[i])
        {
            return AcquireResult::Duplicate;
        }

        for (int slot = 0; slot < static_cast<int>(slots_.size()); ++slot)
        {
            if (slots_[slot] >= 0)
            {
                continue;
            }
            slots_[slot] = static_cast<int>(i);
            unlocked_[i] = true;
            selectedSlot_ = slot;
            index_ = static_cast<int>(i);
            return AcquireResult::Added;
        }

        pendingWeaponIndex_ = static_cast<int>(i);
        return AcquireResult::NeedsReplacement;
    }
    return AcquireResult::Duplicate;
}

/** @brief 交換待ちの武器で指定スロットを置き換える */
void WeaponManager::ReplacePendingWeapon(int slot)
{
    if (!HasPendingWeapon() || slot < 0 || slot >= static_cast<int>(slots_.size()))
    {
        return;
    }

    const int removed = slots_[slot];
    if (removed >= 0)
    {
        unlocked_[removed] = false;
    }
    slots_[slot] = pendingWeaponIndex_;
    unlocked_[pendingWeaponIndex_] = true;
    selectedSlot_ = slot;
    index_ = pendingWeaponIndex_;
    pendingWeaponIndex_ = -1;
}

/** @brief 交換待ちの武器を破棄する */
void WeaponManager::DiscardPendingWeapon()
{
    pendingWeaponIndex_ = -1;
}

/** @brief 全武器を解放し、先頭からスロット数ぶんを登録する */
void WeaponManager::UnlockAll()
{
    // 全武器を使える状態にするのは常に安全（現在の装備選択には影響しない）
    unlocked_.assign(weapons_.size(), true);

    // 既に何か装備済み（Trainingで組んだ構成など）ならスロット構成は壊さない。
    // ここで毎回スロットを既定値へ戻すと、Trainingで選んだ武器がテストシーンへ行くたびに
    // リセットされてしまい、Training→テストという行き来での検証ができなくなる
    if (HasEquippedWeapon())
    {
        return;
    }

    slots_.fill(-1);
    for (int slot = 0; slot < static_cast<int>(slots_.size())
        && slot < static_cast<int>(weapons_.size());
        ++slot)
    {
        slots_[slot] = slot;
    }
    selectedSlot_ = 0;
    index_ = 0;
    pendingWeaponIndex_ = -1;
}

/** @brief トレーニング用に指定武器を現在のスロットへ装備する */
void WeaponManager::EquipForTraining(WeaponType type)
{
    // 指定された種類の武器を探す
    for (int weaponIndex = 0; weaponIndex < static_cast<int>(weapons_.size()); ++weaponIndex)
    {
        if (weapons_[weaponIndex].type != type)
        {
            continue;
        }

        // もうスロットに入っていれば、そのスロットを選ぶだけ
        for (int slot = 0; slot < static_cast<int>(slots_.size()); ++slot)
        {
            if (slots_[slot] == weaponIndex)
            {
                selectedSlot_ = slot;
                index_ = weaponIndex;
                pendingWeaponIndex_ = -1;
                return;
            }
        }

        // 空きスロットへ入れる。空きが無ければ今選んでいるスロット（無ければ先頭）と入れ替える
        int targetSlot = -1;
        for (int slot = 0; slot < static_cast<int>(slots_.size()); ++slot)
        {
            if (slots_[slot] < 0)
            {
                targetSlot = slot;
                break;
            }
        }
        if (targetSlot < 0 || targetSlot >= static_cast<int>(slots_.size()))
        {
            targetSlot = (selectedSlot_ >= 0 && selectedSlot_ < static_cast<int>(slots_.size()))
                ? selectedSlot_
                : 0;
        }
        // 押し出した武器は未解放に戻し、入れた武器を解放して選ぶ
        const int removedWeapon = slots_[targetSlot];
        if (removedWeapon >= 0 && removedWeapon != weaponIndex)
        {
            unlocked_[removedWeapon] = false;
        }
        slots_[targetSlot] = weaponIndex;
        unlocked_[weaponIndex] = true;
        selectedSlot_ = targetSlot;
        index_ = weaponIndex;
        pendingWeaponIndex_ = -1;
        return;
    }
}

/** @brief タイトルデモ用に指定武器を装備する（スロットから押し出した武器も解放済みのまま残す） */
void WeaponManager::EquipForDemo(WeaponType type)
{
    const std::vector<bool> unlocked = unlocked_;
    EquipForTraining(type);
    unlocked_ = unlocked;
    unlocked_[index_] = true;
}

/** @brief 全武器を未解放状態へ戻す（新しいランの開始時に呼ぶ） */
void WeaponManager::Reset()
{
    std::fill(unlocked_.begin(), unlocked_.end(), false);
    slots_.fill(-1);
    selectedSlot_ = -1;
    pendingWeaponIndex_ = -1;
    index_ = 0;
    rangedIndex_ = 0;
}

/** @brief 新しいランの開始時に呼ぶ。Reset()した上でgame_rules.jsonのstarterWeaponを最初のスロットへ装備する */
void WeaponManager::ResetForNewRun()
{
    Reset();
    const std::string& starter = GameRules::GetInstance()->Get().starterWeapon;
    if (!starter.empty())
    {
        Acquire(ParseWeaponTypeName(starter));
    }
}

/** @brief 現在の解放状況・装備状態を退避する */
WeaponManager::Snapshot WeaponManager::SaveSnapshot() const
{
    return { unlocked_, slots_, index_, selectedSlot_, pendingWeaponIndex_, rangedIndex_ };
}

/** @brief SaveSnapshot()で退避した状態へ戻す */
void WeaponManager::RestoreSnapshot(const Snapshot& snapshot)
{
    unlocked_ = snapshot.unlocked;
    slots_ = snapshot.slots;
    index_ = snapshot.index;
    selectedSlot_ = snapshot.selectedSlot;
    pendingWeaponIndex_ = snapshot.pendingWeaponIndex;
    rangedIndex_ = snapshot.rangedIndex;
}

} // namespace engine::game
