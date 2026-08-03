/**
 * @file WeaponManager.cpp
 * @brief WeaponManagerのプレイヤーの操作、戦闘、状態遷移に関する具体的な処理を実装するファイル
 */
#include "WeaponManager.h"
#include "JsonHelper.h"
#include "StringUtility.h"
#include <algorithm>
using namespace engine;
using namespace engine::game;

namespace {

constexpr const char* kWeaponDataPath = "Resources/Config/weapons.json";

WeaponType ParseWeaponType(const std::string& type)
{
    if (type == "Sword") {
        return WeaponType::Sword;
    }
    if (type == "Spear") {
        return WeaponType::Spear;
    }
    if (type == "Hammer") {
        return WeaponType::Hammer;
    }
    if (type == "Dagger") {
        return WeaponType::Dagger;
    }
    if (type == "Ball") {
        return WeaponType::Ball;
    }
    if (type == "Greatsword") {
        return WeaponType::Greatsword;
    }
    if (type == "Scythe") {
        return WeaponType::Scythe;
    }
    if (type == "Axe") {
        return WeaponType::Axe;
    }
    return WeaponType::Sword;
}

GunType ParseGunType(const std::string& type)
{
    if (type == "Magnum") {
        return GunType::Magnum;
    }
    if (type == "SMG") {
        return GunType::SMG;
    }
    if (type == "Shotgun") {
        return GunType::Shotgun;
    }
    if (type == "Railgun") {
        return GunType::Railgun;
    }
    return GunType::Pistol;
}

std::vector<WeaponCommand> ParseCommands(const nlohmann::json& arr)
{
    std::vector<WeaponCommand> commands;
    for (const auto& c : arr) {
        commands.push_back({ c.value("key", ""),
            StringUtility::ConvertString(c.value("desc", "")) });
    }
    return commands;
}

} // namespace

WeaponManager* WeaponManager::GetInstance()
{
    static WeaponManager instance;
    return &instance;
}

WeaponManager::WeaponManager()
{
    nlohmann::json j = JsonHelper::Load(kWeaponDataPath);

    // 射撃武器一覧（Gキーで循環切り替え、スタイルとは独立に選ぶ）
    for (const auto& r : j.value("rangedWeapons", nlohmann::json::array())) {
        RangedWeaponData data;
        data.name = r.value("name", "");
        data.nameJp = StringUtility::ConvertString(r.value("nameJp", ""));
        data.type = ParseGunType(r.value("type", ""));
        data.damage = r.value("damage", 0.0f);
        data.range = r.value("range", 0.0f);
        data.attackInterval = r.value("attackInterval", 0.0f);
        data.description = r.value("description", "");
        data.commands = ParseCommands(r.value("commands", nlohmann::json::array()));

        data.color[0] = data.color[1] = data.color[2] = 1.0f;
        data.color[3] = 1.0f;
        auto color = r.value("color", nlohmann::json::array());
        for (size_t i = 0; i < 4 && i < color.size(); ++i) {
            data.color[i] = color[i].get<float>();
        }

        rangedWeapons_.push_back(std::move(data));
    }
    // JSON が空でも GetRanged() が範囲外参照しないよう保険を入れておく
    if (rangedWeapons_.empty()) {
        rangedWeapons_.push_back({ "Handgun", L"ハンドガン", GunType::Pistol,
            12.0f, 15.0f, 0.18f, "", { 1.0f, 1.0f, 1.0f, 1.0f }, { } });
    }

    // スタイル一覧（1〜5キー対応）
    for (const auto& w : j.value("weapons", nlohmann::json::array())) {
        WeaponData data;
        data.name = w.value("name", "");
        data.styleName = w.value("styleName", "");
        data.styleNameJp = StringUtility::ConvertString(w.value("styleNameJp", ""));
        data.type = ParseWeaponType(w.value("type", ""));
        data.damage = w.value("damage", 0.0f);
        data.range = w.value("range", 0.0f);
        data.attackInterval = w.value("attackInterval", 0.0f);
        data.description = w.value("description", "");
        data.knockbackMult = w.value("knockbackMult", 1.0f);
        data.element = w.value("element", "None");
        data.commands = ParseCommands(w.value("commands", nlohmann::json::array()));

        data.styleColor[0] = data.styleColor[1] = data.styleColor[2] = 0.0f;
        data.styleColor[3] = 1.0f;
        auto color = w.value("styleColor", nlohmann::json::array());
        for (size_t i = 0; i < 4 && i < color.size(); ++i) {
            data.styleColor[i] = color[i].get<float>();
        }

        const auto effect = w.value("effect", nlohmann::json::object());
        data.effectBurstCount = std::clamp(effect.value("burstCount", 0), 0, 64);
        data.effectRingRadius = (std::max)(effect.value("ringRadius", 0.0f), 0.0f);
        auto effectColor = effect.value("color", nlohmann::json::array());
        for (size_t i = 0; i < 4; ++i) {
            data.effectColor[i] = i < effectColor.size()
                ? effectColor[i].get<float>()
                : data.styleColor[i];
        }

        weapons_.push_back(std::move(data));
    }

    // 4スロットは倒した敵から奪って埋めていく想定なので、初期状態は全ロック。
    // ただし何も使えないと詰むため、機動力型（奇術師/Dagger）だけ最初から解放しておく
    unlocked_.assign(weapons_.size(), false);
}

void WeaponManager::SelectIndex(int i)
{
    int n = static_cast<int>(weapons_.size());
    i = std::clamp(i, 0, n - 1);
    if (IsUnlocked(i)) {
        index_ = i;
        for (int slot = 0; slot < static_cast<int>(slots_.size()); ++slot) {
            if (slots_[slot] == i) {
                selectedSlot_ = slot;
                break;
            }
        }
    }
}

