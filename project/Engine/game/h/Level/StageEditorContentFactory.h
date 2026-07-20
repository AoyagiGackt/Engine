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
};

} // namespace engine::game
