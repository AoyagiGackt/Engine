/**
 * @file GameRules.cpp
 * @brief GameRulesの進行ルール読み込みを実装するファイル
 */
#include "GameRules.h"
#include "JsonFieldTable.h"
#include "JsonHelper.h"
#include "Logger.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace engine::game {
namespace {
constexpr const char* kGameRulesPath = "Resources/Config/game_rules.json";

// 1つの値で済む項目の対応表（配列や表になる levelPaths / rankScore は Reload で読む）
constexpr JsonField<GameRulesData> kFields[] = {
    { nullptr, "requireBossWeaponSteal", &GameRulesData::requireBossWeaponSteal },
    { nullptr, "clearFlag", &GameRulesData::clearFlag },
    { nullptr, "gameOverOnHpZero", &GameRulesData::gameOverOnHpZero },
    { nullptr, "debugGameOverButton", &GameRulesData::debugGameOverButton },
    { nullptr, "debugClearButton", &GameRulesData::debugClearButton },
    { nullptr, "debugLiberationButton", &GameRulesData::debugLiberationButton },
    { nullptr, "resultDisplaySeconds", &GameRulesData::resultDisplaySeconds },
    { nullptr, "finalFloor", &GameRulesData::finalFloor },
    { "bossHp", "combat", &GameRulesData::bossHpCombat },
    { "bossHp", "elite", &GameRulesData::bossHpElite },
    { "bossHp", "boss", &GameRulesData::bossHpBoss },
    { nullptr, "weaponEnemyHp", &GameRulesData::weaponEnemyHp },
    { nullptr, "flyingWeaponEnemyHp", &GameRulesData::flyingWeaponEnemyHp },
    { nullptr, "playerMaxHp", &GameRulesData::playerMaxHp },
    { nullptr, "starterWeapon", &GameRulesData::starterWeapon },
    { nullptr, "randomEnemyWeapons", &GameRulesData::randomEnemyWeapons },
    { nullptr, "gunSwitchEnabled", &GameRulesData::gunSwitchEnabled },
    { nullptr, "bossStealWeapon", &GameRulesData::bossStealWeapon },
    { nullptr, "bossColor", &GameRulesData::bossColor },
    { nullptr, "bossTechnique", &GameRulesData::bossTechnique },
    { nullptr, "bossTechniqueRadiusMult", &GameRulesData::bossTechniqueRadiusMult },
    { nullptr, "bossTechniqueBonusDamage", &GameRulesData::bossTechniqueBonusDamage },
};
}

/** @brief 唯一のGameRulesインスタンスを取得する（初回はJSONを読み込む） */
GameRules* GameRules::GetInstance() {
    static GameRules sInstance;
    return &sInstance;
}

/** @brief game_rules.json を読み込んで初期化する */
GameRules::GameRules() {
    Reload();
}

/** @brief JSONを読み直す */
void GameRules::Reload() {
    // まず既定値に戻し、読めない時はそのまま既定値で動かす
    GameRulesData defaults;
    data_ = defaults;
    const nlohmann::json root = JsonHelper::Load(kGameRulesPath);
    if (!root.is_object()) {
        Logger::LogWarning(std::string("GameRules: ") + kGameRulesPath + " が読めません。既定値で動きます");
        return;
    }
    ReadJsonFields(root, kFields, data_);
    // 入口は全座標が数値で、少なくとも1か所ある場合だけまとめて差し替える
    if (root.contains("stagePortalXs") && root["stagePortalXs"].is_array() && !root["stagePortalXs"].empty()) {
        std::vector<float> positions;
        for (const auto& value : root["stagePortalXs"]) {
            if (!value.is_number() || !std::isfinite(value.get<float>())) {
                positions.clear();
                break;
            }
            positions.push_back(value.get<float>());
        }
        if (!positions.empty()) {
            data_.stagePortalXs = std::move(positions);
        }
    }
    // 道中の敵に持たせる武器の候補（書いてあれば置き換える。空の文字列は飛ばす）
    if (root.contains("enemyWeaponPool") && root["enemyWeaponPool"].is_array()) {
        std::vector<std::string> pool;
        for (const auto& name : root["enemyWeaponPool"]) {
            if (name.is_string() && !name.get<std::string>().empty()) {
                pool.push_back(name.get<std::string>());
            }
        }
        if (!pool.empty()) {
            data_.enemyWeaponPool = std::move(pool);
        }
    }
    // 本編で順に遊ぶレベルの一覧（空の文字列は飛ばす）
    for (const auto& path : root.value("levelPaths", nlohmann::json::array())) {
        if (path.is_string() && !path.get<std::string>().empty()) {
            data_.levelPaths.push_back(path.get<std::string>());
        }
    }
    // finalFloorが0以下ならレベル数をそのまま最終フロアにする（ステージを足すだけで周回数が追従する）
    if (data_.finalFloor <= 0) {
        data_.finalFloor = (std::max)(1, static_cast<int>(data_.levelPaths.size()));
    }
    // ランクごとのスコア（整数だけを採用する）
    if (root.contains("rankScore") && root["rankScore"].is_object()) {
        for (const auto& [rank, score] : root["rankScore"].items()) {
            if (score.is_number_integer()) {
                data_.rankScores[rank] = score.get<int>();
            }
        }
    }
}

/** @brief フロアクリア時に加算するスコアを返す */
int GameRules::ScoreForRank(const std::string& rank) const {
    const auto it = data_.rankScores.find(rank);
    return it != data_.rankScores.end() ? it->second : 0;
}

/** @brief フロア番号に対応するレベルJSONのパスを返す */
std::string GameRules::LevelPathForFloor(int floor, const std::string& fallback) const {
    if (data_.levelPaths.empty()) {
        return fallback;
    }
    const int lastIndex = static_cast<int>(data_.levelPaths.size()) - 1;
    const int index = floor < 0 ? 0 : (floor > lastIndex ? lastIndex : floor);
    return data_.levelPaths[static_cast<size_t>(index)];
}

} // namespace engine::game
