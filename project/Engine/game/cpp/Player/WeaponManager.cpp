#include "WeaponManager.h"
#include "JsonHelper.h"
#include "StringUtility.h"
#include <algorithm>
using namespace engine;
using namespace engine::game;

namespace {

constexpr const char* kWeaponDataPath = "Resources/weapons.json";

WeaponType ParseWeaponType(const std::string& type) {
    if (type == "Sword")  { return WeaponType::Sword; }
    if (type == "Spear")  { return WeaponType::Spear; }
    if (type == "Hammer") { return WeaponType::Hammer; }
    if (type == "Dagger") { return WeaponType::Dagger; }
    if (type == "Ball")   { return WeaponType::Ball; }
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
}

void WeaponManager::SelectIndex(int i) {
    index_ = std::clamp(i, 0, static_cast<int>(weapons_.size()) - 1);
}

void WeaponManager::SelectNext() {
    SelectIndex((index_ + 1) % static_cast<int>(weapons_.size()));
}

void WeaponManager::SelectPrev() {
    int n = static_cast<int>(weapons_.size());
    SelectIndex((index_ - 1 + n) % n);
}
