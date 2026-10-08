/**
 * @file ScoreManager.h
 * @brief スコアランキングの保存・読み込みを行うクラス
 * @note ラン中のスコア自体は RunData が持つ
 */
#pragma once
#include <vector>
namespace engine::game {
class ScoreManager {
public:
    static ScoreManager* GetInstance();

    /** @brief スコアをランキングに登録してファイル保存（ゲームクリア時に呼ぶ） @param score 登録するスコア */
    void SubmitAndSave(int score);

    /** @brief ランキングを全消去してファイルにも反映 */
    void ResetAllScores();

    /** @brief 保存ファイルからランキングを読み込む */
    void LoadScores();

    const std::vector<int>& GetRanking() const { return ranking_; }

private:
    ScoreManager() = default;

    std::vector<int> ranking_;

    static constexpr int kMaxRank = 10;
    static constexpr const char* kSaveFile = "scores.txt";

    void SaveScores();
};

} // namespace engine::game
