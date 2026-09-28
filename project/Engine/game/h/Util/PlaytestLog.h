/**
 * @file PlaytestLog.h
 * @brief プレイ1回ごとの結果（死亡/クリア・フロア・経過時間・到達ランク等）をCSVへ追記するロガー
 */
#pragma once
#include "Vector3.h"
#include <string>
namespace engine::game {

/**
 * @brief プレイテストの手応えを数値で振り返るための実行ログ
 * @note 演出やバランス調整の判断材料にするため、GamePlaySceneがGAMEOVER/CLEARへ
 *       遷移する直前に1行だけ記録する。ファイルはExe実行ディレクトリ直下の
 *       playtest_log.csv に追記され、存在しなければヘッダー行から作成される
 */
class PlaytestLog {
public:
    static PlaytestLog* GetInstance()
    {
        static PlaytestLog instance;
        return &instance;
    }

    /**
     * @brief 1回のランの結果を1行追記する
     * @param cleared        true=最終フロアクリア, false=HP0でのゲームオーバー
     * @param floor          結果に至った時点のフロア番号（0始まり）
     * @param elapsedSeconds そのフロアでの滞在時間（秒）
     * @param peakStyle01    このランでの最高スタイルゲージ（0.0〜1.0、RunData::CalcRankにそのまま渡せる値）
     * @param bestChain      最大ヒットチェーン数
     * @param position       結果に至った時点のプレイヤー座標（死因の位置把握用）
     */
    void RecordRunResult(bool cleared, int floor, float elapsedSeconds,
        float peakStyle01, int bestChain, const Vector3& position);

private:
    PlaytestLog() = default;

    bool headerWritten_ = false; ///< 起動後に一度でも書き込んだら、ファイルの存在チェックを省略する
    static constexpr const char* kLogFile = "playtest_log.csv";
};

} // namespace engine::game
