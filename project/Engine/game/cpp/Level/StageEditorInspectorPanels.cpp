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
#include "KnightEnemy.h"
#include "SceneShared.h"
#include "StageEditor.h"
#include "WinApp.h"
#include <algorithm>
#include <commdlg.h>
#include <cstring>
#include <imgui.h>
#pragma comment(lib, "comdlg32.lib")

namespace {
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

// screen座標のui_text/hud_anchorが編集パネル（ツールバー/左カラム/右インスペクタ）の下に隠れて
// 3Dビュー上でドラッグできない場合に警告し、見える位置へ逃がすボタンを出す
void StageEditorInspectorPanel::RenderScreenAnchorOcclusionWarning(StageEditor& editor, ObjectDesc& desc)
{
    constexpr float kVisibleAreaTopMargin = 40.0f; // ツールバーのすぐ下は掴みにくいので少し余白を空ける
    const float visibleLeft = StageEditor::kLeftPanelWidth;
    const float visibleRight = static_cast<float>(WinApp::kClientWidth) - StageEditor::kRightPanelWidth;
    const float visibleTop = StageEditor::kToolbarHeight;
    const bool hiddenByPanel = desc.position.x < visibleLeft || desc.position.x > visibleRight || desc.position.y < visibleTop;
    if (!hiddenByPanel) {
        return;
    }
    ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "この位置は編集パネルの下に隠れており、3Dビュー上ではドラッグできません");
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
    // hud_anchorは固定名でシーン側から検索されるため、名前変更を許すと位置指定が無効化されてしまう
    {
        ImGui::BeginDisabled(desc.kind == "hud_anchor");
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
        ImGui::EndDisabled();
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
    const bool visualKind = desc.kind == "prop" || desc.kind == "background" || desc.kind == "gimmick" || desc.kind == "terrain";
    if (visualKind) {
        if (desc.kind == "background") {
            ImGui::TextDisabled("背景モデル（レベルJSONに保存）");
            ImGui::SameLine();
            EditorUI::HelpMarker("モデル・テクスチャ・位置・回転・スケールを通常のオブジェクトと同様に調整できます。当たり判定は初期状態で無効です。");
        }
        char modelBuf[256];
        strncpy_s(modelBuf, desc.model.c_str(), _TRUNCATE);
        ImGui::SetNextItemWidth(180.0f);
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
        ImGui::SetNextItemWidth(180.0f);
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

        const char* kTypes[] = { "static", "row" };
        const char* kTypeLabels[] = { "単体配置(static)", "並べて配置(row)" };
        int typeIdx = (desc.type == "row") ? 1 : 0;
        if (ImGui::Combo("種類", &typeIdx, kTypeLabels, 2)) {
            editor.RecordUndoSnapshotNow();
            desc.type = kTypes[typeIdx];
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
    const bool visualKind = desc.kind == "prop" || desc.kind == "background" || desc.kind == "gimmick" || desc.kind == "terrain";
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
    const bool positionChanged = captureItemUndo(ImGui::DragFloat3("位置", &desc.position.x, 0.1f));
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

    if (visualKind || desc.kind == "camera_point") {
        const Vector3 previousRotation = desc.rotation;
        const bool rotationChanged = captureItemUndo(ImGui::DragFloat3("回転", &desc.rotation.x, 0.01f));
        transformDirty |= rotationChanged;
        const Vector3 previousScale = desc.scale;
        const bool scaleChanged = captureItemUndo(ImGui::DragFloat3("スケール", &desc.scale.x, 0.05f));
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
                    || (editor.objects_[index].desc.kind != "prop" && editor.objects_[index].desc.kind != "background" && editor.objects_[index].desc.kind != "gimmick"
                        && editor.objects_[index].desc.kind != "terrain" && editor.objects_[index].desc.kind != "camera_point")) {
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

        if (desc.kind == "terrain") {
            if (ImGui::Checkbox("メッシュ同期コライダー", &desc.meshCollider)) {
                editor.RecordUndoSnapshotNow();
                desc.solid = desc.meshCollider || desc.solid;
            }
        }

        if (desc.type == "row") {
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
            transformDirty |= captureItemUndo(ImGui::DragFloat("間隔", &desc.step, 0.05f));
        }
    }
}

void StageEditorInspectorPanel::RenderObjectGameplay(StageEditor& editor, bool& structuralDirty)
{
    auto& desc = editor.objects_[editor.selIndex_].desc;
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
    if (desc.kind == "gimmick" || desc.kind == "spawn_point" || desc.kind == "camera_point") {
        char flagBuffer[96] = { };
        strncpy_s(flagBuffer, desc.activationFlag.c_str(), _TRUNCATE);
        if (captureItemUndo(ImGui::InputText("有効化フラグ", flagBuffer, sizeof(flagBuffer)))) {
            desc.activationFlag = flagBuffer;
        }
        EditorUI::HelpMarker("空なら常時有効です。イベントトリガーが同名のフラグを立てると有効になります");
    }
    if (desc.kind == "spawn_point") {
        const char* spawnTypes[] = { "basic", "knight" };
        int spawnTypeIndex = desc.spawnType == "knight" ? 1 : 0;
        if (ImGui::Combo("発生する敵", &spawnTypeIndex, spawnTypes, 2)) {
            editor.RecordUndoSnapshotNow();
            desc.spawnType = spawnTypes[spawnTypeIndex];
            structuralDirty = true;
        }
    }
    if (desc.kind == "spawn_point" || desc.kind == "enemy_basic" || desc.kind == "enemy_knight") {
        char groupBuffer[96] = { };
        strncpy_s(groupBuffer, desc.enemyGroup.c_str(), _TRUNCATE);
        if (captureItemUndo(ImGui::InputText("敵グループ", groupBuffer, sizeof(groupBuffer)))) {
            desc.enemyGroup = groupBuffer;
        }
    }
    if (desc.kind == "enemy_basic" || desc.kind == "enemy_knight" || desc.kind == "patrol_point") {
        char routeBuffer[96] = { };
        strncpy_s(routeBuffer, desc.patrolRoute.c_str(), _TRUNCATE);
        if (captureItemUndo(ImGui::InputText("巡回ルート名", routeBuffer, sizeof(routeBuffer)))) {
            desc.patrolRoute = routeBuffer;
        }
        if (desc.kind == "patrol_point") {
            if (ImGui::InputInt("巡回順", &desc.routeOrder)) {
                editor.RecordUndoSnapshotNow();
            }
        } else {
            captureItemUndo(ImGui::DragFloat("巡回速度", &desc.patrolSpeed, 0.05f, 0.0f, 20.0f));
        }
    }
    if (desc.kind == "enemy_basic") {
        // 空="武器を持たない一般敵"。指定すると倒してJキーで奪取できるようになる（GamePlayScene参照）
        constexpr const char* kWeaponTypes[] = { "なし", "Sword", "Spear", "Hammer", "Dagger", "Ball", "Greatsword", "Scythe", "Axe" };
        constexpr int kWeaponTypeCount = static_cast<int>(sizeof(kWeaponTypes) / sizeof(kWeaponTypes[0]));
        int weaponTypeIndex = 0;
        for (int i = 1; i < kWeaponTypeCount; ++i) {
            if (desc.weaponType == kWeaponTypes[i]) {
                weaponTypeIndex = i;
                break;
            }
        }
        if (ImGui::Combo("奪取可能な武器", &weaponTypeIndex, kWeaponTypes, kWeaponTypeCount)) {
            editor.RecordUndoSnapshotNow();
            desc.weaponType = weaponTypeIndex == 0 ? "" : kWeaponTypes[weaponTypeIndex];
            structuralDirty = true; // 武器種別はEnemyEntity生成時にしか反映できないため実体を作り直す
        }
        // isStageBossはEnemyEntity生成には関わらないメタデータなのでstructuralDirtyは不要
        captureItemUndo(ImGui::Checkbox("ステージボス（倒して奪取するとクリア）", &desc.isStageBoss));
    }
    if (desc.kind == "event_condition") {
        const char* conditionTypes[] = { "manual", "timer", "enemy_group_defeated" };
        int conditionIndex = desc.conditionType == "timer" ? 1
            : desc.conditionType == "enemy_group_defeated" ? 2
                                                           : 0;
        if (ImGui::Combo("条件", &conditionIndex, conditionTypes, 3)) {
            editor.RecordUndoSnapshotNow();
            desc.conditionType = conditionTypes[conditionIndex];
        }
        if (desc.conditionType == "timer") {
            captureItemUndo(ImGui::DragFloat("成立までの秒数", &desc.conditionSeconds, 0.1f, 0.0f, 300.0f));
        } else if (desc.conditionType == "enemy_group_defeated") {
            char groupBuffer[96] = { };
            strncpy_s(groupBuffer, desc.enemyGroup.c_str(), _TRUNCATE);
            if (captureItemUndo(ImGui::InputText("監視する敵グループ", groupBuffer, sizeof(groupBuffer)))) {
                desc.enemyGroup = groupBuffer;
            }
        }
    }
    if (desc.kind == "gimmick") {
        const char* motions[] = { "none", "move_y", "rotate_y", "rotate_z", "fall", "blink" };
        int motionIndex = desc.gimmickMotion == "move_y" ? 1
            : desc.gimmickMotion == "rotate_y"           ? 2
            : desc.gimmickMotion == "rotate_z"           ? 3
            : desc.gimmickMotion == "fall"               ? 4
            : desc.gimmickMotion == "blink"              ? 5
                                                         : 0;
        if (ImGui::Combo("動作プリセット", &motionIndex, motions, 5)) {
            editor.RecordUndoSnapshotNow();
            desc.gimmickMotion = motions[motionIndex];
        }
        captureItemUndo(ImGui::DragFloat("動作量", &desc.motionAmount, 0.1f));
        captureItemUndo(ImGui::DragFloat("動作速度", &desc.motionSpeed, 0.1f, 0.0f, 20.0f));
    }
    if (desc.kind == "camera_point") {
        if (editor.camera_ && ImGui::Button("現在のビューをカメラポイントへ保存")) {
            editor.RecordUndoSnapshotNow();
            desc.position = editor.camera_->GetTranslate();
            desc.rotation = editor.camera_->GetRotate();
            editor.RefreshTransforms(editor.objects_[editor.selIndex_]);
        }
        if (editor.camera_ && ImGui::Button("カメラポイントをプレビュー")) {
            editor.camera_->SetTranslate(editor.WorldPositionOf(desc));
            editor.camera_->SetRotate(desc.rotation);
        }
        captureItemUndo(ImGui::DragFloat("カメラ補間秒数", &desc.cameraBlendSeconds, 0.05f, 0.0f, 10.0f));
        captureItemUndo(ImGui::DragFloat("カメラ維持秒数", &desc.cameraHoldSeconds, 0.1f, 0.0f, 30.0f));
    }
}

void StageEditorInspectorPanel::RenderObjectText(StageEditor& editor)
{
    auto& desc = editor.objects_[editor.selIndex_].desc;
    if (desc.kind != "ui_text") {
        return;
    }
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

    char textBuf[512];
    strncpy_s(textBuf, desc.text.c_str(), _TRUNCATE);
    if (captureItemUndo(ImGui::InputTextMultiline("文字列", textBuf, sizeof(textBuf), ImVec2(-1.0f, 60.0f)))) {
        desc.text = textBuf;
    }

    captureItemUndo(ImGui::ColorEdit4("色", &desc.textColor.x));
    if (ImGui::Checkbox("太字", &desc.textBold)) {
        editor.RecordUndoSnapshotNow();
    }
    captureItemUndo(ImGui::DragFloat("大きさ", &desc.textScale, 0.05f, 0.1f, 10.0f));

    const char* spaces[] = { "screen", "world" };
    const char* spaceLabels[] = { "画面座標(px)", "ワールド座標" };
    int spaceIndex = (desc.textSpace == "world") ? 1 : 0;
    if (ImGui::Combo("座標基準", &spaceIndex, spaceLabels, 2)) {
        const std::string newSpace = spaces[spaceIndex];
        if (newSpace != desc.textSpace) {
            editor.RecordUndoSnapshotNow();
            // 座標基準の切り替え時にposition(px⇔ワールド単位)を素通りさせると、
            // 数値のスケールが噛み合わずカメラ範囲外へ飛んで見えなくなるため、その場でスクリーン上の見た目を保つよう変換する
            if (newSpace == "world") {
                Vector3 worldPos;
                desc.position = editor.MouseToGround(desc.position.x, desc.position.y, worldPos) ? worldPos : Vector3 { };
            } else {
                float camX = 0.0f, camY = 0.0f;
                if (editor.camera_) {
                    const Vector3& camPos = editor.camera_->GetTranslate();
                    camX = camPos.x;
                    camY = camPos.y;
                }
                const Vector3 world = editor.WorldPositionOf(desc);
                float screenX, screenY;
                SceneShared::WorldToScreen(world.x, world.y, camX, camY, screenX, screenY);
                desc.position = { screenX, screenY, 0.0f };
            }
            desc.textSpace = newSpace;
        }
    }
    EditorUI::HelpMarker("画面座標: 位置のx/yをスクリーンピクセル座標として使います（カメラに影響されず常に同じ位置に表示）\nワールド座標: 位置をワールド座標として扱い、カメラに応じて画面へ投影します");

    if (desc.textSpace == "screen") {
        RenderScreenAnchorOcclusionWarning(editor, desc);
    }
}

void StageEditorInspectorPanel::RenderHudAnchorInspector(StageEditor& editor)
{
    auto& desc = editor.objects_[editor.selIndex_].desc;
    if (desc.kind != "hud_anchor") {
        return;
    }
    ImGui::TextDisabled("「%s」パネルの表示位置マーカーです（文言はコード側で管理、位置だけ上の「位置」欄で編集できます）", desc.text.c_str());
    RenderScreenAnchorOcclusionWarning(editor, desc);
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
    RenderObjectVisual(editor, structuralDirty);
    RenderObjectTransform(editor, structuralDirty, transformDirty);
    RenderObjectGameplay(editor, structuralDirty);
    RenderObjectText(editor);
    RenderHudAnchorInspector(editor);

    auto& entry = editor.objects_[editor.selIndex_];
    const bool visualKind = entry.desc.kind == "prop" || entry.desc.kind == "background" || entry.desc.kind == "gimmick" || entry.desc.kind == "terrain";
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

    captureItemUndo(ImGui::DragFloat3("位置", &desc.position.x, 0.1f));
    captureItemUndo(ImGui::DragFloat("半径", &desc.radius, 0.05f, 0.1f, 50.0f));
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
        ImGui::DragFloat3("位置", &ref.position->x, 0.1f);
    }
    if (ref.object) {
        Transform& transform = ref.object->GetTransform();
        ImGui::DragFloat3("回転", &transform.rotate.x, 0.01f);
        ImGui::DragFloat3("スケール", &transform.scale.x, 0.05f);
        ImGui::TextDisabled("シーン背景（実行中のみ編集）");
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
                        editor.statusTimer_ = 4.0f;
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
                    editor.statusTimer_ = 4.0f;
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
    ImGui::SetNextWindowPos(ImVec2(static_cast<float>(WinApp::kClientWidth) - StageEditor::kRightPanelWidth, StageEditor::kToolbarHeight), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(StageEditor::kRightPanelWidth, static_cast<float>(WinApp::kClientHeight) - StageEditor::kToolbarHeight), ImGuiCond_Always);
    ImGui::Begin("詳細設定", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
    if (!RenderObjectInspector(editor)
        && !RenderTriggerInspector(editor)
        && !RenderExternalInspector(editor)) {
        ImGui::TextDisabled("左のステージエディタでオブジェクト/トリガーを選択してください");
    }
    ImGui::End();
}
} // namespace engine::game
#endif // USE_IMGUI
