/**
 * @file ScoreManager.cpp
 * @brief スコアランキングの保存とファイル読み書き（ScoreManager）の実装
 */
#include "ScoreManager.h"
#include <algorithm>
#include <fstream>
#include <iterator>

namespace engine::game {
/** @brief 共有インスタンスを取得する */
ScoreManager* ScoreManager::GetInstance() {
    static ScoreManager sInstance;
    return &sInstance;
}

/** @brief スコアをランキングに登録してファイル保存（ゲームクリア時に呼ぶ） */
void ScoreManager::SubmitAndSave(int score) {
    ranking_.push_back(score);
    NormalizeRanking();
    SaveScores();
}

/** @brief ランキングを全消去してファイルにも反映 */
void ScoreManager::ResetAllScores() {
    ranking_.clear();
    SaveScores();
}

/** @brief 保存ファイルからランキングを読み込む */
void ScoreManager::LoadScores() {
    ranking_.clear();
    std::ifstream file(kSaveFile);
    if (!file.is_open()) {
        return;
    }
    int score;
    while (file >> score) {
        ranking_.push_back(score);
    }
    NormalizeRanking();
}

/** @brief 登録順に関わらず上位スコアを最大件数まで保持する */
void ScoreManager::NormalizeRanking() {
    std::sort(ranking_.begin(), ranking_.end(), std::greater<int>());
    if (ranking_.size() > static_cast<size_t>(kMaxRank)) {
        ranking_.resize(kMaxRank);
    }
}

/** @brief ランキングをファイルへ保存する */
void ScoreManager::SaveScores() {
    if (!writesEnabled_) {
        return;
    }
    std::ofstream file(kSaveFile);
    for (int s : ranking_) {
        file << s << "\n";
    }
}

} // namespace engine::game
