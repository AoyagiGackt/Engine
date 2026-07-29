/**
 * @file StageEditorPanels.h
 * @brief ステージ編集の階層表示と詳細編集パネルを分担するファイル
 */
#pragma once
#include <string>

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

private:
    /** @brief 制作ガイドの進捗チェックリストと、開く/保存等のファイル操作行を描画する */
    static void RenderGuideAndFileActions(StageEditor& editor);
    /** @brief 「オブジェクト」「エンティティ」「トリガー」の各ツリーをまとめて描画する（RenderObjectTree等の呼び出し元） */
    static void RenderEntityBrowser(StageEditor& editor);
    // RenderEntityBrowserの下請け（責務ごとに分割）
    /** @brief ゲームカメラの位置・回転を編集するセクションを描画する */
    static void RenderCameraSection(StageEditor& editor);
    /** @brief レベルファイルの開く/保存、元に戻す/やり直す、スナップ、親子リンク等の操作行を描画する */
    static void RenderFileAndHistoryActions(StageEditor& editor);
    /** @brief 階層検索ボックスを描画し、小文字化した検索語を返す（空なら全件一致扱い） */
    static std::string RenderSearchBar(StageEditor& editor);
    /** @brief 「オブジェクト」ツリー（新規追加ポップアップ・検索一致時のフラット表示・通常時の親子階層表示）を描画する */
    static void RenderObjectTree(StageEditor& editor, const std::string& searchTextLower);
    /** @brief 「エンティティ」（Player/Enemy等の外部登録実体）の一覧を描画する */
    static void RenderExternalEntityList(StageEditor& editor, const std::string& searchTextLower);
    /** @brief 「トリガー」の一覧と新規追加ボタンを描画する */
    static void RenderTriggerList(StageEditor& editor, const std::string& searchTextLower);
    /** @brief 選択中の配置物/トリガーに対する削除・複製・コピー&ペーストのボタン行を描画する */
    static void RenderSelectionActions(StageEditor& editor);
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

private:
    /** @brief 選択中が配置物(Object)の場合の詳細パネルをまとめて描画する（下記Render*の呼び出し元） */
    static bool RenderObjectInspector(StageEditor& editor);
    /** @brief 有効/無効・名前・親子関係を編集するセクションを描画する */
    static void RenderObjectIdentity(StageEditor& editor, bool& structuralDirty);
    /** @brief モデル/テクスチャ/種類（static・row）等、見た目に関するセクションを描画する */
    static void RenderObjectVisual(StageEditor& editor, bool& structuralDirty);
    /** @brief 位置・回転・スケール・並べ方（軸/個数/間隔）を編集するセクションを描画する */
    static void RenderObjectTransform(StageEditor& editor, bool& structuralDirty, bool& transformDirty);
    /** @brief 有効化フラグ・敵グループ・巡回ルート・ギミック動作等、種別固有のゲームプレイ設定を編集するセクションを描画する */
    static void RenderObjectGameplay(StageEditor& editor, bool& structuralDirty);
    /** @brief kind=="ui_text"の表示文字列・色・太字・大きさ・座標基準を編集するセクションを描画する */
    static void RenderObjectText(StageEditor& editor);
    /** @brief 選択中がトリガーの場合の詳細パネル（位置・半径・フラグ・成立条件）を描画する */
    static bool RenderTriggerInspector(StageEditor& editor);
    /** @brief 選択中が外部登録エンティティ(Player/Enemy等)の場合の詳細パネル（位置のみ）を描画する */
    static bool RenderExternalInspector(StageEditor& editor);
};
} // namespace engine::game
