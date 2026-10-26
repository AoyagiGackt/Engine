/**
 * @file PlaytestLog.cpp
 * @brief PlaytestLogの実装（CSVへの追記）
 */
#include "PlaytestLog.h"
#include "TextBufferCapacity.h"
#include "RunData.h"
#include <chrono>
#include <ctime>
#include <fstream>

namespace engine::game {
namespace {
constexpr int kTmYearBase = 1900; // std::tm の年は1900年からの年数
constexpr int kTmMonthBase = 1; // std::tm の月は0始まり

/** @brief ファイル名に使う今の日時の文字列を返す */
std::string CurrentTimestamp() {
    std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local;
    localtime_s(&local, &t);
    char buf[textBufferCapacity::kShortText];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
        local.tm_year + kTmYearBase, local.tm_mon + kTmMonthBase, local.tm_mday,
        local.tm_hour, local.tm_min, local.tm_sec);
    return buf;
}
} // namespace

/** @brief 1回のランの結果を1行追記する */
void PlaytestLog::RecordRunResult(bool cleared, int floor, float elapsedSeconds,
    float peakStyle01, int bestChain, const Vector3& position) {
#ifdef ENGINE_RELEASE
    // 配布ビルドでは記録を残さない
    (void)cleared;
    (void)floor;
    (void)elapsedSeconds;
    (void)peakStyle01;
    (void)bestChain;
    (void)position;
    return;
#else
    // ファイルが無いか空なら、最初に列名の行を書く
    if (!headerWritten_) {
        // 開けない（無い）か、最初の1文字が終端なら空のファイル
        std::ifstream check(kLogFile);
        const bool needsHeader = !check.good() || check.peek() == std::ifstream::traits_type::eof();
        // 書き込み用に開き直す前に、読み込み側を閉じておく
        check.close();
        if (needsHeader) {
            std::ofstream header(kLogFile);
            header << "timestamp,result,floor,elapsed_seconds,peak_style,rank,best_chain,pos_x,pos_y,pos_z\n";
        }
        // 確認は起動中に1回だけでよい
        headerWritten_ = true;
    }

    // 1ランの結果を CSV の1行として追記する
    std::ofstream file(kLogFile, std::ios::app);
    // 開けなければ記録をあきらめる（ゲームの進行は止めない）
    if (!file.is_open()) {
        return;
    }
    // 列の並びは上の列名の行と同じ順にする
    file << CurrentTimestamp() << ','
         << (cleared ? "clear" : "gameover") << ','
         << floor << ','
         << elapsedSeconds << ','
         << peakStyle01 << ','
         << RunData::CalcRank(peakStyle01) << ','
         << bestChain << ','
         << position.x << ',' << position.y << ',' << position.z << '\n';
#endif
}

} // namespace engine::game
