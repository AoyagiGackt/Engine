/**
 * @file SaveData.h
 * @brief ローグライトのラン継続データ・通算記録の永続化を管理するファイル
 */
#pragma once
#include "RunData.h"
#include "json.hpp"
#include <cstdint>
#include <vector>
namespace engine::game {

/** @brief タイトルのコンティニューで復元するラン継続データ */
struct ContinueData {
    /** @brief スロット順の武器番号。負の番号は空きスロット */
    std::vector<int32_t> weaponSlots;
    /** @brief 武器定義順の解放状態。0は未解放、1は解放済み */
    std::vector<int32_t> unlockedWeapons;
    /** @brief 現在の近接武器番号 */
    int32_t weaponIndex = 0;
    /** @brief 選択中の近接武器スロット */
    int32_t selectedWeaponSlot = -1;
    /** @brief 交換待ちの近接武器番号 */
    int32_t pendingWeaponIndex = -1;
    /** @brief 選択中の射撃武器番号 */
    int32_t rangedWeaponIndex = 0;
    bool valid = false; ///< コンティニューデータが存在するか

    int32_t hp = 0; ///< 保存時点のHP
    int32_t maxHp = 0; ///< 保存時点の最大HP
    int32_t gold = 0; ///< 保存時点のゴールド
    int32_t score = 0; ///< 保存時点のスコア
    int32_t floor = 0; ///< 保存時点のフロア番号

    RunData::NodeType currentNode = RunData::NodeType::kCombat; ///< 保存時点のノード種別
    std::vector<RunData::Skill> skills; ///< 習得済みスキル一覧
};

/** @brief タイトル画面等で参照する通算記録 */
struct SaveRecords {
    int32_t bestFloorReached = 0; ///< 過去最高到達フロア
    int32_t totalRuns = 0; ///< 累計プレイ回数
    int32_t totalClears = 0; ///< 累計クリア回数
    int64_t totalGoldEarned = 0; ///< 累計獲得ゴールド
};

/**
 * @brief コンティニューデータ・通算記録の読み書きを管理するシングルトン
 * @note Load() はゲーム起動時、各種更新系メソッドは呼び出し時に即座に保存する
 */
class SaveDataManager {
public:
    static SaveDataManager* GetInstance();

    /** @brief save/save.json から読み込む（ファイルがなければデフォルト値のまま） */
    void Load();
    /** @brief save/save.json へ書き込む */
    void Save();
    /** @brief ファイルへの書き込みを止める（自動テスト中にプレイヤーのセーブを消さないため。止めている間も内容はメモリ上で変わる） */
    void SetWritesEnabled(bool enabled) {
        writesEnabled_ = enabled;
    }

    /** @brief コンティニューデータが存在するか */
    bool HasContinue() const {
        return continue_.valid;
    }

    /**
     * @brief RunData の現在状態をコンティニューデータとして保存する
     * @param rd 保存元のランデータ
     */
    void SaveContinue(const RunData& rd);

    /**
     * @brief 保存済みのコンティニューデータを RunData へ復元する
     * @param rd 復元先のランデータ
     */
    void LoadContinue(RunData& rd) const;

    /** @brief コンティニューデータを破棄する（ラン終了時に呼ぶ） */
    void ClearContinue();

    /**
     * @brief ラン終了結果を通算記録へ反映する
     * @param cleared     ボスを撃破してクリアしたか
     * @param floorReached 到達したフロア番号
     * @param goldEarned   そのランで獲得したゴールド
     */
    void RecordRunResult(bool cleared, int floorReached, int goldEarned);

    /** @brief 表示用の通算記録を独立した値として取得する */
    SaveRecords GetRecordSnapshot() const {
        return records_;
    }

private:
    /** @brief 初期の記録を持つセーブデータ管理を作る */
    SaveDataManager() = default;

    /** @brief 現在のセーブ形式へ古いデータを補完する */
    void Migrate(nlohmann::json& data, int sourceVersion) const;

    ContinueData continue_;
    SaveRecords records_;

    static constexpr const char* kFilePath = "save/save.json";
    bool writesEnabled_ = true; ///< false の間は Save() でファイルへ書かない
    static constexpr int kCurrentVersion = 2;
};

} // namespace engine::game
