/**
 * @file GameRules.cpp
 * @brief GameRulesの進行ルール読み込みを実装するファイル
 */
#include "GameRules.h"
#include "JsonHelper.h"
#include "Logger.h"
#include <algorithm>
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
    data_.flyingWeaponEnemyHp = root.value("flyingWeaponEnemyHp", defaults.flyingWeaponEnemyHp);
    data_.starterWeapon = root.value("starterWeapon", defaults.starterWeapon);
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
    // finalFloorが0以下ならレベル数をそのまま最終フロアにする（ステージを足すだけで周回数が追従する）
    if (data_.finalFloor <= 0) {
        data_.finalFloor = (std::max)(1, static_cast<int>(data_.levelPaths.size()));
    }
    data_.bossTechnique = root.value("bossTechnique", defaults.bossTechnique);
    data_.bossTechniqueRadiusMult = root.value("bossTechniqueRadiusMult", defaults.bossTechniqueRadiusMult);
    data_.bossTechniqueBonusDamage = root.value("bossTechniqueBonusDamage", defaults.bossTechniqueBonusDamage);
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
