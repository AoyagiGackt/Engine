/**
 * @file StageEditorSelectionService.cpp
 * @brief ステージエディタの選択対象に対する編集コマンドを実行する
 */
#ifdef USE_IMGUI
#include "StageEditorSelectionService.h"

#include "DirectXCommon.h"
#include "EnemyEntity.h"
#include "KnightEnemy.h"
#include "ModelCommon.h"
#include "StageEditor.h"

#include <algorithm>

namespace engine::game {

void StageEditorSelectionService::DeleteSelected(StageEditor& editor)
{
    if (editor.selKind_ == StageEditor::SelKind::Object && editor.selIndex_ >= 0
        && editor.selIndex_ < static_cast<int>(editor.objects_.size())) {
        editor.RecordUndoSnapshotNow();
        if (editor.modelCommon_ && editor.modelCommon_->GetDxCommon()) {
            editor.modelCommon_->GetDxCommon()->WaitForGpu();
        }
        std::vector<int> targets = editor.selectedObjectIndices_.empty()
            ? std::vector<int> { editor.selIndex_ }
            : editor.selectedObjectIndices_;
        std::sort(targets.begin(), targets.end());
        targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
        for (auto iterator = targets.rbegin(); iterator != targets.rend(); ++iterator) {
            const int index = *iterator;
            if (index < 0 || index >= static_cast<int>(editor.objects_.size())) {
                continue;
            }
            // 子のワールド位置を維持してから削除対象への親参照を解除する
            const std::string deletedName = editor.objects_[index].desc.name;
            for (auto& other : editor.objects_) {
                if (other.desc.parent == deletedName) {
                    other.desc.position = editor.WorldPositionOf(other.desc);
                    other.desc.parent.clear();
                }
            }
            editor.DestroyObjectRuntime(editor.objects_[index], true);
            editor.objects_.erase(editor.objects_.begin() + index);
        }
    } else if (editor.selKind_ == StageEditor::SelKind::Trigger && editor.selIndex_ >= 0
        && editor.selIndex_ < static_cast<int>(editor.triggers_.size())) {
        editor.RecordUndoSnapshotNow();
        editor.triggers_.erase(editor.triggers_.begin() + editor.selIndex_);
    } else if (editor.selKind_ == StageEditor::SelKind::External && editor.selIndex_ >= 0
        && editor.selIndex_ < static_cast<int>(editor.externalEntities_.size())
        && editor.externalEntities_[editor.selIndex_].onDelete) {
        // シーン所有の背景オブジェクト等onDeleteが設定されたエンティティのみ削除できる
        // （JSON/Undoの管理対象外なので、実体の破棄は登録元シーンのコールバックに委ねる）
        editor.externalEntities_[editor.selIndex_].onDelete();
        editor.externalEntities_.erase(editor.externalEntities_.begin() + editor.selIndex_);
    } else {
        return;
    }
    editor.selKind_ = StageEditor::SelKind::None;
    editor.selIndex_ = -1;
    editor.selectedObjectIndices_.clear();
}

void StageEditorSelectionService::DuplicateSelected(StageEditor& editor)
{
    if (editor.selKind_ == StageEditor::SelKind::Object && editor.selIndex_ >= 0
        && editor.selIndex_ < static_cast<int>(editor.objects_.size())) {
        editor.RecordUndoSnapshotNow();
        const std::vector<int> sources = editor.selectedObjectIndices_.empty()
            ? std::vector<int> { editor.selIndex_ }
            : editor.selectedObjectIndices_;
        std::vector<ObjectDesc> copies;
        for (int index : sources) {
            if (index >= 0 && index < static_cast<int>(editor.objects_.size())) {
                copies.push_back(editor.objects_[index].desc);
            }
        }
        editor.selectedObjectIndices_.clear();
        for (ObjectDesc& desc : copies) {
            StageEditor::ObjectEntry entry;
            entry.desc = std::move(desc);
            entry.desc.name = "obj_" + std::to_string(editor.nextSerial_++);
            entry.desc.parent.clear();
            entry.desc.position.x += editor.snapEnabled_ ? editor.snapStep_ : 1.0f;
            editor.objects_.push_back(std::move(entry));
            editor.RegenerateInstances(editor.objects_.back());
            editor.selectedObjectIndices_.push_back(static_cast<int>(editor.objects_.size()) - 1);
        }
        editor.selKind_ = StageEditor::SelKind::Object;
        editor.selIndex_ = static_cast<int>(editor.objects_.size()) - 1;
        editor.statusMessage_ = "複製しました";
        editor.statusTimer_ = 1.5f;
    } else if (editor.selKind_ == StageEditor::SelKind::Trigger && editor.selIndex_ >= 0
        && editor.selIndex_ < static_cast<int>(editor.triggers_.size())) {
        editor.RecordUndoSnapshotNow();
        TriggerDesc desc = editor.triggers_[editor.selIndex_].GetDesc();
        desc.name = "trigger_" + std::to_string(editor.nextSerial_++);
        desc.position.x += editor.snapEnabled_ ? editor.snapStep_ : 1.0f;
        TriggerVolume trigger;
        trigger.Init(desc);
        editor.triggers_.push_back(std::move(trigger));
        editor.selKind_ = StageEditor::SelKind::Trigger;
        editor.selIndex_ = static_cast<int>(editor.triggers_.size()) - 1;
        editor.statusMessage_ = "複製しました";
        editor.statusTimer_ = 1.5f;
    }
}

void StageEditorSelectionService::CopySelected(StageEditor& editor)
{
    editor.objectClipboard_.clear();
    if (editor.selKind_ != StageEditor::SelKind::Object) {
        return;
    }
    const std::vector<int> sources = editor.selectedObjectIndices_.empty()
        ? std::vector<int> { editor.selIndex_ }
        : editor.selectedObjectIndices_;
    for (int index : sources) {
        if (index >= 0 && index < static_cast<int>(editor.objects_.size())) {
            editor.objectClipboard_.push_back(editor.objects_[index].desc);
        }
    }
    editor.statusMessage_ = std::to_string(editor.objectClipboard_.size()) + "個コピーしました";
    editor.statusTimer_ = 1.5f;
}

void StageEditorSelectionService::PasteClipboard(StageEditor& editor)
{
    if (editor.objectClipboard_.empty()) {
        return;
    }
    editor.RecordUndoSnapshotNow();
    editor.selectedObjectIndices_.clear();
    for (const ObjectDesc& source : editor.objectClipboard_) {
        StageEditor::ObjectEntry entry;
        entry.desc = source;
        entry.desc.name = "obj_" + std::to_string(editor.nextSerial_++);
        entry.desc.parent.clear();
        entry.desc.position.x += editor.snapEnabled_ ? editor.snapStep_ : 1.0f;
        editor.objects_.push_back(std::move(entry));
        editor.RegenerateInstances(editor.objects_.back());
        editor.selectedObjectIndices_.push_back(static_cast<int>(editor.objects_.size()) - 1);
    }
    editor.selKind_ = StageEditor::SelKind::Object;
    editor.selIndex_ = editor.selectedObjectIndices_.back();
    editor.statusMessage_ = std::to_string(editor.selectedObjectIndices_.size()) + "個貼り付けました";
    editor.statusTimer_ = 1.5f;
}

} // namespace engine::game

// StageEditor.cppからの分割StageEditor::の薄い転送メソッド本体はすべて上のStageEditorSelectionServiceが持つ
void engine::game::StageEditor::DeleteSelected()
{
    StageEditorSelectionService::DeleteSelected(*this);
}

void engine::game::StageEditor::DuplicateSelected()
{
    StageEditorSelectionService::DuplicateSelected(*this);
}

void engine::game::StageEditor::CopySelected()
{
    StageEditorSelectionService::CopySelected(*this);
}

void engine::game::StageEditor::PasteClipboard()
{
    StageEditorSelectionService::PasteClipboard(*this);
}
#endif // USE_IMGUI
