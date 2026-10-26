/**
 * @file PlayerBridge.cpp
 * @brief ノードグラフからプレイヤーを操作するための橋渡し（PlayerBridge）の実装
 */
#include "PlayerBridge.h"

namespace engine::game {
/** @brief 唯一のPlayerBridgeインスタンスを取得する（未生成なら生成する） */
PlayerBridge* PlayerBridge::GetInstance() {
    static PlayerBridge sInstance;
    return &sInstance;
}

} // namespace engine::game
