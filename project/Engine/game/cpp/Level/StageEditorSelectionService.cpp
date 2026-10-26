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
namespace {
constexpr float kCopyOffsetX = 1.0f; // スナップしていない時に、写しを元からずらす量
} // namespace

/** @brief 配置物の写しを新しい名前で末尾へ足し、少しずらして選択に加える（親子関係は外す） */
void StageEditorSelectionService::AppendCopy(StageEditor& editor, const ObjectDesc& source) {
    StageEditor::ObjectEntry entry;
    entry.desc = source;
    entry.desc.name = "obj_" + std::to_string(editor.nextSerial_++);
    entry.desc.parent.clear();
    entry.desc.position.x += editor.snapEnabled_ ? editor.snapStep_ : kCopyOffsetX;
    editor.objects_.push_back(std::move(entry));
    editor.RegenerateInstances(editor.objects_.back());
    editor.selectedObjectIndices_.push_back(static_cast<int>(editor.objects_.size()) - 1);
}

/** @brief 選択中の配置物またはトリガーを削除する */
void StageEditorSelectionService::DeleteSelected(StageEditor& editor) {
    if (!StageEditor::SelectionKindOf(editor.selKind_).Delete(editor, editor.selIndex_)) {
        return;
    }
    editor.selKind_ = StageEditor::SelKind::kNone;
    editor.selIndex_ = -1;
    editor.selectedObjectIndices_.clear();
}

/** @brief 選択中の配置物またはトリガーを複製する */
void StageEditorSelectionService::DuplicateSelected(StageEditor& editor) {
    StageEditor::SelectionKindOf(editor.selKind_).Duplicate(editor, editor.selIndex_);
}

/** @brief 選択中の配置物を内部クリップボードへ複製する */
void StageEditorSelectionService::CopySelected(StageEditor& editor) {
    editor.objectClipboard_.clear();
    if (editor.selKind_ != StageEditor::SelKind::kObject) {
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

/** @brief 内部クリップボードの配置物を新しい実体として貼り付ける */
void StageEditorSelectionService::PasteClipboard(StageEditor& editor) {
    if (editor.objectClipboard_.empty()) {
        return;
    }
    editor.RecordUndoSnapshotNow();
    editor.selectedObjectIndices_.clear();
    for (const ObjectDesc& source : editor.objectClipboard_) {
        AppendCopy(editor, source);
    }
    editor.selKind_ = StageEditor::SelKind::kObject;
    editor.selIndex_ = editor.selectedObjectIndices_.back();
    editor.statusMessage_ = std::to_string(editor.selectedObjectIndices_.size()) + "個貼り付けました";
    editor.statusTimer_ = StageEditor::kStatusBriefSeconds;
}


// StageEditor.cppからの分割StageEditor::の薄い転送メソッド本体はすべて上のStageEditorSelectionServiceが持つ
void StageEditor::DeleteSelected() {
    StageEditorSelectionService::DeleteSelected(*this);
}

/** @brief 選択中の配置物を別の名前で複製する */
void StageEditor::DuplicateSelected() {
    StageEditorSelectionService::DuplicateSelected(*this);
}

/** @brief 選択中の配置物をエディタのクリップボードへ写す */
void StageEditor::CopySelected() {
    StageEditorSelectionService::CopySelected(*this);
}

/** @brief クリップボードの配置物を新しい名前で貼り付ける（Undo できるように記録する） */
void StageEditor::PasteClipboard() {
    StageEditorSelectionService::PasteClipboard(*this);
}

} // namespace engine::game
#endif // USE_IMGUI
