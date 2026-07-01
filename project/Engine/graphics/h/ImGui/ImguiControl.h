/**
 * @file ImGuiControl.h
 * @brief 開発・デバッグ用のImGuiコントロールパネル
 */
#pragma once
#include "Object3dCommon.h"
#include "MakeAffine.h"
#include <functional>
#include <vector>
namespace engine::graphics {

/// @brief ImGuiパネルで設定するデバッグ用ポイントライト
struct DebugPointLight {
    bool    enabled   = true;
    Vector3 position  = { 0.f, 2.f, 0.f };
    float   radius    = 10.f;
    Vector4 color     = { 1.f, 1.f, 1.f, 1.f };
    float   intensity = 2.f;
};

/// @brief デバッグImGuiコントロールウィンドウを管理するクラス
class ImGuiControlPanel {
public:
    /// @brief 初期化時に一度だけ呼ぶ（Object3dCommon を登録する）
    static void RegisterObject3dCommon(Object3dCommon* common);

    /// @brief ガラス割れエフェクトのテスト再生コールバックを登録する（シーン切替時は nullptr で解除する）
    static void RegisterGlassShatterTrigger(std::function<void()> trigger);

    /// @brief ImGuiで設定されたデバッグポイントライト一覧を返す
    static const std::vector<DebugPointLight>& GetDebugPointLights();

    /// @brief デバッグコントロールウィンドウを描画する（ImGuiManager::Begin/Endの間に呼ぶ）
    static void ShowControls();
};

} // namespace engine::graphics
