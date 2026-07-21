/**
 * @file StageEditorPanels.h
 * @brief ステージ編集の階層表示と詳細編集パネルを分担するファイル
 */
#pragma once

namespace engine::game {
class StageEditor;

/**
 * @brief ステージ要素を階層構造で提示するパネル
 * @details 配置物、トリガー、外部エンティティの選択操作と階層表示を担当する
 */
class StageEditorHierarchyPanel {
public:
    /**
     * @brief 現在の編集内容から階層パネルを描画する
     * @param editor 編集状態を所有するエディタ
     * @return なし
     */
    static void Render(StageEditor& editor);
};

/**
 * @brief 選択対象の設定を編集するパネル
 * @details 対象種別に応じたプロパティ表示、Undo記録、実体への反映を担当する
 */
class StageEditorInspectorPanel {
public:
    /**
     * @brief 現在の選択対象に対応する詳細パネルを描画する
     * @param editor 編集状態を所有するエディタ
     * @return なし
     */
    static void Render(StageEditor& editor);
};
} // namespace engine::game
