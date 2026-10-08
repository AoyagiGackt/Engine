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
    if (!StageEditor::SelectionKindOf(editor.selKind_).Delete(editor, editor.selIndex_)) {
        return;
    }
    editor.selKind_ = StageEditor::SelKind::None;
    editor.selIndex_ = -1;
    editor.selectedObjectIndices_.clear();
}

void StageEditorSelectionService::DuplicateSelected(StageEditor& editor)
{
    StageEditor::SelectionKindOf(editor.selKind_).Duplicate(editor, editor.selIndex_);
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
    editor.statusTimer_ = StageEditor::kStatusBriefSeconds;
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
    editor.statusTimer_ = StageEditor::kStatusBriefSeconds;
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
