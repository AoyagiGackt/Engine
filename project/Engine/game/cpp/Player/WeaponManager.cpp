#include "WeaponManager.h"
#include "JsonHelper.h"
#include "StringUtility.h"
#include <algorithm>
using namespace engine;
using namespace engine::game;

namespace {

constexpr const char* kWeaponDataPath = "Resources/weapons.json";

WeaponType ParseWeaponType(const std::string& type) {
    if (type == "Sword")      { return WeaponType::Sword; }
    if (type == "Spear")      { return WeaponType::Spear; }
    if (type == "Hammer")     { return WeaponType::Hammer; }
    if (type == "Dagger")     { return WeaponType::Dagger; }
    if (type == "Ball")       { return WeaponType::Ball; }
    if (type == "Greatsword") { return WeaponType::Greatsword; }
    if (type == "Scythe")     { return WeaponType::Scythe; }
    if (type == "Axe")        { return WeaponType::Axe; }
    return WeaponType::Sword;
}

std::vector<WeaponCommand> ParseCommands(const nlohmann::json& arr) {
    std::vector<WeaponCommand> commands;
    for (const auto& c : arr) {
        commands.push_back({
            c.value("key", ""),
            StringUtility::ConvertString(c.value("desc", ""))
        });
    }
    return commands;
}

} // namespace

WeaponManager* WeaponManager::GetInstance() {
    static WeaponManager instance;
    return &instance;
}

WeaponManager::WeaponManager() {
    nlohmann::json j = JsonHelper::Load(kWeaponDataPath);

    // 初期射撃武器（全スタイル共通）
    const auto& r = j["ranged"];
    ranged_ = {
        r.value("name", ""),
        r.value("damage", 0.0f),
        r.value("range", 0.0f),
        r.value("attackInterval", 0.0f),
        r.value("description", ""),
        ParseCommands(r.value("commands", nlohmann::json::array()))
    };

    // スタイル一覧（1〜5キー対応）
    for (const auto& w : j.value("weapons", nlohmann::json::array())) {
        WeaponData data;
        data.name           = w.value("name", "");
        data.styleName      = w.value("styleName", "");
        data.styleNameJp    = StringUtility::ConvertString(w.value("styleNameJp", ""));
        data.type           = ParseWeaponType(w.value("type", ""));
        data.damage         = w.value("damage", 0.0f);
        data.range          = w.value("range", 0.0f);
        data.attackInterval = w.value("attackInterval", 0.0f);
        data.description    = w.value("description", "");
        data.knockbackMult  = w.value("knockbackMult", 1.0f);
        data.commands       = ParseCommands(w.value("commands", nlohmann::json::array()));

        data.styleColor[0] = data.styleColor[1] = data.styleColor[2] = 0.0f;
        data.styleColor[3] = 1.0f;
        auto color = w.value("styleColor", nlohmann::json::array());
        for (size_t i = 0; i < 4 && i < color.size(); ++i) {
            data.styleColor[i] = color[i].get<float>();
        }

        weapons_.push_back(std::move(data));
    }

    // 4スロットは倒した敵から奪って埋めていく想定なので、初期状態は全ロック。
    // ただし何も使えないと詰むため、機動力型（奇術師/Dagger）だけ最初から解放しておく
    unlocked_.assign(weapons_.size(), false);
    for (size_t i = 0; i < weapons_.size(); ++i) {
        if (weapons_[i].type == WeaponType::Dagger) {
            unlocked_[i] = true;
            index_       = static_cast<int>(i);
            break;
        }
    }
}

void WeaponManager::SelectIndex(int i) {
    int n = static_cast<int>(weapons_.size());
    i = std::clamp(i, 0, n - 1);
    if (IsUnlocked(i)) { index_ = i; }
}

void WeaponManager::SelectNext() {
    int n = static_cast<int>(weapons_.size());
    for (int step = 1; step <= n; ++step) {
        int i = (index_ + step) % n;
        if (IsUnlocked(i)) { index_ = i; return; }
    }
}

void WeaponManager::SelectPrev() {
    int n = static_cast<int>(weapons_.size());
    for (int step = 1; step <= n; ++step) {
        int i = ((index_ - step) % n + n) % n;
        if (IsUnlocked(i)) { index_ = i; return; }
    }
}

bool WeaponManager::Unlock(WeaponType type) {
    for (size_t i = 0; i < weapons_.size(); ++i) {
        if (weapons_[i].type != type) { continue; }
        if (unlocked_[i]) { return false; } // 重複入手（将来: 経験値/強化素材に転用）
        unlocked_[i] = true;
        index_       = static_cast<int>(i); // 奪った武器をそのまま装備
        return true;
    }
    return false;
}
