/**
 * @file EngineAssert.h
 * @brief assert()の代わりのマクロ。条件が偽の場合にログファイルへ記録してから通常のassertを実行する
 * @note NDEBUGを定義していないため通常のassert()もRelease構成含め常に有効だが、
 * ダイアログ表示だけでは何がどこで失敗したかがログに残らない
 * ENGINE_ASSERTはその文脈を log/engine.log に残してから同じように停止させる
 */
#pragma once
#include "CrashBreadcrumbs.h"
#include "Logger.h"
#include <cassert>
#include <string>

#define ENGINE_ASSERT(expr)                                                                                                               \
    do                                                                                                                                    \
    {                                                                                                                                     \
        bool engineAssertResult_ = static_cast<bool>(expr);                                                                               \
        if (!engineAssertResult_)                                                                                                         \
        {                                                                                                                                 \
            const std::string engineAssertMessage_ = std::string(#expr) + " (" + __FILE__ + ":" + std::to_string(__LINE__) + ")"; \
            engine::Logger::LogError("Assertion failed: " + engineAssertMessage_); \
            engine::CrashBreadcrumbs::Add("assert", engineAssertMessage_); \
        }                                                                                                                                 \
        assert(engineAssertResult_);                                                                                                      \
    } while (0)
