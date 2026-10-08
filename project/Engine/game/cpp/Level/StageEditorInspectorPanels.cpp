/**
 * @file StageEditorInspectorPanels.cpp
 * @brief ステージエディタの「詳細設定」パネル（StageEditorInspectorPanel）の表示責務を実行する
 * @note StageEditorPanels.cppからの分割ファイル（StageEditorHierarchyPanelはStageEditorPanels.cppに残る）
 */
#ifdef USE_IMGUI
#include "StageEditorPanels.h"
#include "Camera.h"
#include "EditorUI.h"
#include "EnemyEntity.h"
#include "GameFlags.h"
#include "KnightEnemy.h"
#include "SceneShared.h"
#include "StageEditor.h"
#include "StageEditorUiStyle.h"
#include "WinApp.h"
#include <algorithm>
#include <commdlg.h>
#include <cstring>
#include <imgui.h>
#pragma comment(lib, "comdlg32.lib")

namespace {
namespace EditorUi = engine::game::StageEditorUiStyle;

constexpr float kAssetPathFieldWidth = 180.0f;
constexpr float kMaxTriggerRadius = 50.0f;

std::string OpenFileDialog(const char* filter, const char* initialDirectory)
{
    char path[MAX_PATH] = { };
    OPENFILENAMEA dialog = { };
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = path;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrInitialDir = initialDirectory;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameA(&dialog) ? std::string(path) : std::string { };
}

std::string ToProjectRelativePath(const std::string& absolutePath)
{
    std::string path = absolutePath;
    std::replace(path.begin(), path.end(), '\\', '/');
    const size_t resourcesPosition = path.find("Resources/");
    return resourcesPosition == std::string::npos ? path : path.substr(resourcesPosition);
}

} // namespace

