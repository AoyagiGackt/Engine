/**
 * @file PlayerBridge.cpp
 * @brief PlayerBridgeのイベントグラフのデータ、編集、実行に関する具体的な処理を実装するファイル
 */
#include "PlayerBridge.h"
using namespace engine::game;

PlayerBridge* PlayerBridge::GetInstance()
{
    static PlayerBridge instance;
    return &instance;
}
