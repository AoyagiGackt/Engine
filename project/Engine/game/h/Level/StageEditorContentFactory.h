/**
 * @file StageEditorContentFactory.h
 * @brief ステージエディタで使用する配置テンプレートを生成するファイル
 */
#pragma once
#include "LevelLoader.h"
#include <string>
#include <vector>

namespace engine::game {

/** @brief ファクトリが生成した配置物とトリガーをまとめる構造体 */
struct StageEditorGeneratedContent {
    std::vector<ObjectDesc> objects;
    std::vector<TriggerDesc> triggers;
};

/** @brief 敵Waveの生成条件をまとめる構造体 */
struct StageEditorWaveConfig {
    std::string groupName;
    std::string spawnType;
    std::string activationFlag;
    int enemyCount = 1;
    float spacing = 2.0f;
    Vector3 center = { };
};

/**
 * @brief 複数の配置物から構成される制作テンプレートを生成するクラス
 */
class StageEditorContentFactory {
public:
    /**
     * @brief 進入、敵出現、全滅条件、出口、カメラを含む戦闘部屋を生成する
     * @param center テンプレートの基準位置
     * @param nextSerial 一意な名前の生成に使用して更新する連番
     * @return 生成した配置データを返す
     */
    static StageEditorGeneratedContent CreateBattleRoom(const Vector3& center, int& nextSerial);

    /**
     * @brief 指定条件に沿った敵出現地点をまとめて生成する
     * @param config 敵種類、数、間隔、開始条件を含む生成設定
     * @param nextSerial 一意な名前の生成に使用して更新する連番
     * @return 生成した配置データを返す
     */
    static StageEditorGeneratedContent CreateWave(const StageEditorWaveConfig& config, int& nextSerial);

    /**
     * @brief 条件フラグが立つとせり上がって消えるスライド扉を生成する
     * @param center 扉の中心位置（縦5ブロックぶんの壁）
     * @param conditionFlag 開く条件のフラグ名（例: "condition_room_clear"）。空なら常時閉じたまま
     * @param nextSerial 一意な名前の生成に使用して更新する連番
     */
    static StageEditorGeneratedContent CreateSlidingDoor(const Vector3& center, const std::string& conditionFlag, int& nextSerial);

    /**
     * @brief 指定武器の近接攻撃でしか壊れない壁を生成する（solid、爆風なし）
     * @param center 壁の中心位置
     * @param weaponType 壊せる武器種別名（"Hammer"等、空なら何でも壊せる）
     * @param nextSerial 一意な名前の生成に使用して更新する連番
     */
    static StageEditorGeneratedContent CreateBreakableWall(const Vector3& center, const std::string& weaponType, int& nextSerial);

    /**
     * @brief 区画トリガーと、そのフラグで表示される画面下の案内文をセットで生成する
     * @param center トリガーの位置
     * @param text 案内文（UTF-8）
     * @param nextSerial 一意な名前の生成に使用して更新する連番
     */
    static StageEditorGeneratedContent CreateZoneGuide(const Vector3& center, const std::string& text, int& nextSerial);

    /**
     * @brief 収集物を横一列に生成する
     * @param center 列の中心位置
     * @param count 個数
     * @param spacing 間隔
     * @param nextSerial 一意な名前の生成に使用して更新する連番
     */
    static StageEditorGeneratedContent CreatePickupRow(const Vector3& center, int count, float spacing, int& nextSerial);
};

} // namespace engine::game
