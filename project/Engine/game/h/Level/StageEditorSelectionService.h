/**
 * @file StageEditorSelectionService.h
 * @brief ステージエディタの選択対象に対する編集コマンドを実行するファイル
 */
#pragma once

namespace engine::game {

class StageEditor;

/**
 * @brief 選択対象の削除、複製、コピー、貼り付けを担当する
 *
 * 選択操作に伴うUndo記録、実体の再生成、親子参照の修復を一か所へ集約する。
 */
class StageEditorSelectionService {
public:
    /** @brief 選択中の配置物またはトリガーを削除する */
    static void DeleteSelected(StageEditor& editor);
    /** @brief 選択中の配置物またはトリガーを複製する */
    static void DuplicateSelected(StageEditor& editor);
    /** @brief 選択中の配置物を内部クリップボードへ複製する */
    static void CopySelected(StageEditor& editor);
    /** @brief 内部クリップボードの配置物を新しい実体として貼り付ける */
    static void PasteClipboard(StageEditor& editor);
};

} // namespace engine::game
