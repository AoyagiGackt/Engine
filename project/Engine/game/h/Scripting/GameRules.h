/**
 * @file GameRules.h
 * @brief 本編ステージの進行ルール（クリア条件・ゲームオーバー・フロア数・敵HP）をJSONから読む設定
 * @note Resources/Config/game_rules.json を編集するだけで進行ルールを変えられるようにするための置き場
 */
#pragma once
#include "Vector4.h"
#include <string>
#include <vector>
namespace engine::game {

/** @brief game_rules.jsonの内容 */
struct GameRulesData {
    bool requireBossWeaponSteal = true; ///< trueならボス撃破+武器奪取でクリア条件が成立する
    std::string clearFlag = "stage_clear"; ///< このGameFlagsが立ってもクリア条件が成立する（グラフからの操作用）
    bool gameOverOnHpZero = true; ///< trueならラン中にHPが0になるとゲームオーバー結果へ遷移する
    float resultDisplaySeconds = 2.5f; ///< クリア結果表示の秒数
    int finalFloor = 6; ///< このフロア以上でクリア扱い（それ未満は次のフロアへ）
    int bossHpCombat = 20;
    int bossHpElite = 35;
    int bossHpBoss = 60;
    int weaponEnemyHp = 5; ///< 道中の武器持ち雑魚のHP
    std::string bossStealWeapon = "Hammer"; ///< ボスから奪取する武器種別名
    Vector4 bossColor = { 0.9f, 0.65f, 0.15f, 1.0f };
    int waterFloor = 3; ///< 水面演出を有効にするフロア番号（負なら無効）
    std::vector<std::string> levelPaths; ///< フロア番号順のレベルJSON。フロア数を超えた分は最後のものを使う
};

/**
 * @brief game_rules.jsonを読み込んで保持するシングルトン
 */
class GameRules {
public:
    /**
     * @brief 唯一のGameRulesインスタンスを取得する（初回はJSONを読み込む）
     * @return GameRulesのインスタンス
     */
    static GameRules* GetInstance();

    /** @brief JSONを読み直す */
    void Reload();

    /** @brief 読み込んだルールを返す */
    const GameRulesData& Get() const { return data_; }

    /**
     * @brief フロア番号に対応するレベルJSONのパスを返す
     * @param floor 現在のフロア番号（0始まり）
     * @param fallback levelPathsが空の場合に返すパス
     * @return レベルJSONのパス（levelPathsの範囲外なら最後の要素）
     */
    std::string LevelPathForFloor(int floor, const std::string& fallback) const;

private:
    GameRules();
    GameRules(const GameRules&) = delete;
    GameRules& operator=(const GameRules&) = delete;

    GameRulesData data_;
};

} // namespace engine::game
