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

// 階層循環への保護上限と奥行き方向のギズモ識別値
inline constexpr int kMaxHierarchyDepth = 8;
inline constexpr int kGizmoAxisFree = 0;
inline constexpr int kGizmoAxisX = 1;
inline constexpr int kGizmoAxisY = 2;
inline constexpr int kGizmoAxisZ = 3;
/** @brief ギズモの持ち手の数（X/Y/Z の3軸） */
inline constexpr int kGizmoAxisCount = 3;

/** @brief アセットパレットで使うモデルとテクスチャの組み合わせ */
struct AssetPreset {
    const char* label;
    const char* model;
    const char* texture;
};

inline constexpr AssetPreset kAssetPresets[] = {
    { "ブロック", "Resources/block/block.obj", "Resources/block/block.png" },
    { "剣", "Resources/Knight/OBJ/Sword.obj", "Resources/Knight/OBJ/SwordPalette.png" },
    { "刀", "Resources/Knight/OBJ/Katana.obj", "Resources/Knight/OBJ/KatanaPalette.png" },
    { "ナイト像", "Resources/Knight/OBJ/KnightCharacter.obj", "Resources/Knight/OBJ/KnightCharacterPalette.png" },
    { "短剣", "Resources/MedievalWeaponsPack/OBJ/Dagger.obj", "Resources/MedievalWeaponsPack/OBJ/DaggerPalette.png" },
    { "大槌", "Resources/MedievalWeaponsPack/OBJ/Hammer_Small.obj", "Resources/MedievalWeaponsPack/OBJ/Hammer_SmallPalette.png" },
    { "槍", "Resources/MedievalWeaponsPack/OBJ/Spear.obj", "Resources/MedievalWeaponsPack/OBJ/SpearPalette.png" },
    { "大剣", "Resources/MedievalWeaponsPack/OBJ/Claymore.obj", "Resources/MedievalWeaponsPack/OBJ/ClaymorePalette.png" },
    { "大鎌", "Resources/MedievalWeaponsPack/OBJ/Scythe.obj", "Resources/MedievalWeaponsPack/OBJ/ScythePalette.png" },
    { "両手斧", "Resources/MedievalWeaponsPack/OBJ/Axe_Double.obj", "Resources/MedievalWeaponsPack/OBJ/Axe_DoublePalette.png" },
};

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

// ビューポートでの選択・ドラッグ
inline constexpr float kZDragPerPixel = 0.0015f; // Shift+ドラッグのZ移動。マウス垂直1pxあたりの移動量（カメラ距離1.0・720p基準）
inline constexpr float kPickRadiusPx = 40.0f; // クリックで選ぶ最大の距離（画面px）。これより遠いものは選ばない

// 配置物の印（ワールド単位）
inline constexpr float kSpawnMarkerHalf = 0.4f; // spawn_pointを示す赤い枠の半径
inline constexpr float kMotionEndMarkerRadius = 0.2f; // ギミック可動域の端に出す十字の大きさ

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
