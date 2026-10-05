/**
 * @file StageEditorUiStyle.h
 * @brief ステージエディタの各パネルで共通に使う操作量・色・既定配置をまとめたファイル
 */
#pragma once
#include "GameConstants.h"
#include "Vector3.h"
#ifdef USE_IMGUI
#include <imgui.h>
#endif

namespace engine::game::StageEditorUiStyle {

// ImGuiのドラッグ操作1pxあたりの変化量
inline constexpr float kDragStepPosition = 0.1f;
inline constexpr float kDragStepRotation = 0.01f;
inline constexpr float kDragStepScale = 0.05f;
inline constexpr float kDragStepFine = 0.05f; // 半径・速度などの細かい値
inline constexpr float kDragStepRatio = 0.01f; // 0〜1の割合
inline constexpr float kDragStepSeconds = 0.1f;

// 編集値の共通の下限・上限
inline constexpr float kMinRadius = 0.1f;
inline constexpr float kMaxSpeed = 20.0f;
inline constexpr float kMaxRadius = 20.0f;
inline constexpr float kMinSnapStep = 0.1f;
inline constexpr float kMaxSnapStep = 10.0f;

// 画面中央（文章UIの初期配置）
inline constexpr Vector3 kScreenCenterPosition = { GameConstants::kScreenCenterX, GameConstants::kScreenCenterY, 0.0f };

// ヒエラルキーとアセットパレットの高さ配分（左列の高さに対するヒエラルキーの割合）
inline constexpr float kHierarchyHeightRatio = 0.62f;

// 同じ位置に重なった配置物とみなす距離
inline constexpr float kOverlapEpsilon = 0.01f;

#ifdef USE_IMGUI
// メッセージの色
inline constexpr ImVec4 kOkColor = { 0.4f, 1.0f, 0.5f, 1.0f };
inline constexpr ImVec4 kWarningColor = { 1.0f, 0.85f, 0.4f, 1.0f };
inline constexpr ImVec4 kNoticeColor = { 1.0f, 0.85f, 0.2f, 1.0f };
inline constexpr ImVec4 kChangedColor = { 1.0f, 0.85f, 0.3f, 1.0f };
inline constexpr ImVec4 kErrorColor = { 1.0f, 0.4f, 0.4f, 1.0f };
inline constexpr ImVec4 kCautionColor = { 1.0f, 0.7f, 0.3f, 1.0f };
inline constexpr ImVec4 kHeadingColor = { 0.6f, 0.85f, 1.0f, 1.0f };
inline constexpr ImVec4 kMissingSourceColor = { 1.0f, 0.55f, 0.3f, 1.0f };
inline constexpr ImVec4 kFlagOnColor = { 0.5f, 1.0f, 0.6f, 1.0f };
inline constexpr ImVec4 kFlagOffColor = { 0.6f, 0.6f, 0.6f, 1.0f };

// 標準のボタン幅
inline constexpr float kWideButtonWidth = 140.0f;
inline constexpr float kButtonWidth = 120.0f;
#endif

} // namespace engine::game::StageEditorUiStyle