void WeaponManager::SelectSlot(int slot)
{
    if (slot < 0 || slot >= static_cast<int>(slots_.size()) || slots_[slot] < 0) {
        return;
    }
    selectedSlot_ = slot;
    index_ = slots_[slot];
}

int WeaponManager::GetSlotWeaponIndex(int slot) const
{
    if (slot < 0 || slot >= static_cast<int>(slots_.size())) {
        return -1;
    }
    return slots_[slot];
}

void WeaponManager::SelectNext()
{
    const int count = static_cast<int>(slots_.size());
    for (int step = 1; step <= count; ++step) {
        const int slot = (selectedSlot_ + step) % count;
        if (slots_[slot] >= 0) {
            SelectSlot(slot);
            return;
        }
    }
}

void WeaponManager::SelectPrev()
{
    const int count = static_cast<int>(slots_.size());
    for (int step = 1; step <= count; ++step) {
        const int slot = ((selectedSlot_ - step) % count + count) % count;
        if (slots_[slot] >= 0) {
            SelectSlot(slot);
            return;
        }
    }
}

bool WeaponManager::Unlock(WeaponType type)
{
    return Acquire(type) == AcquireResult::Added;
}

void WeaponManager::SelectNextUnlockedInCurrentSlot()
{
    const int count = static_cast<int>(weapons_.size());
    for (int step = 1; step <= count; ++step) {
        const int candidate = (index_ + step) % count;
        if (!IsUnlocked(candidate)) {
            continue;
        }
        if (selectedSlot_ < 0 || selectedSlot_ >= static_cast<int>(slots_.size())) {
            selectedSlot_ = 0;
        }
        index_ = candidate;
        slots_[selectedSlot_] = candidate;
        return;
    }
}

void WeaponManager::SelectPrevUnlockedInCurrentSlot()
{
    const int count = static_cast<int>(weapons_.size());
    for (int step = 1; step <= count; ++step) {
        const int candidate = ((index_ - step) % count + count) % count;
        if (!IsUnlocked(candidate)) {
            continue;
        }
        if (selectedSlot_ < 0 || selectedSlot_ >= static_cast<int>(slots_.size())) {
            selectedSlot_ = 0;
        }
        index_ = candidate;
        slots_[selectedSlot_] = candidate;
        return;
    }
}

WeaponManager::AcquireResult WeaponManager::Acquire(WeaponType type)
{
    for (size_t i = 0; i < weapons_.size(); ++i) {
        if (weapons_[i].type != type) {
            continue;
        }
        if (unlocked_[i]) {
            return AcquireResult::Duplicate;
        }

        for (int slot = 0; slot < static_cast<int>(slots_.size()); ++slot) {
            if (slots_[slot] >= 0) {
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

void WeaponManager::ReplacePendingWeapon(int slot)
{
    if (!HasPendingWeapon() || slot < 0 || slot >= static_cast<int>(slots_.size())) {
        return;
    }

    const int removed = slots_[slot];
    if (removed >= 0) {
        unlocked_[removed] = false;
    }
    slots_[slot] = pendingWeaponIndex_;
    unlocked_[pendingWeaponIndex_] = true;
    selectedSlot_ = slot;
    index_ = pendingWeaponIndex_;
    pendingWeaponIndex_ = -1;
}

void WeaponManager::DiscardPendingWeapon()
{
    pendingWeaponIndex_ = -1;
}

void WeaponManager::UnlockAll()
{
    // 全武器を使える状態にするのは常に安全（現在の装備選択には影響しない）
    unlocked_.assign(weapons_.size(), true);

    // 既に何か装備済み（Trainingで組んだ構成など）ならスロット構成は壊さない。
    // ここで毎回スロットを既定値へ戻すと、Trainingで選んだ武器がテストシーンへ行くたびに
    // リセットされてしまい、Training→テストという行き来での検証ができなくなる
    if (HasEquippedWeapon()) {
        return;
    }

    slots_.fill(-1);
    for (int slot = 0; slot < static_cast<int>(slots_.size())
        && slot < static_cast<int>(weapons_.size());
        ++slot) {
        slots_[slot] = slot;
    }
    selectedSlot_ = 0;
    index_ = 0;
    pendingWeaponIndex_ = -1;
}

void WeaponManager::EquipForTraining(WeaponType type)
{
    for (int weaponIndex = 0; weaponIndex < static_cast<int>(weapons_.size()); ++weaponIndex) {
        if (weapons_[weaponIndex].type != type) {
            continue;
        }

        for (int slot = 0; slot < static_cast<int>(slots_.size()); ++slot) {
            if (slots_[slot] == weaponIndex) {
                selectedSlot_ = slot;
                index_ = weaponIndex;
                pendingWeaponIndex_ = -1;
                return;
            }
        }

        int targetSlot = -1;
        for (int slot = 0; slot < static_cast<int>(slots_.size()); ++slot) {
            if (slots_[slot] < 0) {
                targetSlot = slot;
                break;
            }
        }
        if (targetSlot < 0 || targetSlot >= static_cast<int>(slots_.size())) {
            targetSlot = (selectedSlot_ >= 0 && selectedSlot_ < static_cast<int>(slots_.size()))
                ? selectedSlot_
                : 0;
        }
        const int removedWeapon = slots_[targetSlot];
        if (removedWeapon >= 0 && removedWeapon != weaponIndex) {
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

void WeaponManager::Reset()
{
    std::fill(unlocked_.begin(), unlocked_.end(), false);
    slots_.fill(-1);
    selectedSlot_ = -1;
    pendingWeaponIndex_ = -1;
    index_ = 0;
    rangedIndex_ = 0;
}
