/**
 * @file Logger.cpp
 * @brief Loggerの描画資源とGPU処理の管理に関する具体的な処理を実装するファイル
 */
#include "Logger.h"
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>

namespace engine {

namespace {

    const char* LevelToTag(LogLevel level)
    {
        switch (level) {
        case LogLevel::Warning:
            return "WARN";
        case LogLevel::Error:
            return "ERROR";
        default:
            return "INFO";
        }
    }

    std::string CurrentTimeString()
    {
        std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::tm local;
        localtime_s(&local, &t);
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", local.tm_hour, local.tm_min, local.tm_sec);
        return buf;
    }

} // namespace

void Logger::Log(const std::string& message, LogLevel level)
{
    std::string line = "[" + CurrentTimeString() + "][" + LevelToTag(level) + "] " + message + "\n";

    OutputDebugStringA(line.c_str());

    auto parent = std::filesystem::path(kLogFilePath).parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }

    // このプロセスで最初の書き込み時だけファイルを空にする（前回起動分の肥大化を防ぐ）
    static bool isFirstWrite = true;
    std::ofstream file(kLogFilePath, isFirstWrite ? std::ios::trunc : std::ios::app);
    isFirstWrite = false;

    if (file) {
        file << line;
    }
}

} // namespace engine
