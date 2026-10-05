/**
 * @file PlaytestLog.cpp
 * @brief PlaytestLogの実装（CSVへの追記）
 */
#include "PlaytestLog.h"
#include "RunData.h"
#include <chrono>
#include <ctime>
#include <fstream>
using namespace engine::game;

namespace {
std::string CurrentTimestamp()
{
    std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local;
    localtime_s(&local, &t);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
        local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
        local.tm_hour, local.tm_min, local.tm_sec);
    return buf;
}
} // namespace

void PlaytestLog::RecordRunResult(bool cleared, int floor, float elapsedSeconds,
    float peakStyle01, int bestChain, const Vector3& position)
{
#ifdef ENGINE_RELEASE
    (void)cleared;
    (void)floor;
    (void)elapsedSeconds;
    (void)peakStyle01;
    (void)bestChain;
    (void)position;
    return;
#else
    if (!headerWritten_) {
        std::ifstream check(kLogFile);
        const bool needsHeader = !check.good() || check.peek() == std::ifstream::traits_type::eof();
        check.close();
        if (needsHeader) {
            std::ofstream header(kLogFile);
            header << "timestamp,result,floor,elapsed_seconds,peak_style,rank,best_chain,pos_x,pos_y,pos_z\n";
        }
        headerWritten_ = true;
    }

    std::ofstream file(kLogFile, std::ios::app);
    if (!file.is_open()) {
        return;
    }
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
