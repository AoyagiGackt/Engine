/**
 * @file GraphEditor.h
 * @brief ノードグラフ（ビジュアルスクリプティング）をコードを書かずに組むためのImGuiエディタ
 * @note SceneEditorと同様、USE_IMGUIビルドでのみ動作するデバッグ/開発時専用ツール
 * 実行フロー（next/nextTrue/nextFalse）と型付きデータピン（paramLinks）の両方の配線に対応。
 * 配線されていないparamsはInspector的にテキスト/数値/チェックボックスで直接編集する
 */
#pragma once
#ifdef USE_IMGUI
#include "EditorHistory.h"
#include "GraphRuntime.h"
#include "GraphTypes.h"
#include <imgui.h>
#include <map>
#include <string>
#include <utility>
#include <vector>
namespace engine {
class Input;
}
namespace engine::game {
class GraphEditorInteraction;
class GraphNodeRenderer;

class GraphEditor {
    friend class GraphEditorInteraction;
    friend class GraphNodeRenderer;

public:
    static GraphEditor* GetInstance();

    /** @brief 指定パスのグラフを読み込んで編集対象にする（失敗時は空のグラフになる） */
    void Open(const std::string& path);

    /** @brief 現在編集中のグラフをOpen()したパスへ保存する */
    void Save();

    /**
     * @brief 毎フレーム呼ぶF1で表示/非表示を切り替える（非表示中はImGui呼び出し自体を行わない）
     * @param input F1キー判定用（nullptrなら表示中でもトグル操作は無視される）
     */
    void Update(engine::Input* input);

    /** @brief 現在編集中のグラフ（GraphRuntime::Start()に渡して実行確認する用） */
    const GraphDesc& GetGraph() const { return graph_; }

    /** @brief エディタが表示中か（ホットキーオーバーレイの表示中マーク用） */
    bool IsVisible() const { return visible_; }

private:
    GraphEditor() = default;
    GraphEditor(const GraphEditor&) = delete;
    GraphEditor& operator=(const GraphEditor&) = delete;

    /** @brief キャンバス1フレーム分のリンクドラッグ共有状態（ノード描画→プレビュー描画で受け渡す） */
    struct CanvasFrameState {
        ImVec2 linkFromScreenPos = { 0, 0 }; ///< ドラッグ中の実行リンクの始点
        bool linkFromFound = false;
        bool linkCompletedThisFrame = false;
        bool hoveringAnyPin = false; ///< 何かのピンにカーソルが乗っているか（右クリックメニュー抑制用）
        bool hoveringNode = false; ///< 何かのノードタイトルにカーソルが乗っているか（キャンバスの追加メニュー抑制用）
        ImVec2 dataLinkFromScreenPos = { 0, 0 }; ///< ドラッグ中のデータ配線の始点
        bool dataLinkFromFound = false;
        bool dataLinkCompletedThisFrame = false;

        // Run中ハイライト用（DrawCanvasが1フレームに1回だけ求めてDrawNodeへ配る）
        bool viewingActiveGraph = false; ///< 今表示中のグラフが実行チェーンの実際に動いている階層と一致するか
        std::string activeRunNodeId; ///< viewingActiveGraphがtrueの時だけ有効な、ハイライト対象ノードID
    };

    /** @brief 表示切替と編集入力の実処理を実行する */
    void UpdateContent(engine::Input* input);

    void DrawCanvas();
    // DrawCanvas() 分割ヘルパー（呼び出し順に宣言）
    /** @brief マウスホイールズームと中ボタンドラッグパンを処理する */
    void UpdateCanvasView(bool canvasHovered);
    /** @brief ノード1つ分（タイトル・パラメータ・ピン・枠）を描画し、ドラッグ操作を処理する */
    void DrawNode(ImDrawList* dl, const ImVec2& origin, const std::string& id, GraphNode& node, CanvasFrameState& state);
    /** @brief ノード一件の表示と操作の実処理を実行する */
    void DrawNodeContent(ImDrawList* dl, const ImVec2& origin, const std::string& id, GraphNode& node, CanvasFrameState& state);
    /** @brief ノードのパラメータ編集ウィジェットと左端のデータ入力ピンを描画する */
    void DrawNodeParams(ImDrawList* dl, const std::string& id, GraphNode& node, const ImVec2& nodeScreenPos, CanvasFrameState& state);
    /** @brief ドラッグ中のリンク／データ配線のプレビュー線を描き、空振り時はキャンセルする */
    void DrawLinkPreviews(ImDrawList* dl, const CanvasFrameState& state);
    /** @brief 右クリックのノード追加メニューとDeleteキー削除を処理する */
    void HandleCanvasShortcuts(const ImVec2& origin, bool canvasHovered, const CanvasFrameState& state);

