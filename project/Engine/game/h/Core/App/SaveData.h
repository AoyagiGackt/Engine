/**
 * @file SaveData.h
 * @brief ローグライトのラン継続データ・通算記録の永続化を管理するファイル
 */
#pragma once
#include "RunData.h"
#include <vector>
namespace engine::game {

/** @brief タイトルの「コンティニュー」で復元するラン継続データ */
struct ContinueData {
    bool valid = false; ///< コンティニューデータが存在するか

    int hp     = 0; ///< 保存時点のHP
    int maxHp  = 0; ///< 保存時点の最大HP
    int gold   = 0; ///< 保存時点のゴールド
    int floor  = 0; ///< 保存時点のフロア番号

    RunData::NodeType currentNode = RunData::NodeType::Combat; ///< 保存時点のノード種別
    std::vector<RunData::Skill> skills;                        ///< 習得済みスキル一覧
};

/** @brief タイトル画面等で参照する通算記録 */
struct SaveRecords {
    int       bestFloorReached = 0; ///< 過去最高到達フロア
    int       totalRuns        = 0; ///< 累計プレイ回数
    int       totalClears      = 0; ///< 累計クリア回数
    long long totalGoldEarned  = 0; ///< 累計獲得ゴールド
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

    /** @brief コンティニューデータが存在するか */
    bool HasContinue() const { return continue_.valid; }

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

    const SaveRecords& GetRecords() const { return records_; }

private:
    SaveDataManager() = default;

    ContinueData continue_;
    SaveRecords  records_;

    static constexpr const char* kFilePath = "save/save.json";
};

} // namespace engine::game
