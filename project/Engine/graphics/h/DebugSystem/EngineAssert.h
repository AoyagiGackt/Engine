/**
 * @file EngineAssert.h
 * @brief assert()の代替マクロ条件が偽の場合にログファイルへ記録してから通常のassertを実行する
 * @note NDEBUGを定義していないため通常のassert()もRelease構成含め常に有効だが、
 * ダイアログ表示だけでは「何が」「どこで」失敗したかがログに残らない
 * ENGINE_ASSERTはその文脈を log/engine.log に残してから同じように停止させる
 */
#pragma once
#include "Logger.h"
#include <cassert>
#include <string>

#define ENGINE_ASSERT(expr)                                                                                                               \
    do {                                                                                                                                  \
        bool engineAssertResult_ = static_cast<bool>(expr);                                                                               \
        if (!engineAssertResult_) {                                                                                                       \
            engine::Logger::LogError(std::string("Assertion failed: ") + #expr + " (" + __FILE__ + ":" + std::to_string(__LINE__) + ")"); \
        }                                                                                                                                 \
        assert(engineAssertResult_);                                                                                                      \
    } while (0)
