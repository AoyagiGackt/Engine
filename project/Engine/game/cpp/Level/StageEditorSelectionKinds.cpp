/**
 * @file StageEditorSelectionKinds.cpp
 * @brief ステージエディタの選択対象（配置物・トリガー・外部実体）ごとの編集操作
 */
#ifdef USE_IMGUI
#include "StageEditor.h"
#include "DirectXCommon.h"
#include "EnemyEntity.h"
#include "KnightEnemy.h"
#include "ModelCommon.h"
#include "ObjectKind.h"
#include "StageEditorPanels.h"
#include <algorithm>
#include <iterator>
#include <imgui.h>

namespace engine::game {

/** @brief 配置物: 複数選択・親子関係・画面座標テキストを考慮して操作する */
class StageEditor::ObjectSelectionKind final : public StageEditor::ISelectionKind {
public:
    bool WorldPosition(const StageEditor& editor, int index, Vector3& out) const override
    {
        if (!IsValid(editor, index)) {
            return false;
        }
        out = editor.WorldPositionOf(editor.objects_[index].desc);
        return true;
    }

    bool IsScreenAnchor(const StageEditor& editor, int index) const override
    {
        if (!IsValid(editor, index)) {
            return false;
        }
        const ObjectDesc& desc = editor.objects_[index].desc;
        return ObjectKind::Of(desc.kind).IsScreenSpace(desc);
    }

    float LocalZ(const StageEditor& editor, int index) const override
    {
        return IsValid(editor, index) ? editor.objects_[index].desc.position.z : 0.0f;
    }

    void SetLocalZ(StageEditor& editor, int index, float z, bool moved) const override
    {
        if (!IsValid(editor, index)) {
            return;
        }
        editor.objects_[index].desc.position.z = z;
        if (moved) {
            editor.MarkUndoDirty();
        }
    }

    void DragTo(StageEditor& editor, int index, float worldX, float worldY, int axis, bool moved) const override
    {
        if (!IsValid(editor, index)) {
            return;
        }
        // スナップはワールド座標側で丸め済みなので、親がいる場合はローカル座標へ逆算する
        ObjectDesc& desc = editor.objects_[index].desc;
        const Vector3 parentW = editor.ParentWorldPositionOf(desc);
        if (axis == 0 || axis == 1) {
            desc.position.x = worldX - parentW.x;
        }
        if (axis == 0 || axis == 2) {
            desc.position.y = worldY - parentW.y;
        }
        if (moved) {
            editor.MarkUndoDirty();
        }
    }

    void Nudge(StageEditor& editor, int, const Vector3& delta) const override
    {
        // 複数選択中はまとめて動かす
        if (editor.selectedObjectIndices_.empty()) {
            return;
        }
        editor.RecordUndoSnapshotNow();
        for (int index : editor.selectedObjectIndices_) {
            if (!IsValid(editor, index)) {
                continue;
            }
            ObjectEntry& entry = editor.objects_[index];
            entry.desc.position = entry.desc.position + delta;
            entry.authoredPosition = entry.desc.position;
            if (ObjectKind::Of(entry.desc.kind).IsVisual()) {
                editor.RefreshTransforms(entry);
            }
        }
    }

    bool Delete(StageEditor& editor, int index) const override
    {
        if (!IsValid(editor, index)) {
            return false;
        }
        editor.RecordUndoSnapshotNow();
        if (editor.modelCommon_ && editor.modelCommon_->GetDxCommon()) {
            editor.modelCommon_->GetDxCommon()->WaitForGpu();
        }
        std::vector<int> targets = editor.selectedObjectIndices_.empty()
            ? std::vector<int> { index }
            : editor.selectedObjectIndices_;
        std::sort(targets.begin(), targets.end());
        targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
        for (auto iterator = targets.rbegin(); iterator != targets.rend(); ++iterator) {
            const int target = *iterator;
            if (!IsValid(editor, target)) {
                continue;
            }
            // 子のワールド位置を維持してから削除対象への親参照を解除する
            const std::string deletedName = editor.objects_[target].desc.name;
            for (auto& other : editor.objects_) {
                if (other.desc.parent == deletedName) {
                    other.desc.position = editor.WorldPositionOf(other.desc);
                    other.desc.parent.clear();
                }
            }
            editor.DestroyObjectRuntime(editor.objects_[target], true);
            editor.objects_.erase(editor.objects_.begin() + target);
        }
        return true;
    }

