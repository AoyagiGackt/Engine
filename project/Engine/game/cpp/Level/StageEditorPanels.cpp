/**
 * @file StageEditorPanels.cpp
 * @brief ステージ編集パネルの表示責務を個別に実行する
 */
#include "StageEditorPanels.h"
#include "EnemyEntity.h"
#include "KnightEnemy.h"
#include "StageEditor.h"

namespace engine::game {
void StageEditorHierarchyPanel::Render(StageEditor& editor)
{
    editor.RenderHierarchyContent();
}

void StageEditorInspectorPanel::Render(StageEditor& editor)
{
    constexpr float kToolbarHeight = 42.0f;
    constexpr float kPanelWidth = 300.0f;
    ImGui::SetNextWindowPos(ImVec2(static_cast<float>(WinApp::kClientWidth) - kPanelWidth, kToolbarHeight), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(kPanelWidth, static_cast<float>(WinApp::kClientHeight) - kToolbarHeight), ImGuiCond_Always);
    ImGui::Begin("詳細設定", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);

    if (editor.selKind_ == StageEditor::SelKind::Object && editor.selIndex_ >= 0 && editor.selIndex_ < static_cast<int>(editor.objects_.size())) {
        StageEditor::ObjectEntry& entry = editor.objects_[editor.selIndex_];
        ObjectDesc& desc = entry.desc;
        bool structuralDirty = false;
        bool transformDirty = false;

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

        const bool visualKind = desc.kind == "prop" || desc.kind == "gimmick" || desc.kind == "terrain";
        if (visualKind) {
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
                std::string p = OpenFileDialog("OBJファイル\0*.obj\0すべてのファイル\0*.*\0\0", "Resources");
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
        // ドラッグ系ウィジェットは操作開始で変更前を控え、離した時に1回分のUndoとして確定する
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
                        || (editor.objects_[index].desc.kind != "prop" && editor.objects_[index].desc.kind != "gimmick"
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
                const char* kAxes[] = { "x", "y", "z" };
                int axisIdx = (desc.axis == 'y') ? 1 : (desc.axis == 'z') ? 2
                                                                          : 0;
                if (ImGui::Combo("並べる軸", &axisIdx, kAxes, 3)) {
                    editor.RecordUndoSnapshotNow();
                    desc.axis = kAxes[axisIdx][0];
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
            const char* motions[] = { "none", "move_y", "rotate_y", "fall", "blink" };
            int motionIndex = desc.gimmickMotion == "move_y" ? 1
                : desc.gimmickMotion == "rotate_y"            ? 2
                : desc.gimmickMotion == "fall"                ? 3
                : desc.gimmickMotion == "blink"               ? 4
                                                                  : 0;
            if (ImGui::Combo("動作プリセット", &motionIndex, motions, 5)) {
                editor.RecordUndoSnapshotNow();
                desc.gimmickMotion = motions[motionIndex];
            }
            captureItemUndo(ImGui::DragFloat("動作量", &desc.motionAmount, 0.1f));
            captureItemUndo(ImGui::DragFloat("動作速度", &desc.motionSpeed, 0.1f, 0.0f, 20.0f));
        }
        if (desc.kind == "camera_point") {
            captureItemUndo(ImGui::DragFloat("カメラ補間秒数", &desc.cameraBlendSeconds, 0.05f, 0.0f, 10.0f));
            captureItemUndo(ImGui::DragFloat("カメラ維持秒数", &desc.cameraHoldSeconds, 0.1f, 0.0f, 30.0f));
        }

        if (structuralDirty) {
            editor.RegenerateInstances(entry);
        } else if (transformDirty && visualKind) {
            // enemy系はStageEditor::UpdateObjects()側が毎フレームdesc.position⇔実体位置を同期するので、ここでは不要
            editor.RefreshTransforms(entry);
        }
    } else if (editor.selKind_ == StageEditor::SelKind::Trigger && editor.selIndex_ >= 0 && editor.selIndex_ < static_cast<int>(editor.triggers_.size())) {
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
        }
        ImGui::TextDisabled(editor.triggers_[editor.selIndex_].IsInside() ? "プレイヤーは範囲内にいます" : "プレイヤーは範囲外です");
    } else if (editor.selKind_ == StageEditor::SelKind::External && editor.selIndex_ >= 0 && editor.selIndex_ < static_cast<int>(editor.externalEntities_.size())) {
        ExternalEntityRef& ref = editor.externalEntities_[editor.selIndex_];
        ImGui::Text("%s", ref.name.c_str());
        ImGui::TextDisabled("ランタイム実体（JSONには保存されません）");
        if (ref.position) {
            ImGui::DragFloat3("位置", &ref.position->x, 0.1f);
        }
    } else {
        ImGui::TextDisabled("左のステージエディタでオブジェクト/トリガーを選択してください");
    }

    ImGui::End();

}
} // namespace engine::game
