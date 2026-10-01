/**
 * @file StageEditorContentPanels.cpp
 * @brief ステージエディタの各種ImGuiパネル（ヒエラルキー・ツールバー・インスペクタ・
 * アセットパレット・フラグ・制作ワークフロー・ノーコードイベント・Wave・解析・差分・ヘルプ）を実装するファイル
 * @note StageEditor.cppからの分割ファイルクラス自体はStageEditorのまま、定義の置き場所だけを分けている
 */
#include "StageEditor.h"
#ifdef USE_IMGUI
#include "Camera.h"
#include "DiagnosticsDraw.h"
#include "EditorUI.h"
#include "EnemyEntity.h"
#include "GameFlags.h"
#include "GraphEditor.h"
#include "KnightEnemy.h"
#include "StageEditorPanels.h"
#include "SceneManager.h"
#include "StageEditorPrefabService.h"
#include "StageEditorUiStyle.h"
#include "WinApp.h"
#include <algorithm>
#include <cfloat>
#include <iterator>
#include <imgui.h>
#endif
using namespace engine::game;
using namespace engine;
using namespace engine::graphics;

#ifdef USE_IMGUI

namespace {
namespace EditorUi = engine::game::StageEditorUiStyle;

constexpr ImVec2 kContentWindowSize = { 700.0f, 470.0f };
constexpr ImVec2 kContentWindowMinSize = { 320.0f, 240.0f };
constexpr ImVec4 kModeLabelColor = { 0.55f, 0.78f, 1.0f, 1.0f };
constexpr ImVec4 kUnsavedColor = { 1.0f, 0.8f, 0.35f, 1.0f };
constexpr ImVec4 kSavedColor = { 0.5f, 0.8f, 0.65f, 1.0f };
constexpr ImVec4 kActiveToolColor = { 0.2f, 0.46f, 0.75f, 1.0f };
constexpr ImVec4 kStopPlayTestColor = { 0.65f, 0.32f, 0.15f, 1.0f };
constexpr ImVec4 kStartPlayTestColor = { 0.16f, 0.48f, 0.34f, 1.0f };
constexpr float kToolbarSnapStepWidth = 55.0f;
constexpr float kDebugStartSceneComboWidth = 240.0f;
constexpr ImVec2 kPlayTestOverlayPos = { 10.0f, 10.0f };
constexpr float kPlayTestOverlayAlpha = 0.85f;
constexpr float kAssetTileHeight = 34.0f;
// アセットパレットに並べる配置物プリセット（モデル+テクスチャが対応済みの組み合わせのみ収録）
struct AssetPreset {
    const char* label;
    const char* model;
    const char* texture;
};

constexpr AssetPreset kAssetPresets[] = {
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
} // namespace

void StageEditor::DrawHierarchyEntry(int index, int depthLevel)
{
    if (depthLevel > 8) {
        return;
    } // 循環参照の安全弁

    const ObjectDesc& desc = objects_[index].desc;
    bool sel = std::find(selectedObjectIndices_.begin(), selectedObjectIndices_.end(), index)
        != selectedObjectIndices_.end();

    // 種類が一目で分かるようタグを付ける（配置物はタグ無し）
    const char* kindTag = (desc.kind == "enemy_knight") ? "[ナイト] "
        : (desc.kind == "enemy_basic")                  ? "[エネミー] "
        : (desc.kind == "ui_text")                      ? "[テキスト] "
        : (desc.kind == "hud_anchor")                   ? "[HUD位置] "
        : (desc.kind == "pickup")                       ? "[収集物] "
        : (desc.kind == "breakable")                    ? "[壊せる物] "
                                                        : "";

    // 深さぶんインデントして親子関係を視覚化する
    char label[128];
    std::string indent(static_cast<size_t>(depthLevel) * 2, ' ');
    snprintf(label, sizeof(label), "%s%s%s%s##obj%d",
        indent.c_str(), (depthLevel > 0) ? "└ " : "", kindTag, desc.name.c_str(), index);
    if (ImGui::Selectable(label, sel)) {
        selKind_ = SelKind::Object;
        selIndex_ = index;
        if (!ImGui::GetIO().KeyCtrl) {
            selectedObjectIndices_.clear();
        }
        auto selected = std::find(selectedObjectIndices_.begin(), selectedObjectIndices_.end(), index);
        if (selected == selectedObjectIndices_.end()) {
            selectedObjectIndices_.push_back(index);
        } else if (ImGui::GetIO().KeyCtrl) {
            selectedObjectIndices_.erase(selected);
        }
    }
    // ダブルクリックでその配置物へカメラを寄せる（画面外の物を探しに行く手間を省く）
    const bool screenSpaceObject = (desc.kind == "ui_text" && desc.textSpace == "screen") || desc.kind == "hud_anchor";
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !screenSpaceObject) {
        FocusCameraOn(WorldPositionOf(desc));
    }