    void Duplicate(StageEditor& editor, int index) const override
    {
        if (!IsValid(editor, index)) {
            return;
        }
        editor.RecordUndoSnapshotNow();
        const std::vector<int> sources = editor.selectedObjectIndices_.empty()
            ? std::vector<int> { index }
            : editor.selectedObjectIndices_;
        std::vector<ObjectDesc> copies;
        for (int source : sources) {
            if (IsValid(editor, source)) {
                copies.push_back(editor.objects_[source].desc);
            }
        }
        editor.selectedObjectIndices_.clear();
        for (ObjectDesc& desc : copies) {
            ObjectEntry entry;
            entry.desc = std::move(desc);
            entry.desc.name = "obj_" + std::to_string(editor.nextSerial_++);
            entry.desc.parent.clear();
            entry.desc.position.x += editor.snapEnabled_ ? editor.snapStep_ : 1.0f;
            editor.objects_.push_back(std::move(entry));
            editor.RegenerateInstances(editor.objects_.back());
            editor.selectedObjectIndices_.push_back(static_cast<int>(editor.objects_.size()) - 1);
        }
        editor.selKind_ = SelKind::Object;
        editor.selIndex_ = static_cast<int>(editor.objects_.size()) - 1;
        editor.statusMessage_ = "複製しました";
        editor.statusTimer_ = kStatusBriefSeconds;
    }

    void SelectFromClick(StageEditor& editor, int index, bool additive) const override
    {
        editor.selKind_ = SelKind::Object;
        editor.selIndex_ = index;
        if (!additive) {
            editor.selectedObjectIndices_.clear();
        }
        if (std::find(editor.selectedObjectIndices_.begin(), editor.selectedObjectIndices_.end(), index)
            == editor.selectedObjectIndices_.end()) {
            editor.selectedObjectIndices_.push_back(index);
        }
    }

    void SelectFromContext(StageEditor& editor, int index) const override
    {
        editor.selKind_ = SelKind::Object;
        editor.selIndex_ = index;
        // 複数選択の中を右クリックした時は選択を保つ
        if (std::find(editor.selectedObjectIndices_.begin(), editor.selectedObjectIndices_.end(), index)
            == editor.selectedObjectIndices_.end()) {
            editor.selectedObjectIndices_ = { index };
        }
    }

    bool DrawContextMenu(StageEditor& editor, int index) const override
    {
        if (!IsValid(editor, index)) {
            return false;
        }
        // 複製・削除はobjects_を組み替えるので、実行したらdescを触らずに抜ける
        const ObjectDesc& desc = editor.objects_[index].desc;
        ImGui::TextDisabled("%s", desc.name.c_str());
        ImGui::Separator();
        if (ImGui::MenuItem("ここへカメラ (F)")) {
            editor.FocusCameraOn(editor.WorldPositionOf(desc));
        }
        if (ImGui::MenuItem("複製 (Ctrl+D)")) {
            editor.DuplicateSelected();
            return true;
        }
        if (ImGui::MenuItem("削除 (Delete)")) {
            editor.DeleteSelected();
            return true;
        }
        ImGui::Separator();
        if (!desc.parent.empty() && ImGui::MenuItem("親を外す")) {
            editor.SetParentPreservingWorld(index, -1);
        }
        const bool hasOtherSelection = editor.selectedObjectIndices_.size() > 1;
        if (hasOtherSelection && ImGui::MenuItem("選択中の物をこの子にする")) {
            const std::vector<int> children = editor.selectedObjectIndices_;
            for (int child : children) {
                if (child != index) {
                    editor.SetParentPreservingWorld(child, index);
                }
            }
        }
        if (ImGui::MenuItem("クリックした物を親にする...")) {
            editor.parentLinkChildIndex_ = index;
            editor.statusMessage_ = "親にしたい配置物をクリックしてください";
            editor.statusTimer_ = kStatusLongSeconds;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("この位置からテスト")) {
            editor.StartPlayTestOrWarn(editor.WorldPositionOf(editor.objects_[index].desc));
        }
        return true;
    }

