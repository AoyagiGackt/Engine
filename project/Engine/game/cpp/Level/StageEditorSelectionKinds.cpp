/**
 * @file StageEditorSelectionKinds.cpp
 * @brief ステージエディタの選択対象（配置物・トリガー・外部実体）ごとの編集操作
 */
#ifdef USE_IMGUI
#include "StageEditor.h"
#include "StageEditorUiStyle.h"
#include "DirectXCommon.h"
#include "EnemyEntity.h"
#include "KnightEnemy.h"
#include "ModelCommon.h"
#include "ObjectKind.h"
#include "StageEditorPanels.h"
#include "StageEditorSelectionService.h"
#include <algorithm>
#include <iterator>
#include <imgui.h>

namespace engine::game {
namespace {
/** @brief 軸制限を保ちながらXYの移動結果を反映する */
void ApplyDragPosition(Vector3& position, float x, float y, int axis) {
    if (axis == stageEditorUiStyle::kGizmoAxisFree || axis == stageEditorUiStyle::kGizmoAxisX) {
        position.x = x;
    }
    if (axis == stageEditorUiStyle::kGizmoAxisFree || axis == stageEditorUiStyle::kGizmoAxisY) {
        position.y = y;
    }
}
} // namespace


/** @brief 配置物: 複数選択・親子関係・画面座標テキストを考慮して操作する */
class StageEditor::ObjectSelectionKind final : public StageEditor::ISelectionKind {
public:
    /** @brief 配置物のワールド座標を out へ入れる（求められない時 false） */
    bool WorldPosition(const StageEditor& editor, int index, Vector3& out) const override {
        if (!IsValid(editor, index)) {
            return false;
        }
        out = editor.WorldPositionOf(editor.objects_[index].desc);
        return true;
    }

    /** @brief 画面に固定された配置物かを返す */
    bool IsScreenAnchor(const StageEditor& editor, int index) const override {
        if (!IsValid(editor, index)) {
            return false;
        }
        const ObjectDesc& desc = editor.objects_[index].desc;
        return ObjectKind::Of(desc.kind).IsScreenSpace(desc);
    }

    /** @brief 配置物の親から見た奥行き（Z）を返す */
    float LocalZ(const StageEditor& editor, int index) const override {
        return IsValid(editor, index) ? editor.objects_[index].desc.position.z : 0.0f;
    }

    /** @brief 配置物の奥行き（Z）を変更する */
    void SetLocalZ(StageEditor& editor, int index, float z, bool moved) const override {
        if (!IsValid(editor, index)) {
            return;
        }
        editor.objects_[index].desc.position.z = z;
        MarkUndoIfMoved(editor, moved);
    }

    /** @brief 配置物を有効な軸に沿ってワールド座標の位置へ動かす */
    void DragTo(StageEditor& editor, int index, float worldX, float worldY, int axis, bool moved) const override {
        if (!IsValid(editor, index)) {
            return;
        }
        // スナップはワールド座標側で丸め済みなので、ワールド位置を動かしてから親から見た位置へ逆算する
        ObjectDesc& desc = editor.objects_[index].desc;
        Vector3 world = editor.WorldPositionOf(desc);
        ApplyDragPosition(world, worldX, worldY, axis);
        desc.position = editor.LocalPositionOf(desc, world);
        MarkUndoIfMoved(editor, moved);
    }