    // 階層内のドラッグ＆ドロップで親子付け（他の項目へ落とすとその子になる。見た目の位置は変わらない）
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
        ImGui::SetDragDropPayload("STAGE_OBJECT", &index, sizeof(int));
        ImGui::Text("%s を親にする物の上へ", desc.name.c_str());
        ImGui::EndDragDropSource();
    }
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("STAGE_OBJECT")) {
            const int childIndex = *static_cast<const int*>(payload->Data);
            SetParentPreservingWorld(childIndex, index);
        }
        ImGui::EndDragDropTarget();
    }

    // このエントリを親にしている子を直下に描く
    for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
        if (i != index && objects_[i].desc.parent == desc.name) {
            DrawHierarchyEntry(i, depthLevel + 1);
        }
    }
}

void StageEditor::RenderHierarchy()
{
    StageEditorHierarchyPanel::Render(*this);
}

void StageEditor::RenderGameViewport()
{
    viewport_.ClearImageRect();
    DiagnosticsDraw::SetImageViewport();
    if (viewportFocusMode_ || previewTextureId_ == 0) { return; }

    ImGui::SetNextWindowPos(ImVec2(kLeftPanelWidth, kToolbarHeight), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(kContentWindowSize, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(kContentWindowMinSize, ImVec2(FLT_MAX, FLT_MAX));
    ImGui::Begin("ゲームビュー", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::TextDisabled("タイトルバーで移動 / 右下でサイズ変更 / F4で大きく表示");
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float ratio = static_cast<float>(WinApp::kClientWidth) / WinApp::kClientHeight;
    const float width = (std::max)(1.0f, (std::min)(available.x, available.y * ratio));
    const ImVec2 size(width, width / ratio);
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const ImVec2 origin(cursor.x + (std::max)(0.0f, (available.x - size.x) * 0.5f),
        cursor.y + (std::max)(0.0f, (available.y - size.y) * 0.5f));
    ImGui::SetCursorScreenPos(origin);
    ImGui::Image((ImTextureID)previewTextureId_, size, { previewU0_, previewV0_ }, { previewU1_, previewV1_ });
    viewport_.SetImageRect(origin.x, origin.y, size.x, size.y, ImGui::IsItemHovered());
    DiagnosticsDraw::SetImageViewport(origin, size, ImGui::GetWindowDrawList());
    ImGui::End();
}
void StageEditor::RenderEditorToolbar()
{
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(WinApp::kClientWidth), kToolbarHeight), ImGuiCond_Always);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar;
    ImGui::Begin("シーンビューツールバー", nullptr, flags);

    ImGui::TextColored(kModeLabelColor, playTestMode_ ? "テスト中" : "ステージ編集");
    ImGui::SameLine();
    if (ImGui::Button("保存 (Ctrl+S)")) {
        Save();
    }
    ImGui::SameLine();
    ImGui::TextColored(dirty_ ? kUnsavedColor : kSavedColor,
        dirty_ ? "未保存" : "保存済み");
    ImGui::SameLine();
    ImGui::BeginDisabled(!history_.CanUndo());
    if (ImGui::Button("戻す")) { Undo(); }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!history_.CanRedo());
    if (ImGui::Button("やり直す")) { Redo(); }
    ImGui::EndDisabled();
    ImGui::SameLine();
    auto toolButton = [&](const char* label, TransformTool tool) {
        const bool active = transformTool_ == tool;
        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Button, kActiveToolColor);
        }
        if (ImGui::Button(label)) { transformTool_ = tool; }
        if (active) { ImGui::PopStyleColor(); }
        ImGui::SameLine();
    };
    ImGui::BeginDisabled(playTestMode_);
    toolButton("移動 W", TransformTool::Move);
    toolButton("回転 E", TransformTool::Rotate);
    toolButton("拡縮 R", TransformTool::Scale);
    ImGui::Checkbox("座標を揃える", &snapEnabled_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(kToolbarSnapStepWidth);
    ImGui::DragFloat("##toolbarSnapStep", &snapStep_, EditorUi::kDragStepPosition, EditorUi::kMinSnapStep, EditorUi::kMaxSnapStep, "%.1f");
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, playTestMode_ ? kStopPlayTestColor : kStartPlayTestColor);
    if (ImGui::Button(playTestMode_ ? "編集へ戻る" : "遊んで確認")) { SetPlayTestMode(!playTestMode_); }
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::BeginDisabled(playTestMode_);
    if (ImGui::Button("文章UIを追加")) { AddUITextAt(EditorUi::kScreenCenterPosition); }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("追加機能")) { ImGui::OpenPopup("編集追加機能"); }
    if (ImGui::BeginPopup("編集追加機能")) {
#ifdef _DEBUG
        ImGui::SeparatorText("Debugの開始位置");
        constexpr const char* scenes[] = { "TITLE", "MAP", "GAMEPLAY", "TRAINING", "BATTLETEST", "OPTIONS" };
        constexpr const char* labels[] = { "タイトル", "マップ選択", "本編の最初のステージ", "トレーニング", "戦闘テスト", "オプション" };
        auto* manager = SceneManager::GetInstance();
        constexpr int kSceneCount = static_cast<int>(std::size(scenes));
        int selected = 0;
        for (int i = 0; i < kSceneCount; ++i) {
            if (manager->GetDebugStartScene() == scenes[i]) { selected = i; }
        }
        ImGui::SetNextItemWidth(kDebugStartSceneComboWidth);
        if (ImGui::Combo("開始画面", &selected, labels, kSceneCount)) {
            const bool saved = manager->SetDebugStartScene(scenes[selected]);
            statusMessage_ = saved ? std::string("次回のDebug起動は「") + labels[selected] + "」から開始します"
                                   : "Debugの開始位置を保存できませんでした";
            statusTimer_ = StageEditor::kStatusLongSeconds;
        }
        ImGui::TextDisabled("自動保存され、次回のDebug起動に反映します");
        ImGui::Separator();
#endif
        ImGui::MenuItem("制作・保存前チェック", nullptr, &showWorkflowPanel_);
        ImGui::MenuItem("イベントをつなぐ", nullptr, &showNoCodeEventPanel_);
        ImGui::MenuItem("敵の出現を作る", nullptr, &showWavePanel_);
        ImGui::Separator();
        ImGui::MenuItem("フラグを確認", nullptr, &showFlagsPanel_);
        ImGui::MenuItem("グラフを編集", nullptr, &showGraphPanel_);
        ImGui::MenuItem("配置したテキストを表示", nullptr, &showUIText_);
        ImGui::MenuItem("グリッド線を表示", nullptr, &showGrid_);
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("使い方")) { showEditorHelp_ = true; }
    ImGui::SameLine();
    if (ImGui::Button("広く見る F4")) { viewportFocusMode_ = true; }

    ImGui::SameLine();
    if (statusTimer_ > 0.0f) {
        ImGui::TextUnformatted(statusMessage_.c_str());
    } else if (playTestMode_) {
        ImGui::TextUnformatted("ゲーム操作で確認 / 「編集へ戻る」で調整を再開");
    } else if (selKind_ == SelKind::None) {
        ImGui::TextUnformatted("素材で配置 / クリックで選択 / 右ボタン+WASDでカメラ / ホイールでズーム");
    } else {
        ImGui::TextUnformatted("つかんで移動 / Fでカメラ / Ctrl+Dで複製 / Ctrl+Zで戻す");
    }
    ImGui::End();
}
void StageEditor::RenderViewportFocusBar()
{
    // ゲーム画面を遮る範囲を抑えつつ、通常レイアウトへ戻る操作だけを残す
    ImGui::SetNextWindowPos(kPlayTestOverlayPos, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(kPlayTestOverlayAlpha);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize
        | ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin("画面優先モード", nullptr, flags);
    ImGui::TextDisabled("編集状態とギズモを維持してパネルを隠している");
    if (ImGui::Button("編集パネルを表示 (F4)")) {
        viewportFocusMode_ = false;
    }
    ImGui::End();
}

void StageEditor::RenderInspector()
{
    StageEditorInspectorPanel::Render(*this);
}

void StageEditor::RenderAssetPalette()
{
    const float availableHeight = static_cast<float>(WinApp::kClientHeight) - kToolbarHeight;
    const float hierarchyHeight = availableHeight * EditorUi::kHierarchyHeightRatio;
    ImGui::SetNextWindowPos(ImVec2(0.0f, kToolbarHeight + hierarchyHeight), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(kLeftPanelWidth, availableHeight - hierarchyHeight), ImGuiCond_Always);
    ImGui::Begin("素材を配置", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

    // どちらの動作になるかを隠れた自動判定にせず、ラジオボタンで明示的に選ばせる
    bool hasPropSel = (selKind_ == SelKind::Object && selIndex_ >= 0
        && selIndex_ < static_cast<int>(objects_.size())
        && objects_[selIndex_].desc.kind == "prop");

    ImGui::RadioButton("新規配置", &paletteMode_, 0);
    ImGui::SameLine();
    ImGui::BeginDisabled(!hasPropSel);
    ImGui::RadioButton("見た目を変更", &paletteMode_, 1);
    ImGui::EndDisabled();
    EditorUI::HelpMarker("新規配置: クリックしたプリセットを画面中央に追加します\n選択へ差し替え: 選択中の配置物のモデルを置き換えます（配置物を選択中のみ有効）");

    // 差し替え対象がなくなった時に、意図せず新規配置しないようモードを戻す。
    if (!hasPropSel) { paletteMode_ = 0; }
    bool applyToSelection = (paletteMode_ == 1 && hasPropSel);
    ImGui::TextDisabled(applyToSelection ? "クリックで選択中の配置物のモデルを差し替え" : "クリックで画面中央に新規配置");

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##素材検索", "素材名で検索", paletteSearch_, sizeof(paletteSearch_));
    const float tileWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    int visibleCount = 0;
    ImGui::BeginDisabled(playTestMode_);
    for (const auto& preset : kAssetPresets) {
        if (paletteSearch_[0] != '\0' && std::string(preset.label).find(paletteSearch_) == std::string::npos) {
            continue;
        }
        if (visibleCount++ % 2 != 0) { ImGui::SameLine(); }
        if (ImGui::Button(preset.label, ImVec2(tileWidth, kAssetTileHeight))) {
            if (applyToSelection) {
                RecordUndoSnapshotNow();
                ObjectEntry& entry = objects_[selIndex_];
                entry.desc.model = preset.model;
                entry.desc.texture = preset.texture;
                RegenerateInstances(entry);
            } else {
                AddPropAtScreenCenter(preset.model, preset.texture);
            }
        }
    }
    ImGui::EndDisabled();
    if (visibleCount == 0) {
        ImGui::TextWrapped("一致する素材がありません。検索語を短くしてみてください。");
    }

    ImGui::End();
}

void StageEditor::SaveSelectedPrefab()
{
    if (selKind_ != SelKind::Object || selIndex_ < 0 || selIndex_ >= static_cast<int>(objects_.size())) {
        return;
    }
    const std::string path = StageEditorPrefabService::Save(prefabName_, objects_[selIndex_].desc);
    statusMessage_ = "プレハブを保存しました: " + path;
    statusTimer_ = StageEditor::kStatusShortSeconds;
}

void StageEditor::InstantiatePrefab()
{
    const std::string path = StageEditorPrefabService::MakePath(prefabName_);
    std::vector<ObjectDesc> prefabObjects = StageEditorPrefabService::Load(prefabName_);
    if (prefabObjects.empty()) {
        statusMessage_ = "プレハブが見つかりません: " + path;
        statusTimer_ = StageEditor::kStatusShortSeconds;
        return;
    }

    RecordUndoSnapshotNow();
    Vector3 center = playerSpawn_;
    center = ViewCenterOnGround();
    for (ObjectDesc desc : prefabObjects) {
        ObjectEntry entry;
        entry.desc = std::move(desc);
        entry.desc.name = "prefab_" + std::to_string(nextSerial_++);
        entry.desc.parent.clear();
        entry.desc.position = center + entry.desc.position;
        objects_.push_back(std::move(entry));
        RegenerateInstances(objects_.back());
    }
    selKind_ = SelKind::Object;
    selIndex_ = static_cast<int>(objects_.size()) - 1;
    selectedObjectIndices_ = { selIndex_ };
    statusMessage_ = "プレハブを配置しました";
    statusTimer_ = StageEditor::kStatusShortSeconds;
}

#else

void StageEditor::RenderWorkflowPanel() { }
void StageEditor::RenderViewportContextMenu() { }
void StageEditor::RenderStageAnalysisPanel() { }
void StageEditor::RenderDiffPanel() { }
void StageEditor::RenderEditorHelpPanel() { }
void StageEditor::RenderNoCodeEventPanel() { }
void StageEditor::RenderGraphPanel() { }
void StageEditor::RenderWavePanel() { }
void StageEditor::RenderHierarchy() { }
void StageEditor::RenderEditorToolbar() { }
void StageEditor::RenderViewportFocusBar() { }
void StageEditor::RenderGameViewport() { }
void StageEditor::RenderInspector() { }
void StageEditor::RenderAssetPalette() { }

void StageEditor::DrawHierarchyEntry(int, int) { }
#endif
