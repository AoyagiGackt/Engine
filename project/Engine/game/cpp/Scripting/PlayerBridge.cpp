/**
 * @file PlayerBridge.cpp
 * @brief PlayerBridgeが担当する処理を実装するファイル
 */
#include "PlayerBridge.h"
using namespace engine::game;

PlayerBridge* PlayerBridge::GetInstance()
{
    static PlayerBridge instance;
    return &instance;
}