    bool DrawInspector(StageEditor& editor) const override
    {
        return StageEditorInspectorPanel::RenderObjectInspector(editor);
    }

    int EventSourceIndex(const StageEditor& editor, int index) const override
    {
        // イベント条件の配置物はトリガーの後ろに番号を振る
        if (!IsValid(editor, index) || editor.objects_[index].desc.kind != ObjectKindName::kEventCondition) {
            return -1;
        }
        return static_cast<int>(editor.triggers_.size()) + index;
    }

private:
    static bool IsValid(const StageEditor& editor, int index)
    {
        return index >= 0 && index < static_cast<int>(editor.objects_.size());
    }
};

/** @brief トリガー: 位置と半径だけを持つ */
class StageEditor::TriggerSelectionKind final : public StageEditor::ISelectionKind {
public:
    bool WorldPosition(const StageEditor& editor, int index, Vector3& out) const override
    {
        if (!IsValid(editor, index)) {
            return false;
        }
        out = editor.triggers_[index].GetDesc().position;
        return true;
    }

    float LocalZ(const StageEditor& editor, int index) const override
    {
        return IsValid(editor, index) ? editor.triggers_[index].GetDesc().position.z : 0.0f;
    }

    void SetLocalZ(StageEditor& editor, int index, float z, bool moved) const override
    {
        if (!IsValid(editor, index)) {
            return;
        }
        editor.triggers_[index].GetDesc().position.z = z;
        if (moved) {
            editor.MarkUndoDirty();
        }
    }

    void DragTo(StageEditor& editor, int index, float worldX, float worldY, int axis, bool moved) const override
    {
        if (!IsValid(editor, index)) {
            return;
        }
        TriggerDesc& desc = editor.triggers_[index].GetDesc();
        if (axis == 0 || axis == 1) {
            desc.position.x = worldX;
        }
        if (axis == 0 || axis == 2) {
            desc.position.y = worldY;
        }
        if (moved) {
            editor.MarkUndoDirty();
        }
    }

    void Nudge(StageEditor& editor, int index, const Vector3& delta) const override
    {
        if (!IsValid(editor, index)) {
            return;
        }
        editor.RecordUndoSnapshotNow();
        Vector3& position = editor.triggers_[index].GetDesc().position;
        position = position + delta;
    }

    bool Delete(StageEditor& editor, int index) const override
    {
        if (!IsValid(editor, index)) {
            return false;
        }
        editor.RecordUndoSnapshotNow();
        editor.triggers_.erase(editor.triggers_.begin() + index);
        return true;
    }

    void Duplicate(StageEditor& editor, int index) const override
    {
        if (!IsValid(editor, index)) {
            return;
        }
        editor.RecordUndoSnapshotNow();
        TriggerDesc desc = editor.triggers_[index].GetDesc();
        desc.name = "trigger_" + std::to_string(editor.nextSerial_++);
        desc.position.x += editor.snapEnabled_ ? editor.snapStep_ : 1.0f;
        TriggerVolume trigger;
        trigger.Init(desc);
        editor.triggers_.push_back(std::move(trigger));
        editor.selKind_ = SelKind::Trigger;
        editor.selIndex_ = static_cast<int>(editor.triggers_.size()) - 1;
        editor.statusMessage_ = "複製しました";
        editor.statusTimer_ = kStatusBriefSeconds;
    }

    void SelectFromClick(StageEditor& editor, int index, bool) const override
    {
        SelectFromContext(editor, index);
    }

