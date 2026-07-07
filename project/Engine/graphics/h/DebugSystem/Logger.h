/**
 * @file Logger.h
 * @brief ログ出力（デバッグ出力ウィンドウ + ログファイル）に関する関数を定義するファイル
 */
#pragma once
#include "DirectXCommon.h"

namespace engine {

/** @brief ログの重要度 */
enum class LogLevel { Info, Warning, Error };

class Logger {
public:
    /**
     * @brief ログメッセージをデバッグ出力ウィンドウとログファイル（log/engine.log）の両方に出力する
     * @param message 出力するメッセージ
     * @param level   重要度（省略時はInfo）
     */
    static void Log(const std::string& message, LogLevel level = LogLevel::Info);

    /** @brief Info（通常の進行状況）としてログを出力する */
    static void LogInfo(const std::string& message)    { Log(message, LogLevel::Info); }
    /** @brief Warning（異常だが継続可能）としてログを出力する */
    static void LogWarning(const std::string& message) { Log(message, LogLevel::Warning); }
    /** @brief Error（処理続行が困難な失敗）としてログを出力する */
    static void LogError(const std::string& message)   { Log(message, LogLevel::Error); }

private:
    /** @brief ログファイルの保存先パス */
    static constexpr const char* kLogFilePath = "log/engine.log";
};

} // namespace engine
