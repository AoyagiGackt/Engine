/**
 * @file GameRules.cpp
 * @brief GameRulesの進行ルール読み込みを実装するファイル
 */
#include "GameRules.h"
#include "JsonHelper.h"
#include "Logger.h"
using namespace engine::game;
using namespace engine;

namespace {
constexpr const char* kGameRulesPath = "Resources/Config/game_rules.json";
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
    data_.requireBossWeaponSteal = root.value("requireBossWeaponSteal", defaults.requireBossWeaponSteal);
    data_.clearFlag = root.value("clearFlag", defaults.clearFlag);
    data_.gameOverOnHpZero = root.value("gameOverOnHpZero", defaults.gameOverOnHpZero);
    data_.resultDisplaySeconds = root.value("resultDisplaySeconds", defaults.resultDisplaySeconds);
    data_.finalFloor = root.value("finalFloor", defaults.finalFloor);
    if (root.contains("bossHp") && root["bossHp"].is_object()) {
        const auto& hp = root["bossHp"];
        data_.bossHpCombat = hp.value("combat", defaults.bossHpCombat);
        data_.bossHpElite = hp.value("elite", defaults.bossHpElite);
        data_.bossHpBoss = hp.value("boss", defaults.bossHpBoss);
    }
    data_.weaponEnemyHp = root.value("weaponEnemyHp", defaults.weaponEnemyHp);
    data_.bossStealWeapon = root.value("bossStealWeapon", defaults.bossStealWeapon);
    if (root.contains("bossColor") && root["bossColor"].is_array() && root["bossColor"].size() >= 4) {
        const auto& c = root["bossColor"];
        data_.bossColor = { c[0].get<float>(), c[1].get<float>(), c[2].get<float>(), c[3].get<float>() };
    }
    data_.waterFloor = root.value("waterFloor", defaults.waterFloor);
    for (const auto& path : root.value("levelPaths", nlohmann::json::array())) {
        if (path.is_string() && !path.get<std::string>().empty()) {
            data_.levelPaths.push_back(path.get<std::string>());
        }
    }
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
