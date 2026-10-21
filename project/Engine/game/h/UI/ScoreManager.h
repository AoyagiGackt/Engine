/**
 * @file ScoreManager.h
 * @brief スコアランキングの保存・読み込みを行うクラス
 * @note ラン中のスコア自体は RunData が持つ
 */
#pragma once
#include <vector>
namespace engine::game {
/** @brief スコアランキングの読み込みと保存を管理する */
class ScoreManager {
public:
    /** @brief ランキング管理の共有インスタンスを取得する */
    static ScoreManager* GetInstance();

    /** @brief スコアをランキングに登録してファイル保存（ゲームクリア時に呼ぶ） @param score 登録するスコア */
    void SubmitAndSave(int score);

    /** @brief ランキングを全消去してファイルにも反映 */
    void ResetAllScores();

    /** @brief 保存ファイルからランキングを読み込む */
    void LoadScores();

    /** @brief 表示用のランキングを独立したスナップショットとして取得する */
    std::vector<int> GetRankingSnapshot() const
    {
        return ranking_;
    }

private:
    /** @brief 空のランキングを生成する */
    ScoreManager() = default;

    std::vector<int> ranking_;

    static constexpr int kMaxRank = 10;
    static constexpr const char* kSaveFile = "scores.txt";

    /** @brief 降順に並べ、保存対象の最大件数に収める */
    void NormalizeRanking();

    /** @brief 現在のランキングを保存する */
    void SaveScores();
};

} // namespace engine::game
