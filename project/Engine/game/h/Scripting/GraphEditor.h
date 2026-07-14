/**
 * @file GraphEditor.h
 * @brief ノードグラフ（ビジュアルスクリプティング）をコードを書かずに組むためのImGuiエディタ
 * @note SceneEditorと同様、USE_IMGUIビルドでのみ動作するデバッグ/開発時専用ツール
 * V1は実行フロー（next/nextTrue/nextFalse）の配線のみ対応データピンの配線は未対応
 * （NodeのparamsはInspector的にテキスト/数値/チェックボックスで直接編集する）
 */
#pragma once
#ifdef USE_IMGUI
#include "GraphTypes.h"
#include <imgui.h>
#include <map>
#include <string>
#include <utility>
namespace engine {
class Input;
}
namespace engine::game {

class GraphEditor {
public:
    static GraphEditor* GetInstance();

    /// @brief 指定パスのグラフを読み込んで編集対象にする（失敗時は空のグラフになる）
    void Open(const std::string& path);

    /// @brief 現在編集中のグラフをOpen()したパスへ保存する
    void Save();

    /**
     * @brief 毎フレーム呼ぶF1で表示/非表示を切り替える（非表示中はImGui呼び出し自体を行わない）
     * @param input F1キー判定用（nullptrなら表示中でもトグル操作は無視される）
     */
    void Update(engine::Input* input);

    /// @brief 現在編集中のグラフ（GraphRuntime::Start()に渡して実行確認する用）
    const GraphDesc& GetGraph() const { return graph_; }

private:
    GraphEditor() = default;
    GraphEditor(const GraphEditor&) = delete;
    GraphEditor& operator=(const GraphEditor&) = delete;

    void DrawCanvas();
    void DrawLinks(ImDrawList* dl);
    void DrawAddNodeMenu();
    void DeleteNode(const std::string& id);
    void ArrangeNodes();

    /// @brief コメント（注釈用の色付き矩形）の描画・ドラッグ移動・リサイズ・テキスト編集
    void DrawComments(ImDrawList* dl, const ImVec2& origin);
    void DeleteComment(const std::string& id);

    /// @brief データ配線（paramLinks）の線をまとめて描く（ピン位置はDrawCanvasが同フレームで記録済み）
    void DrawDataLinks(ImDrawList* dl);

    /// @brief Run中のグラフ変数を一覧表示し、その場で上書きテストできるパネル
    void DrawVariablesPanel();

    std::string graphPath_ = "Resources/Graphs/test_graph.json";
    GraphDesc graph_;

    // F1で表示/非表示を切り替える全画面表示中はゲーム画面が透けて動いて見えないようにする
    bool visible_ = false;
    float savedTimeScale_ = 1.0f; // 開いた時点のTimeManagerのタイムスケール（閉じたら復元する）

    float panOffsetX_ = 40.0f;
    float panOffsetY_ = 40.0f;
    float zoom_ = 1.0f; // マウスホイールで変更（0.3〜2.5）

    std::string selectedNodeId_;
    std::string selectedCommentId_; // ノードとコメントは排他選択（片方を選んだらもう片方はクリア）
    int nextCommentSerial_ = 0; // 新規コメントID生成用（"comment_0", "comment_1", ...）

    // リンク（実行フロー配線）をドラッグ中の状態
    bool linking_ = false;
    std::string linkFromNodeId_;
    std::string linkFromPin_; // "next" | "true" | "false"

    // データ配線（出力データピン→入力データピン）をドラッグ中の状態
    bool dataLinking_ = false;
    std::string dataLinkFromNodeId_;
    GraphValueType dataLinkFromType_ = GraphValueType::Any;

    // 直近フレームで描画した各データピンのスクリーン位置（DrawDataLinksと接続判定用）
    std::map<std::string, std::map<std::string, ImVec2>> dataInPins_; // ノードID → パラメータ名 → 位置
    std::map<std::string, ImVec2> dataOutPins_; // ノードID → 出力ピン位置

    // Subgraphノードの「開く」ボタン用ノード走査ループ中にOpen()を呼ぶとイテレータが壊れるため遅延させる
    std::string pendingOpenPath_;

    // ノード追加メニューを開いた位置（グラフ座標系）
    float pendingAddX_ = 0.0f;
    float pendingAddY_ = 0.0f;

    int nextNodeSerial_ = 0; // 新規ノードID生成用（"node_0", "node_1", ...）

    // 直近フレームで描画した各ノードのスクリーン矩形（min, max）DrawLinks()が正確なピン位置を出すために使う
    std::map<std::string, std::pair<ImVec2, ImVec2>> nodeRects_;

    std::string statusMessage_; // Save後などに表示する一時メッセージ
    float statusTimer_ = 0.0f;
};

} // namespace engine::game
#endif // USE_IMGUI
