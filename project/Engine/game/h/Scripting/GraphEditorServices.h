/**
 * @file GraphEditorServices.h
 * @brief グラフ編集の入力処理とノード表示を分担するファイル
 */
#pragma once
#ifdef USE_IMGUI
#include <imgui.h>
#include <string>
namespace engine { class Input; }
namespace engine::game {
class GraphEditor;
struct GraphNode;

/**
 * @brief グラフエディタの入力を処理するサービス
 * @details 表示切替、ショートカット、キャンバス操作、ノード選択の更新を担当する
 */
class GraphEditorInteraction {
public:
    /**
     * @brief 1フレーム分の編集入力を処理する
     * @param editor 編集状態
     * @param input 入力管理。未指定の場合はキーボード入力を省略する
     * @return なし
     */
    static void Update(GraphEditor& editor, engine::Input* input);
};

/**
 * @brief グラフノードを描画するサービス
 * @details ノード本体、入出力ピン、値編集UI、選択枠の描画を担当する
 */
class GraphNodeRenderer {
public:
    /**
     * @brief ノード一件の表示と操作を処理する
     * @param editor 編集状態
     * @param drawList 描画先
     * @param origin キャンバス原点
     * @param id ノード識別子
     * @param node ノードデータ
     * @param state フレーム共有状態
     * @return なし
     */
    static void Draw(GraphEditor& editor, ImDrawList* drawList, const ImVec2& origin,
        const std::string& id, GraphNode& node, void* state);
};
} // namespace engine::game
#endif