namespace engine::game {
using namespace engine::graphics;

// screen座標のui_textが編集パネル（ツールバー/左カラム/右インスペクタ）の下に隠れて
// 3Dビュー上でドラッグできない場合に警告し、見える位置へ逃がすボタンを出す
void StageEditorInspectorPanel::RenderScreenAnchorOcclusionWarning(StageEditor& editor, ObjectDesc& desc)
{
    constexpr float kVisibleAreaTopMargin = 40.0f; // ツールバーのすぐ下は掴みにくいので少し余白を空ける
    const bool inWindow = editor.viewport_.IsImageMode();
    const float visibleLeft = inWindow ? 0.0f : StageEditor::kLeftPanelWidth;
    const float visibleRight = static_cast<float>(WinApp::kClientWidth) - (inWindow ? 0.0f : StageEditor::kRightPanelWidth);
    const float visibleTop = inWindow ? 0.0f : StageEditor::kToolbarHeight;
    const bool hiddenByPanel = desc.position.x < visibleLeft || desc.position.x > visibleRight || desc.position.y < visibleTop;
    if (!hiddenByPanel) {
        return;
    }
    ImGui::TextColored(EditorUi::kCautionColor, "この位置は編集パネルの下に隠れており、3Dビュー上ではドラッグできません");
    ImGui::TextDisabled("上の「位置」欄で直接数値を入力するか、下のボタンで一旦ドラッグできる位置へ移動してください");
    if (ImGui::Button("ドラッグできる位置へ移動")) {
        editor.RecordUndoSnapshotNow();
        desc.position.x = (visibleLeft + visibleRight) * 0.5f;
        desc.position.y = visibleTop + kVisibleAreaTopMargin;
    }
}

void StageEditorInspectorPanel::RenderObjectIdentity(StageEditor& editor, bool& structuralDirty)
{
    auto& entry = editor.objects_[editor.selIndex_];
    auto& desc = entry.desc;
    bool enabled = desc.enabled;
    if (ImGui::Checkbox("ゲーム側で有効", &enabled)) {
        editor.RecordUndoSnapshotNow();
        desc.enabled = enabled;
        structuralDirty = true;
    }

    // 名前（親子参照のキーなので、変更時は子の親参照も追従させる）
    {
        char nameBuf[96];
        strncpy_s(nameBuf, desc.name.c_str(), _TRUNCATE);
        bool changed = ImGui::InputText("名前", nameBuf, sizeof(nameBuf));
        if (ImGui::IsItemActivated()) {
            editor.BeginUndoCapture();
        }
        if (changed) {
            std::string newName = nameBuf;
            if (newName != desc.name && !newName.empty()) {
                editor.MarkUndoDirty();
                for (auto& other : editor.objects_) {
                    if (other.desc.parent == desc.name) {
                        other.desc.parent = newName;
                    }
                }
                desc.name = newName;
            }
        }
        if (ImGui::IsItemDeactivated()) {
            editor.CommitUndoCapture();
        }
    }

    // 親の選択（自分自身と自分の子孫は循環になるため選択肢から除外する）
    {
        std::string currentParent = desc.parent.empty() ? "(なし)" : desc.parent;
        if (ImGui::BeginCombo("親", currentParent.c_str())) {
            if (ImGui::Selectable("(なし)", desc.parent.empty())) {
                if (!desc.parent.empty()) {
                    editor.RecordUndoSnapshotNow();
                    // 親を外しても見た目の位置が変わらないよう、ワールド座標をローカルへ引き継ぐ
                    desc.position = editor.WorldPositionOf(desc);
                    desc.parent.clear();
                }
            }
            for (const auto& other : editor.objects_) {
                const std::string& name = other.desc.name;
                if (name == desc.name || editor.IsDescendantOf(name, desc.name)) {
                    continue;
                }
                if (ImGui::Selectable(name.c_str(), desc.parent == name)) {
                    if (desc.parent != name) {
                        editor.RecordUndoSnapshotNow();
                        // 付け替えても見た目の位置が変わらないよう、新しい親基準のローカル座標へ変換する
                        Vector3 world = editor.WorldPositionOf(desc);
                        desc.parent = name;
                        Vector3 parentW = editor.ParentWorldPositionOf(desc);
                        desc.position = Subtract(world, parentW);
                    }
                }
            }
            ImGui::EndCombo();
        }
    }
}

void StageEditorInspectorPanel::RenderObjectVisual(StageEditor& editor, bool& structuralDirty)
{
    auto& entry = editor.objects_[editor.selIndex_];
    auto& desc = entry.desc;
    const bool visualKind = ObjectKind::Of(desc.kind).IsVisual();
    if (visualKind) {
        ObjectKind::Of(desc.kind).DrawVisualNote();
        char modelBuf[256];
        strncpy_s(modelBuf, desc.model.c_str(), _TRUNCATE);
        ImGui::SetNextItemWidth(kAssetPathFieldWidth);
        bool modelChanged = ImGui::InputText("モデル", modelBuf, sizeof(modelBuf));
        if (ImGui::IsItemActivated()) {
            editor.BeginUndoCapture();
        }
        if (modelChanged) {
            editor.MarkUndoDirty();
            desc.model = modelBuf;
        }
        structuralDirty |= ImGui::IsItemDeactivatedAfterEdit();
        if (ImGui::IsItemDeactivated()) {
            editor.CommitUndoCapture();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("参照##model")) {
            std::string p = OpenFileDialog("3Dモデル\0*.obj;*.gltf;*.glb\0OBJ\0*.obj\0glTF\0*.gltf;*.glb\0すべてのファイル\0*.*\0\0", "Resources");
            if (!p.empty()) {
                editor.RecordUndoSnapshotNow();
                desc.model = ToProjectRelativePath(p);
                structuralDirty = true;
            }
        }

        char texBuf[256];
        strncpy_s(texBuf, desc.texture.c_str(), _TRUNCATE);
        ImGui::SetNextItemWidth(kAssetPathFieldWidth);
        bool texChanged = ImGui::InputText("テクスチャ", texBuf, sizeof(texBuf));
        if (ImGui::IsItemActivated()) {
            editor.BeginUndoCapture();
        }
        if (texChanged) {
            editor.MarkUndoDirty();
            desc.texture = texBuf;
        }
        structuralDirty |= ImGui::IsItemDeactivatedAfterEdit();
        if (ImGui::IsItemDeactivated()) {
            editor.CommitUndoCapture();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("参照##tex")) {
            std::string p = OpenFileDialog("画像ファイル\0*.png;*.jpg;*.jpeg\0すべてのファイル\0*.*\0\0", "Resources");
            if (!p.empty()) {
                editor.RecordUndoSnapshotNow();
                desc.texture = ToProjectRelativePath(p);
                structuralDirty = true;
            }
        }

        // PlacementType の並び順と一致させる
        const char* kTypeLabels[] = { "単体配置(static)", "並べて配置(row)" };
        int typeIdx = static_cast<int>(desc.type);
        if (ImGui::Combo("種類", &typeIdx, kTypeLabels, 2)) {
            editor.RecordUndoSnapshotNow();
            desc.type = static_cast<PlacementType>(typeIdx);
            structuralDirty = true;
        }
        EditorUI::HelpMarker("単体配置: 1つだけ置く\n並べて配置: 同じモデルを一定間隔で複数並べる（階段や壁に便利）");
    } else {
        const char* kindLabel = desc.kind.c_str();
        ImGui::TextDisabled("%s", kindLabel);
        if (entry.knight) {
            ImGui::Text("HP: %d / %d", entry.knight->GetHp(), entry.knight->GetMaxHp());
        } else if (entry.enemy) {
            ImGui::Text("HP: %d / %d", entry.enemy->GetHp(), entry.enemy->GetMaxHp());
        }
    }

    if (!desc.parent.empty()) {
        ImGui::TextDisabled("※位置は親からの相対値");
    }
}

void StageEditorInspectorPanel::RenderObjectTransform(
    StageEditor& editor, bool& structuralDirty, bool& transformDirty)
{
    auto& desc = editor.objects_[editor.selIndex_].desc;
    const bool visualKind = ObjectKind::Of(desc.kind).IsVisual();
    auto captureItemUndo = [&](bool changed) {
        if (ImGui::IsItemActivated()) {
            editor.BeginUndoCapture();
        }
        if (changed) {
            editor.MarkUndoDirty();
        }
        if (ImGui::IsItemDeactivated()) {
            editor.CommitUndoCapture();
        }
        return changed;
    };
    // ドラッグ系ウィジェットは操作開始で変更前を控え、離した時に1回分のUndoとして確定する

    const Vector3 previousPosition = desc.position;
    const bool positionChanged = captureItemUndo(ImGui::DragFloat3("位置", &desc.position.x, EditorUi::kDragStepPosition));
    transformDirty |= positionChanged;
    if (positionChanged && editor.selectedObjectIndices_.size() > 1) {
        const Vector3 delta = {
            desc.position.x - previousPosition.x,
            desc.position.y - previousPosition.y,
            desc.position.z - previousPosition.z
        };
        for (int index : editor.selectedObjectIndices_) {
            if (index >= 0 && index < static_cast<int>(editor.objects_.size()) && index != editor.selIndex_) {
                editor.objects_[index].desc.position = editor.objects_[index].desc.position + delta;
                editor.RefreshTransforms(editor.objects_[index]);
            }
        }
    }

    if (ObjectKind::Of(desc.kind).HasRotationAndScale()) {
        const Vector3 previousRotation = desc.rotation;
        const bool rotationChanged = captureItemUndo(ImGui::DragFloat3("回転", &desc.rotation.x, EditorUi::kDragStepRotation));
        transformDirty |= rotationChanged;
        const Vector3 previousScale = desc.scale;
        const bool scaleChanged = captureItemUndo(ImGui::DragFloat3("スケール", &desc.scale.x, EditorUi::kDragStepScale));
        transformDirty |= scaleChanged;
        if ((rotationChanged || scaleChanged) && editor.selectedObjectIndices_.size() > 1) {
            const Vector3 rotationDelta = {
                desc.rotation.x - previousRotation.x,
                desc.rotation.y - previousRotation.y,
                desc.rotation.z - previousRotation.z
            };
            const Vector3 scaleDelta = {
                desc.scale.x - previousScale.x,
                desc.scale.y - previousScale.y,
                desc.scale.z - previousScale.z
            };
            for (int index : editor.selectedObjectIndices_) {
                if (index < 0 || index >= static_cast<int>(editor.objects_.size()) || index == editor.selIndex_
                    || !ObjectKind::Of(editor.objects_[index].desc.kind).HasRotationAndScale()) {
                    continue;
                }
                if (rotationChanged) {
                    editor.objects_[index].desc.rotation = editor.objects_[index].desc.rotation + rotationDelta;
                }
                if (scaleChanged) {
                    editor.objects_[index].desc.scale = editor.objects_[index].desc.scale + scaleDelta;
                }
                editor.RefreshTransforms(editor.objects_[index]);
            }
        }
        {
            bool lighting = desc.lighting;
            if (ImGui::Checkbox("ライティング", &lighting)) {
                editor.RecordUndoSnapshotNow();
                desc.lighting = lighting;
                transformDirty = true;
            }
            bool solid = desc.solid;
            if (ImGui::Checkbox("当たり判定あり(solid)", &solid)) {
                editor.RecordUndoSnapshotNow();
                desc.solid = solid;
            }
            EditorUI::HelpMarker("ONにするとプレイヤーや敵が乗れる・ぶつかる足場になります（オレンジの枠で表示）");
        }

        ObjectKind::Of(desc.kind).DrawVisualExtras(editor, desc);

        if (desc.type == PlacementType::Row) {
            const char* axes[] = { "x", "y", "z" };
            int axisIndex = desc.axis == 'y' ? 1 : desc.axis == 'z' ? 2
                                                                    : 0;
            if (ImGui::Combo("並べる軸", &axisIndex, axes, 3)) {
                editor.RecordUndoSnapshotNow();
                desc.axis = axes[axisIndex][0];
                structuralDirty = true;
            }
            int count = desc.count;
            if (ImGui::InputInt("個数", &count)) {
                editor.RecordUndoSnapshotNow();
                desc.count = (std::max)(1, count);
                structuralDirty = true;
            }
            transformDirty |= captureItemUndo(ImGui::DragFloat("間隔", &desc.step, EditorUi::kDragStepFine));
        }
    }
}

bool StageEditorInspectorPanel::RenderObjectInspector(StageEditor& editor)
{
    if (editor.selKind_ != StageEditor::SelKind::Object || editor.selIndex_ < 0
        || editor.selIndex_ >= static_cast<int>(editor.objects_.size())) {
        return false;
    }
    bool structuralDirty = false;
    bool transformDirty = false;
    RenderObjectIdentity(editor, structuralDirty);
    // 文章UIは入力欄を先に出し、3Dモデル用の設定へ埋もれないようにする。
    RenderObjectText(editor);
    RenderObjectVisual(editor, structuralDirty);
    RenderObjectTransform(editor, structuralDirty, transformDirty);
    RenderObjectGameplay(editor, structuralDirty);

    auto& entry = editor.objects_[editor.selIndex_];
    const bool visualKind = ObjectKind::Of(entry.desc.kind).IsVisual();
    if (structuralDirty) {
        editor.RegenerateInstances(entry);
    } else if (transformDirty && visualKind) {
        editor.RefreshTransforms(entry);
    }
    return true;
}

bool StageEditorInspectorPanel::RenderTriggerInspector(StageEditor& editor)
{
    if (editor.selKind_ != StageEditor::SelKind::Trigger || editor.selIndex_ < 0
        || editor.selIndex_ >= static_cast<int>(editor.triggers_.size())) {
        return false;
    }
    TriggerDesc& desc = editor.triggers_[editor.selIndex_].GetDesc();

    auto captureItemUndo = [&](bool changed) {
        if (ImGui::IsItemActivated()) {
            editor.BeginUndoCapture();
        }
        if (changed) {
            editor.MarkUndoDirty();
        }
        if (ImGui::IsItemDeactivated()) {
            editor.CommitUndoCapture();
        }
        return changed;
    };

    char nameBuf[96];
    strncpy_s(nameBuf, desc.name.c_str(), _TRUNCATE);
    bool nameChanged = ImGui::InputText("名前", nameBuf, sizeof(nameBuf));
    if (captureItemUndo(nameChanged)) {
        desc.name = nameBuf;
    }

    char flagBuf[96];
    strncpy_s(flagBuf, desc.flag.c_str(), _TRUNCATE);
    bool flagChanged = ImGui::InputText("フラグ名", flagBuf, sizeof(flagBuf));
    if (captureItemUndo(flagChanged)) {
        desc.flag = flagBuf;
    }
    EditorUI::HelpMarker("プレイヤーが球に入ると、この名前のフラグが立ちます。\nノードエディタ(F1)のGetFlagノードで参照できます");

    captureItemUndo(ImGui::DragFloat3("位置", &desc.position.x, EditorUi::kDragStepPosition));
    captureItemUndo(ImGui::DragFloat("半径", &desc.radius, EditorUi::kDragStepFine, EditorUi::kMinRadius, kMaxTriggerRadius));
    {
        bool value = desc.value;
        if (ImGui::Checkbox("進入時に設定する値", &value)) {
            editor.RecordUndoSnapshotNow();
            desc.value = value;
        }
        bool once = desc.once;
        if (ImGui::Checkbox("一度だけ成立させる", &once)) {
            editor.RecordUndoSnapshotNow();
            desc.once = once;
        }
        bool spawnsWaterSplash = desc.spawnsWaterSplash;
        if (ImGui::Checkbox("この地点に来たら水しぶきを出す", &spawnsWaterSplash)) {
            editor.RecordUndoSnapshotNow();
            desc.spawnsWaterSplash = spawnsWaterSplash;
        }
        EditorUI::HelpMarker("動作対象やイベントパネルでの接続は不要です。プレイヤーがこのトリガーに進入した瞬間、この位置で水しぶきが発生します");
    }
    ImGui::TextDisabled(editor.triggers_[editor.selIndex_].IsInside() ? "プレイヤーは範囲内にいます" : "プレイヤーは範囲外です");
    return true;
}

bool StageEditorInspectorPanel::RenderExternalInspector(StageEditor& editor)
{
    if (editor.selKind_ != StageEditor::SelKind::External || editor.selIndex_ < 0
        || editor.selIndex_ >= static_cast<int>(editor.externalEntities_.size())) {
        return false;
    }
    StageEditor::ExternalEntityRef& ref = editor.externalEntities_[editor.selIndex_];
    ImGui::Text("%s", ref.name.c_str());
    ImGui::TextDisabled("ランタイム実体（JSONには保存されません）");
    if (ref.position) {
        ImGui::DragFloat3("位置", &ref.position->x, EditorUi::kDragStepPosition);
    }
    if (ref.getVisualPreset && ref.setVisualPreset) {
        const int preset = ref.getVisualPreset();
        const bool isCustomStatic = preset < 0 && ref.getStaticVisualModel && !ref.getStaticVisualModel().empty();
        const std::string currentModel = preset == 1
            ? "Resources/AnimatedMechPack/Textured/glTF/Mike.gltf"
            : preset == 0 ? "Resources/AlienAnimated/glTF/Alien.gltf"
            : isCustomStatic ? ref.getStaticVisualModel()
                              : "（未設定）";
        ImGui::TextWrapped("モデル: %s", currentModel.c_str());
        if (isCustomStatic) {
            const std::string currentTex = ref.getStaticVisualTexture ? ref.getStaticVisualTexture() : "";
            ImGui::TextWrapped("テクスチャ: %s", currentTex.empty() ? "（白テクスチャ）" : currentTex.c_str());
        }
        if (ImGui::Button("モデルファイルを選択...")) {
            const std::string selected = OpenFileDialog(
                "対応モデル(gltf/glb/obj)\0*.gltf;*.glb;*.obj\0すべてのファイル\0*.*\0\0", "Resources");
            if (!selected.empty()) {
                std::string normalized = selected;
                std::replace(normalized.begin(), normalized.end(), '\\', '/');
                if (normalized.find("Alien.gltf") != std::string::npos) {
                    if (ref.setStaticVisualModel)
                        ref.setStaticVisualModel("", "");
                    ref.setVisualPreset(0);
                } else if (normalized.find("Mike.gltf") != std::string::npos) {
                    if (ref.setStaticVisualModel)
                        ref.setStaticVisualModel("", "");
                    ref.setVisualPreset(1);
                } else {
                    // Model::Initializeは拡張子で分岐する（.obj以外はAssimp経由のLoadGltfFileへ）ため、
                    // gltf/glb/obj問わずここへそのまま渡してよい。テクスチャは既存の指定があれば引き継ぐ
                    if (ref.setStaticVisualModel) {
                        const std::string keepTex = ref.getStaticVisualTexture ? ref.getStaticVisualTexture() : "";
                        ref.setStaticVisualModel(ToProjectRelativePath(selected), keepTex);
                        editor.statusMessage_ = "アニメーションなしの静的モデルとして読み込みました";
                        editor.statusTimer_ = StageEditor::kStatusLongSeconds;
                    }
                }
            }
        }
        if (isCustomStatic) {
            ImGui::SameLine();
            if (ImGui::Button("テクスチャファイルを選択...")) {
                const std::string selectedTex = OpenFileDialog(
                    "画像ファイル\0*.png;*.jpg;*.jpeg\0すべてのファイル\0*.*\0\0", "Resources");
                if (!selectedTex.empty() && ref.setStaticVisualModel && ref.getStaticVisualModel) {
                    ref.setStaticVisualModel(ref.getStaticVisualModel(), ToProjectRelativePath(selectedTex));
                    editor.statusMessage_ = "テクスチャを差し替えました";
                    editor.statusTimer_ = StageEditor::kStatusLongSeconds;
                }
            }
        }
        if (ImGui::Button("モデルを自動選択へ戻す")) {
            if (ref.setStaticVisualModel)
                ref.setStaticVisualModel("", "");
            ref.setVisualPreset(-1);
        }
    }
    return true;
}

void StageEditorInspectorPanel::Render(StageEditor& editor)
{
    const auto* display = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(display->Pos.x + display->Size.x - StageEditor::kRightPanelWidth,
        display->Pos.y + StageEditor::kToolbarHeight), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(StageEditor::kRightPanelWidth,
        (std::max)(1.0f, display->Size.y - StageEditor::kToolbarHeight)), ImGuiCond_Always);
    ImGui::Begin("選択した物の設定", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
    const bool hasSelection = editor.selKind_ != StageEditor::SelKind::None;
    if (hasSelection) {
        if (ImGui::Button("選択した物へカメラを寄せる (F)", ImVec2(-1.0f, 0.0f))) {
            editor.FocusCameraOnSelection();
        }
        ImGui::TextDisabled("数値はドラッグ、ダブルクリックで直接入力");
        ImGui::Separator();
    }
    if (!StageEditor::SelectionKindOf(editor.selKind_).DrawInspector(editor)) {
        ImGui::TextWrapped("編集したい物を、画面上か左の一覧でクリックしてください。");
        ImGui::Spacing();
        ImGui::TextWrapped("1. 左下の素材をクリックして配置\n2. 画面上でつかんで移動\n3. ここで大きさや動作を調整\n4. 上部のテストで確認して保存");
        if (ImGui::Button("詳しい使い方を見る", ImVec2(-1.0f, 0.0f))) {
            editor.showEditorHelp_ = true;
        }
    }
    ImGui::End();
}
} // namespace engine::game
#endif // USE_IMGUI
