/**
 * @file GameRules.h
 * @brief 本編ステージの進行ルール（クリア条件・ゲームオーバー・フロア数・敵HP）をJSONから読む設定
 * @note Resources/Config/game_rules.json を編集するだけで進行ルールを変えられるようにするための置き場
 */
#pragma once
#include "Vector4.h"
#include <map>
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
    int weaponEnemyHp = 10; ///< 道中の武器持ち雑魚のHP
    int flyingWeaponEnemyHp = 5; ///< 道中の飛行型雑魚のHP（空中にいて殴りにくいため地上の雑魚より低くする）
    std::string starterWeapon = "Sword"; ///< 新規ラン開始時に最初から持っている近接武器の種別名（空文字なら素手で開始）
    std::string bossStealWeapon = "Hammer"; ///< ボスから奪取する武器種別名
    Vector4 bossColor = { 0.9f, 0.65f, 0.15f, 1.0f };
    int waterFloor = 3; ///< 水面演出を有効にするフロア番号（負なら無効）
    std::vector<std::string> levelPaths; ///< フロア番号順のレベルJSON。フロア数を超えた分は最後のものを使う
    std::string bossTechnique = "slam_shockwave"; ///< ボス武器奪取時に習得する技の名前（RunData::AddBossTechnique）
    float bossTechniqueRadiusMult = 1.6f; ///< 習得後、叩きつけ系固有技の判定半径に掛かる倍率
    int bossTechniqueBonusDamage = 3; ///< 習得後、叩きつけ系固有技に上乗せするダメージ
    /** @brief フロアクリア時のスタイルランクごとの加算スコア（キーはRunData::CalcRankのランク文字列） */
    std::map<std::string, int> rankScores = {
        { "SSS", 5000 }, { "SS", 3500 }, { "S", 2500 }, { "A", 1800 }, { "B", 1200 }, { "C", 800 }, { "D", 500 },
    };
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

    /**
     * @brief フロアクリア時に加算するスコアを返す
     * @param rank RunData::CalcRankが返すランク文字列
     * @return 加算スコア（表に無いランクなら0）
     */
    int ScoreForRank(const std::string& rank) const;

private:
    GameRules();
    GameRules(const GameRules&) = delete;
    GameRules& operator=(const GameRules&) = delete;

    GameRulesData data_;
};

} // namespace engine::game
