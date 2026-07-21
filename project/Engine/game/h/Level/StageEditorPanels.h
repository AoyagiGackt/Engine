/**
 * @file StageEditorPanels.h
 * @brief ステージ編集の階層表示と詳細編集パネルを分担するファイル
 */
#pragma once

namespace engine::game {
class StageEditor;

/** @brief 配置物とトリガーの階層表示を担当する */
class StageEditorHierarchyPanel {
public:
    /** @brief 現在の編集内容から階層パネルを描画する @param editor 編集状態を所有するエディタ */
    static void Render(StageEditor& editor);
};

/** @brief 選択対象のプロパティ編集表示を担当する */
class StageEditorInspectorPanel {
public:
    /** @brief 現在の選択対象に対応する詳細パネルを描画する @param editor 編集状態を所有するエディタ */
    static void Render(StageEditor& editor);
};
} // namespace engine::game