    /** @brief 選択中の配置物をまとめて少しずつ動かす */
    void Nudge(StageEditor& editor, int, const Vector3& delta) const override {
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

    /** @brief 配置物を削除する（Undo できるように記録してから消す） */
    bool Delete(StageEditor& editor, int index) const override {
        if (!BeginEdit(editor, index)) {
            return false;
        }
        editor.WaitForGpuIdle();
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

    /** @brief 配置物を複製し、複製した方を選択する */
    void Duplicate(StageEditor& editor, int index) const override {
        if (!BeginEdit(editor, index)) {
            return;
        }
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
        for (const ObjectDesc& desc : copies) {
            StageEditorSelectionService::AppendCopy(editor, desc);
        }
        editor.selKind_ = SelKind::kObject;
        editor.selIndex_ = static_cast<int>(editor.objects_.size()) - 1;
        editor.statusMessage_ = "複製しました";
        editor.statusTimer_ = kStatusBriefSeconds;
    }

    /** @brief ビューポートでのクリックに合わせて選択を更新する（additive なら追加選択） */
    void SelectFromClick(StageEditor& editor, int index, bool additive) const override {
        editor.selKind_ = SelKind::kObject;
        editor.selIndex_ = index;
        if (!additive) {
            editor.selectedObjectIndices_.clear();
        }
        if (std::find(editor.selectedObjectIndices_.begin(), editor.selectedObjectIndices_.end(), index)
            == editor.selectedObjectIndices_.end()) {
            editor.selectedObjectIndices_.push_back(index);
        }
    }

    /** @brief 右クリックメニューを開く前に対象を選択する */
    void SelectFromContext(StageEditor& editor, int index) const override {
        editor.selKind_ = SelKind::kObject;
        editor.selIndex_ = index;
        // 複数選択の中を右クリックした時は選択を保つ
        if (std::find(editor.selectedObjectIndices_.begin(), editor.selectedObjectIndices_.end(), index)
            == editor.selectedObjectIndices_.end()) {
            editor.selectedObjectIndices_ = { index };
        }
    }

    /** @brief 配置物の右クリックメニューを出す（操作した時 true） */
    bool DrawContextMenu(StageEditor& editor, int index) const override {
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

    /** @brief 選択中の配置物のインスペクターを出す */
    bool DrawInspector(StageEditor& editor) const override {
        return StageEditorInspectorPanel::RenderObjectInspector(editor);
    }

    /** @brief イベント接続の元として使う番号を返す */
    int EventSourceIndex(const StageEditor& editor, int index) const override {
        // イベント条件の配置物はトリガーの後ろに番号を振る
        if (!IsValid(editor, index) || editor.objects_[index].desc.kind != objectKindName::kEventCondition) {
            return -1;
        }
        return static_cast<int>(editor.triggers_.size()) + index;
    }

protected:
    /** @brief 配置物の数を返す */
    int Count(const StageEditor& editor) const override {
        return static_cast<int>(editor.objects_.size());
    }
};

/** @brief トリガー: 位置と半径だけを持つ */
class StageEditor::TriggerSelectionKind final : public StageEditor::ISelectionKind {
public:
    /** @brief トリガーのワールド座標を out へ入れる（求められない時 false） */
    bool WorldPosition(const StageEditor& editor, int index, Vector3& out) const override {
        if (!IsValid(editor, index)) {
            return false;
        }
        out = editor.triggers_[index].GetDesc().position;
        return true;
    }

    /** @brief トリガーの奥行き（Z）を返す */
    float LocalZ(const StageEditor& editor, int index) const override {
        return IsValid(editor, index) ? editor.triggers_[index].GetDesc().position.z : 0.0f;
    }

    /** @brief トリガーの奥行き（Z）を変更する */
    void SetLocalZ(StageEditor& editor, int index, float z, bool moved) const override {
        if (!IsValid(editor, index)) {
            return;
        }
        Vector3 position = editor.triggers_[index].GetDesc().position;
        position.z = z;
        editor.triggers_[index].SetPosition(position);
        MarkUndoIfMoved(editor, moved);
    }

    /** @brief トリガーを有効な軸に沿ってワールド座標の位置へ動かす */
    void DragTo(StageEditor& editor, int index, float worldX, float worldY, int axis, bool moved) const override {
        if (!IsValid(editor, index)) {
            return;
        }
        TriggerDesc desc = editor.triggers_[index].GetDesc();
        ApplyDragPosition(desc.position, worldX, worldY, axis);
        editor.triggers_[index].SetPosition(desc.position);
        MarkUndoIfMoved(editor, moved);
    }

    /** @brief トリガーを少しずつ動かす */
    void Nudge(StageEditor& editor, int index, const Vector3& delta) const override {
        if (!BeginEdit(editor, index)) {
            return;
        }
        const Vector3 position = editor.triggers_[index].GetDesc().position;
        editor.triggers_[index].SetPosition(position + delta);
    }

    /** @brief トリガーを削除する（Undo できるように記録してから消す） */
    bool Delete(StageEditor& editor, int index) const override {
        if (!BeginEdit(editor, index)) {
            return false;
        }
        editor.triggers_.erase(editor.triggers_.begin() + index);
        return true;
    }

    /** @brief トリガーを複製し、複製した方を選択する */
    void Duplicate(StageEditor& editor, int index) const override {
        if (!BeginEdit(editor, index)) {
            return;
        }
        TriggerDesc desc = editor.triggers_[index].GetDesc();
        desc.name = "trigger_" + std::to_string(editor.nextSerial_++);
        desc.position.x += editor.snapEnabled_ ? editor.snapStep_ : 1.0f;
        TriggerVolume trigger;
        trigger.Init(desc);
        editor.triggers_.push_back(std::move(trigger));
        editor.selKind_ = SelKind::kTrigger;
        editor.selIndex_ = static_cast<int>(editor.triggers_.size()) - 1;
        editor.statusMessage_ = "複製しました";
        editor.statusTimer_ = kStatusBriefSeconds;
    }

    /** @brief ビューポートでのクリックでトリガーを選択する */
    void SelectFromClick(StageEditor& editor, int index, bool) const override {
        SelectFromContext(editor, index);
    }

    /** @brief 右クリックメニューを開く前にトリガーを選択する */
    void SelectFromContext(StageEditor& editor, int index) const override {
        editor.selKind_ = SelKind::kTrigger;
        editor.selIndex_ = index;
        editor.selectedObjectIndices_.clear();
    }

    /** @brief トリガーの右クリックメニューを出す（操作した時 true） */
    bool DrawContextMenu(StageEditor& editor, int index) const override {
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

    /** @brief 選択中のトリガーのインスペクターを出す */
    bool DrawInspector(StageEditor& editor) const override {
        return StageEditorInspectorPanel::RenderTriggerInspector(editor);
    }

    /** @brief イベント接続の元として使うトリガーの番号を返す */
    int EventSourceIndex(const StageEditor& editor, int index) const override {
        return IsValid(editor, index) ? index : -1;
    }

protected:
    /** @brief トリガーの数を返す */
    int Count(const StageEditor& editor) const override {
        return static_cast<int>(editor.triggers_.size());
    }
};

/** @brief 外部実体（Player等）: 位置だけを借りて動かす。JSONには保存されないのでUndoも記録しない */
class StageEditor::ExternalSelectionKind final : public StageEditor::ISelectionKind {
public:
    /** @brief 外部の実体（プレイヤーなど）のワールド座標を out へ入れる（求められない時 false） */
    bool WorldPosition(const StageEditor& editor, int index, Vector3& out) const override {
        IPositionTarget* target = PositionOf(editor, index);
        if (!target) {
            return false;
        }
        out = target->GetPosition();
        return true;
    }

    /** @brief 外部の実体の奥行き（Z）を返す */
    float LocalZ(const StageEditor& editor, int index) const override {
        IPositionTarget* target = PositionOf(editor, index);
        return target ? target->GetPosition().z : 0.0f;
    }

    /** @brief 外部の実体の奥行き（Z）を変更する */
    void SetLocalZ(StageEditor& editor, int index, float z, bool) const override {
        if (IPositionTarget* target = PositionOf(editor, index)) {
            Vector3 position = target->GetPosition();
            position.z = z;
            target->SetPosition(position);
        }
    }

    /** @brief 外部の実体をワールド座標の位置へ動かす */
    void DragTo(StageEditor& editor, int index, float worldX, float worldY, int, bool) const override {
        if (IPositionTarget* target = PositionOf(editor, index)) {
            Vector3 position = target->GetPosition();
            position.x = worldX;
            position.y = worldY;
            target->SetPosition(position);
        }
    }

    /** @brief 外部の実体を少しずつ動かす */
    void Nudge(StageEditor& editor, int index, const Vector3& delta) const override {
        if (IPositionTarget* target = PositionOf(editor, index)) {
            target->MoveBy(delta);
        }
    }

    /** @brief ビューポートでのクリックで外部の実体を選択する */
    void SelectFromClick(StageEditor& editor, int index, bool) const override {
        editor.selKind_ = SelKind::kExternal;
        editor.selIndex_ = index;
        editor.selectedObjectIndices_.clear();
    }

    /** @brief 選択中の外部の実体のインスペクターを出す */
    bool DrawInspector(StageEditor& editor) const override {
        return StageEditorInspectorPanel::RenderExternalInspector(editor);
    }

private:
    /** @brief 外部の実体の位置を操作する窓口を返す（無ければ nullptr） */
    IPositionTarget* PositionOf(const StageEditor& editor, int index) const {
        return IsValid(editor, index) ? editor.externalEntities_[index].positionTarget : nullptr;
    }

protected:
    /** @brief 外部の実体の数を返す */
    int Count(const StageEditor& editor) const override {
        return static_cast<int>(editor.externalEntities_.size());
    }
};

/** @brief 選択種別に対応する操作を取得する */
const StageEditor::ISelectionKind& StageEditor::SelectionKindOf(SelKind kind) {
    static const ISelectionKind kNone {};
    static const ObjectSelectionKind kObject {};
    static const TriggerSelectionKind kTrigger {};
    static const ExternalSelectionKind kExternal {};
    // SelKind の並び順と一致させる
    static const ISelectionKind* const kKinds[] = { &kNone, &kObject, &kTrigger, &kExternal };
    const size_t index = static_cast<size_t>(kind);
    return index < std::size(kKinds) ? *kKinds[index] : kNone;
}

/** @brief 指定位置からテストを始め、プレイヤー未登録なら警告を出す */
void StageEditor::StartPlayTestOrWarn(const Vector3& worldPosition) {
    if (!StartPlayTestAt(worldPosition)) {
        statusMessage_ = "このシーンにはプレイヤーが登録されていません";
        statusTimer_ = kStatusNormalSeconds;
    }
}

} // namespace engine::game
#endif // USE_IMGUI
