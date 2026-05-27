/**
 * @file ImguiControl.h
 * @brief 開発・デバッグ用のImGuiコントロールパネル（UI）を表示するためのファイル
 */
#pragma once
#include "Object3dCommon.h"
#include "MakeAffine.h"
#include <vector>

// =====================================================
// ポイントライト デバッグ設定
// =====================================================

/**
 * @brief ImGui パネルで定義するデバッグ用ポイントライトの設定
 *
 * GetDebugPointLights() で取得して毎フレーム Object3dCommon に適用する。
 *
 * 使い方（シーンの Update など）:
 * @code
 *   obj3dCommon_->ClearPointLights();
 *   for (auto& dbgLight : GetDebugPointLights()) {
 *       if (!dbgLight.enabled) { continue; }
 *       Object3dCommon::PointLight pl;
 *       pl.position  = dbgLight.position;
 *       pl.radius    = dbgLight.radius;
 *       pl.color     = dbgLight.color;
 *       pl.intensity = dbgLight.intensity;
 *       obj3dCommon_->AddPointLight(pl);
 *   }
 * @endcode
 */
struct DebugPointLight {
    bool    enabled  = true;
    Vector3 position = { 0.f, 2.f, 0.f };
    float   radius   = 10.f;
    Vector4 color    = { 1.f, 1.f, 1.f, 1.f };
    float   intensity = 2.f;
};

/**
 * @brief ImGui で設定されたデバッグポイントライト一覧を取得する
 * @return enabled フラグ込みのライスト（毎フレーム AddPointLight に渡す）
 */
const std::vector<DebugPointLight>& GetDebugPointLights();

// =====================================================
// Object3dCommon の登録
// =====================================================

/**
 * @brief ImGui コントロールパネルが使用する Object3dCommon を登録する
 * @note 初期化時に一度だけ呼ぶこと
 *
 * 使い方:
 * @code
 *   RegisterObject3dCommon(obj3dCommon_.get());  // または生ポインタ
 * @endcode
 */
void RegisterObject3dCommon(Object3dCommon* common);

// =====================================================
// ImGui ウィンドウ描画
// =====================================================

/**
 * @brief ImGuiを使用したデバッグ用コントロールウィンドウを描画する
 * @note 毎フレームの更新処理（Update）内で、ImGuiManager::Begin() と ImGuiManager::End() の間に呼び出すこと。
 * この関数を呼ぶと、画面上にメッシュの切り替え、トランスフォーム（座標・回転・スケール）の操作、
 * マテリアルカラーやライティングモードの変更を行うためのUIウィンドウが表示されます。
 */
void ShowControls();
