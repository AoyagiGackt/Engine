/**
 * @file SceneEffectBridge.cpp
 * @brief SceneEffectBridgeのインスタンス管理を実装するファイル
 */
#include "SceneEffectBridge.h"
using namespace engine::game;

SceneEffectBridge* SceneEffectBridge::GetInstance()
{
    static SceneEffectBridge instance;
    return &instance;
}