    void SelectFromContext(StageEditor& editor, int index) const override
    {
        editor.selKind_ = SelKind::Trigger;
        editor.selIndex_ = index;
        editor.selectedObjectIndices_.clear();
    }

    bool DrawContextMenu(StageEditor& editor, int index) const override
    {
        if (!IsValid(editor, index)) {
            return false;
        }
        const TriggerDesc& desc = editor.triggers_[index].GetDesc();
        ImGui::TextDisabled("トリガー %s", desc.name.c_str());
        ImGui::Separator();
        if (ImGui::MenuItem("ここへカメラ (F)")) {
            editor.FocusCameraOn(desc.position);
        }
        if (ImGui::MenuItem("削除 (Delete)")) {
            editor.DeleteSelected();
            return true;
        }
        if (ImGui::MenuItem("この位置からテスト")) {
            editor.StartPlayTestOrWarn(desc.position);
        }
        return true;
    }

    bool DrawInspector(StageEditor& editor) const override
    {
        return StageEditorInspectorPanel::RenderTriggerInspector(editor);
    }

    int EventSourceIndex(const StageEditor& editor, int index) const override
    {
        return IsValid(editor, index) ? index : -1;
    }

private:
    static bool IsValid(const StageEditor& editor, int index)
    {
        return index >= 0 && index < static_cast<int>(editor.triggers_.size());
    }
};

/** @brief 外部実体（Player等）: 位置だけを借りて動かす。JSONには保存されないのでUndoも記録しない */
class StageEditor::ExternalSelectionKind final : public StageEditor::ISelectionKind {
public:
    bool WorldPosition(const StageEditor& editor, int index, Vector3& out) const override
    {
        const Vector3* position = PositionOf(editor, index);
        if (!position) {
            return false;
        }
        out = *position;
        return true;
    }

    float LocalZ(const StageEditor& editor, int index) const override
    {
        const Vector3* position = PositionOf(editor, index);
        return position ? position->z : 0.0f;
    }

    void SetLocalZ(StageEditor& editor, int index, float z, bool) const override
    {
        if (Vector3* position = PositionOf(editor, index)) {
            position->z = z;
        }
    }

    void DragTo(StageEditor& editor, int index, float worldX, float worldY, int, bool) const override
    {
        if (Vector3* position = PositionOf(editor, index)) {
            position->x = worldX;
            position->y = worldY;
        }
    }

    void Nudge(StageEditor& editor, int index, const Vector3& delta) const override
    {
        if (Vector3* position = PositionOf(editor, index)) {
            *position = *position + delta;
        }
    }

    void SelectFromClick(StageEditor& editor, int index, bool) const override
    {
        editor.selKind_ = SelKind::External;
        editor.selIndex_ = index;
        editor.selectedObjectIndices_.clear();
    }

    bool DrawInspector(StageEditor& editor) const override
    {
        return StageEditorInspectorPanel::RenderExternalInspector(editor);
    }

private:
    static Vector3* PositionOf(const StageEditor& editor, int index)
    {
        if (index < 0 || index >= static_cast<int>(editor.externalEntities_.size())) {
            return nullptr;
        }
        return editor.externalEntities_[index].position;
    }
};

const StageEditor::ISelectionKind& StageEditor::SelectionKindOf(SelKind kind)
{
    static const ISelectionKind none {};
    static const ObjectSelectionKind object {};
    static const TriggerSelectionKind trigger {};
    static const ExternalSelectionKind external {};
    // SelKind の並び順と一致させる
    static const ISelectionKind* const kKinds[] = { &none, &object, &trigger, &external };
    const size_t index = static_cast<size_t>(kind);
    return index < std::size(kKinds) ? *kKinds[index] : none;
}

void StageEditor::StartPlayTestOrWarn(const Vector3& worldPosition)
{
    if (!StartPlayTestAt(worldPosition)) {
        statusMessage_ = "このシーンにはプレイヤーが登録されていません";
        statusTimer_ = kStatusNormalSeconds;
    }
}

} // namespace engine::game
#endif // USE_IMGUI
