/**
 * @file GameRules.cpp
 * @brief GameRulesの進行ルール読み込みを実装するファイル
 */
#include "GameRules.h"
#include "JsonFieldTable.h"
#include "JsonHelper.h"
#include "Logger.h"
#include <algorithm>
using namespace engine::game;
using namespace engine;

namespace {
constexpr const char* kGameRulesPath = "Resources/Config/game_rules.json";

// 1つの値で済む項目の対応表（配列や表になる levelPaths / rankScore は Reload で読む）
constexpr JsonField<GameRulesData> kFields[] = {
    { nullptr, "requireBossWeaponSteal", &GameRulesData::requireBossWeaponSteal },
    { nullptr, "clearFlag", &GameRulesData::clearFlag },
    { nullptr, "gameOverOnHpZero", &GameRulesData::gameOverOnHpZero },
    { nullptr, "resultDisplaySeconds", &GameRulesData::resultDisplaySeconds },
    { nullptr, "finalFloor", &GameRulesData::finalFloor },
    { "bossHp", "combat", &GameRulesData::bossHpCombat },
    { "bossHp", "elite", &GameRulesData::bossHpElite },
    { "bossHp", "boss", &GameRulesData::bossHpBoss },
    { nullptr, "weaponEnemyHp", &GameRulesData::weaponEnemyHp },
    { nullptr, "flyingWeaponEnemyHp", &GameRulesData::flyingWeaponEnemyHp },
    { nullptr, "starterWeapon", &GameRulesData::starterWeapon },
    { nullptr, "bossStealWeapon", &GameRulesData::bossStealWeapon },
    { nullptr, "bossColor", &GameRulesData::bossColor },
    { nullptr, "waterFloor", &GameRulesData::waterFloor },
    { nullptr, "bossTechnique", &GameRulesData::bossTechnique },
    { nullptr, "bossTechniqueRadiusMult", &GameRulesData::bossTechniqueRadiusMult },
    { nullptr, "bossTechniqueBonusDamage", &GameRulesData::bossTechniqueBonusDamage },
};
}

GameRules* GameRules::GetInstance()
{
    static GameRules instance;
    return &instance;
}

GameRules::GameRules()
{
    Reload();
}

void GameRules::Reload()
{
    GameRulesData defaults;
    data_ = defaults;
    const nlohmann::json root = JsonHelper::Load(kGameRulesPath);
    if (!root.is_object()) {
        Logger::LogWarning(std::string("GameRules: ") + kGameRulesPath + " が読めません。既定値で動きます");
        return;
    }
    ReadJsonFields(root, kFields, data_);
    for (const auto& path : root.value("levelPaths", nlohmann::json::array())) {
        if (path.is_string() && !path.get<std::string>().empty()) {
            data_.levelPaths.push_back(path.get<std::string>());
        }
    }
    // finalFloorが0以下ならレベル数をそのまま最終フロアにする（ステージを足すだけで周回数が追従する）
    if (data_.finalFloor <= 0) {
        data_.finalFloor = (std::max)(1, static_cast<int>(data_.levelPaths.size()));
    }
    if (root.contains("rankScore") && root["rankScore"].is_object()) {
        for (const auto& [rank, score] : root["rankScore"].items()) {
            if (score.is_number_integer()) {
                data_.rankScores[rank] = score.get<int>();
            }
        }
    }
}

int GameRules::ScoreForRank(const std::string& rank) const
{
    const auto it = data_.rankScores.find(rank);
    return it != data_.rankScores.end() ? it->second : 0;
}

std::string GameRules::LevelPathForFloor(int floor, const std::string& fallback) const
{
    if (data_.levelPaths.empty()) {
        return fallback;
    }
    const int lastIndex = static_cast<int>(data_.levelPaths.size()) - 1;
    const int index = floor < 0 ? 0 : (floor > lastIndex ? lastIndex : floor);
    return data_.levelPaths[static_cast<size_t>(index)];
}