    void DrawLinks(ImDrawList* dl);
    void DrawAddNodeMenu();
    /** @brief pendingAddX_/Y_の位置に、型ごとの初期パラメータを埋めたtype型のノードを1つ追加する */
    void AddNodeOfType(const std::string& type);
    void DeleteNode(const std::string& id);
    /** @brief idのノードを丸ごと複製する（新しいidを振り、少しずらした位置に置く） */
    void DuplicateNode(const std::string& id);
    void ArrangeNodes();

    /** @brief コメント（注釈用の色付き矩形）の描画・ドラッグ移動・リサイズ・テキスト編集 */
    void DrawComments(ImDrawList* dl, const ImVec2& origin);
    void DeleteComment(const std::string& id);

    /** @brief データ配線（paramLinks）の線をまとめて描く（ピン位置はDrawCanvasが同フレームで記録済み） */
    void DrawDataLinks(ImDrawList* dl);

    /** @brief Run中のグラフ変数を一覧表示し、その場で上書きテストできるパネル */
    void DrawVariablesPanel();

    // Undo/Redo（スナップショット方式、Ctrl+Z/Ctrl+Y）
    /** @brief 直前のgraph_を即座にUndoスタックへ積む（追加/削除/配線など単発で完結する変更の前に呼ぶ） */
    void RecordUndoSnapshotNow();
    /** @brief ドラッグ/テキスト編集の開始時に変更前を仮記録するIsItemActivated()の直後に呼ぶ */
    void BeginUndoCapture();
    /** @brief BeginUndoCapture()後、実際に値が変わったことを記録する（変更が無ければCommit時に捨てられる） */
    void MarkUndoDirty();
    /** @brief ドラッグ/テキスト編集の終了時に呼ぶ実際に変化していた場合のみUndoスタックへ確定する */
    void CommitUndoCapture();
    void Undo();
    void Redo();

    EditorHistory<GraphDesc> history_;

    std::string graphPath_ = "Resources/Graphs/test_graph.json";
    GraphDesc graph_;

    // 最後の保存以降に編集があるか（開く時の破棄確認と未保存マーカー表示に使う）
    bool dirty_ = false;

    // 未保存のまま開こうとした時の確認モーダル用（確認OK後にこのパスをOpen()する）
    std::string pendingConfirmOpenPath_;
    bool requestConfirmOpen_ = false;

    char nodeSearchBuf_[64] = { }; // ノード追加メニューの絞り込み検索欄

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

    // Subgraphノードの開くボタン用ノード走査ループ中にOpen()を呼ぶとイテレータが壊れるため遅延させる
    std::string pendingOpenPath_;

    // ノード右クリックメニューの削除/コピーも、同じ理由（ノード走査ループ中にgraph_.nodesを書き換えると
    // イテレータが壊れる）で遅延させるループの外（DrawCanvas末尾）でまとめて処理する
    std::string pendingDeleteNodeId_;
    std::string pendingDuplicateNodeId_;

    // ノード追加メニューを開いた位置（グラフ座標系）
    float pendingAddX_ = 0.0f;
    float pendingAddY_ = 0.0f;

    int nextNodeSerial_ = 0; // 新規ノードID生成用（"node_0", "node_1", ...）

    // 直近フレームで描画した各ノードのスクリーン矩形（min, max）DrawLinks()が正確なピン位置を出すために使う
    std::map<std::string, std::pair<ImVec2, ImVec2>> nodeRects_;

    std::string statusMessage_; // Save後などに表示する一時メッセージ
    float statusTimer_ = 0.0f;

    // Runボタンで動かす簡易実行インスタンス（表示・操作専用、GraphRuntime本体はGraphTypes.h参照）
    GraphRuntime testRuntime_;
    // 実行開始時に開いていたグラフのパス（GetActiveLeaf()のパスが空＝トップレベル実行中の判定に使う）
    std::string testRuntimeRootPath_;
};

} // namespace engine::game
#endif // USE_IMGUI
