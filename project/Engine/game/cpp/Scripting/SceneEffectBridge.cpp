/**
 * @file SceneEffectBridge.cpp
 * @brief SceneEffectBridgeのインスタンス管理を実装するファイル
 */
#include "SceneEffectBridge.h"

namespace engine::game {
/** @brief 唯一のSceneEffectBridgeインスタンスを取得する（未生成なら生成する） */
SceneEffectBridge* SceneEffectBridge::GetInstance() {
    static SceneEffectBridge sInstance;
    return &sInstance;
}

} // namespace engine::game
