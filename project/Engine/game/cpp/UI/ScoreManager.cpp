/**
 * @file ScoreManager.cpp
 * @brief ScoreManagerのゲーム画面UIの状態更新と描画に関する具体的な処理を実装するファイル
 */
#include "ScoreManager.h"
#include <algorithm>
#include <fstream>
#include <iterator>
using namespace engine;
using namespace engine::game;

ScoreManager* ScoreManager::GetInstance()
{
    static ScoreManager instance;
    return &instance;
}

void ScoreManager::ResetCurrentScore()
{
    currentScore_ = 0;
}

void ScoreManager::AddScore(int points)
{
    currentScore_ += points;
}

void ScoreManager::SubmitAndSave()
{
    ranking_.push_back(currentScore_);
    std::sort(ranking_.begin(), ranking_.end(), std::greater<int>());
    if ((int)ranking_.size() > kMaxRank) {
        ranking_.resize(kMaxRank);
    }
    SaveScores();
}

void ScoreManager::ResetAllScores()
{
    ranking_.clear();
    SaveScores();
}

void ScoreManager::LoadScores()
{
    ranking_.clear();
    std::ifstream file(kSaveFile);
    if (!file.is_open()) {
        return;
    }
    int score;
    while (file >> score) {
        ranking_.push_back(score);
    }
    std::sort(ranking_.begin(), ranking_.end(), std::greater<int>());
    if ((int)ranking_.size() > kMaxRank) {
        ranking_.resize(kMaxRank);
    }
}

void ScoreManager::SaveScores()
{
    std::ofstream file(kSaveFile);
    for (int s : ranking_) {
        file << s << "\n";
    }
}
