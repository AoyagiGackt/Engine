/**
 * @file Logger.h
 * @brief ログ出力（デバッグ用文字列出力）に関する関数を定義するファイル
 */
#pragma once
#include "DirectXCommon.h"

namespace engine {
class Logger {
public:
    // ログメッセージを出力ウィンドウに表示する（内部でOutputDebugStringを呼ぶ）
    static void Log(const std::string& message);
};
} // namespace engine
