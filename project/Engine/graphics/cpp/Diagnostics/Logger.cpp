/**
 * @file Logger.cpp
 * @brief ログの出力（Logger）の実装
 */
#include "Logger.h"
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace {

// 時刻文字列の容量
constexpr size_t kTimeStringCapacity = 16;

}

namespace engine {

namespace {

    /** @brief ログの重要度に対応する表示タグを返す */
    const char* LevelToTag(LogLevel level) {
        // LogLevel の並び順
        static constexpr const char* kTags[] = { "INFO", "WARN", "ERROR" };
        const size_t index = static_cast<size_t>(level);
        return index < std::size(kTags) ? kTags[index] : kTags[0];
    }

    /** @brief ログに付ける今の時刻の文字列を返す */
    std::string CurrentTimeString() {
        std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::tm local;
        localtime_s(&local, &t);
        char buf[kTimeStringCapacity];
        std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", local.tm_hour, local.tm_min, local.tm_sec);
        return buf;
    }

} // namespace

/** @brief ログメッセージをデバッグ出力ウィンドウとログファイル（log/engine.log）の両方に出力する */
void Logger::Log(const std::string& message, LogLevel level) {
    std::string line = "[" + CurrentTimeString() + "][" + LevelToTag(level) + "] " + message + "\n";

    OutputDebugStringA(line.c_str());

#ifndef ENGINE_RELEASE
    auto parent = std::filesystem::path(kLogFilePath).parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }

    // このプロセスで最初の書き込み時だけファイルを空にする（前回起動分の肥大化を防ぐ）
    static bool sIsFirstWrite = true;
    std::ofstream file(kLogFilePath, sIsFirstWrite ? std::ios::trunc : std::ios::app);
    sIsFirstWrite = false;

    if (file) {
        file << line;
    }
#endif
}

} // namespace engine
