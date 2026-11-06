/**
 * @file GraphEditor.h
 * @brief ノードグラフ（ビジュアルスクリプティング）をコードを書かずに組むためのImGuiエディタ
 * @note SceneEditorと同様、USE_IMGUIビルドでのみ動作するデバッグ/開発時専用ツール
 * 実行フロー（next/nextTrue/nextFalse）と型付きデータピン（paramLinks）の両方の配線に対応。
 * 配線されていないparamsはInspector的にテキスト/数値/チェックボックスで直接編集する
 */
#pragma once
#include "TextBufferCapacity.h"
#ifdef USE_IMGUI
#include "EditorHistory.h"
#include "GraphRuntime.h"
#include "GraphTypes.h"
#include <imgui.h>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>
namespace engine {
class Input;
}
namespace engine::game {
class GraphEditorInteraction;
class GraphNodeRenderer;

/** @brief キャンバス上のピンの種類（配線解除の表はこの並びで引く） */
enum class GraphPinKind {
    kExecIn,
    kExecOut,
    kDataIn,
    kDataOut,
};

/** @brief そのフレームに描いたピン1つぶんの位置と接続情報 */
struct GraphCanvasPin {
    GraphPinKind kind = GraphPinKind::kExecIn;
    std::string nodeId;
    std::string name; ///< 実行出力ならピン名（next/true/false）、データ入力ならパラメータ名
    ImVec2 position = { 0.0f, 0.0f };
    float radius = 0.0f;
    GraphValueType type = GraphValueType::kAny;
};

/**
 * @brief ノードグラフをキャンバス上で編集するImGuiエディタ本体
 * @details グラフの読み込み/保存、ノード・コメントの追加削除、実行フロー配線とデータ配線の
 * ドラッグ操作、Undo/Redo、テスト実行（Run）までの編集機能一式をまとめて保持する
 */
class GraphEditor {
    friend class GraphEditorInteraction;
    friend class GraphNodeRenderer;

public:
    /**
     * @brief 唯一のGraphEditorインスタンスを取得する（未生成なら生成する）
     * @return GraphEditorのインスタンス
     */
    static GraphEditor* GetInstance();

    /** @brief 指定パスのグラフを読み込んで編集対象にする（失敗時は空のグラフになる） */
    void Open(const std::string& path);

    /** @brief 現在編集中のグラフをOpen()したパスへ保存する */
    void Save();

    /**
     * @brief 指定パスのグラフを開き、エディタを表示状態にする（ステージエディタのグラフパネルから呼ぶ）
     * @note F1トグルと同じくゲームを一時停止し、閉じた時に元のタイムスケールへ戻す
     */
    void OpenAndShow(const std::string& path);

    /**
     * @brief 毎フレーム呼ぶF1で表示/非表示を切り替える（非表示中はImGui呼び出し自体を行わない）
     * @param input F1キー判定用（nullptrなら表示中でもトグル操作は無視される）
     */
    void Update(engine::Input* input);


    /** @brief エディタが表示中か（ホットキーオーバーレイの表示中マーク用） */
    bool IsVisible() const {
        return visible_;
    }

private:
    /** @brief 履歴から復元したグラフを反映し、選択と変更通知を更新する */
    void ApplyHistorySnapshot(std::optional<GraphDesc> snapshot, const std::string& message);

    /** @brief 編集中グラフが未選択のエディタを作る */
    GraphEditor() = default;
    GraphEditor(const GraphEditor&) = delete;
    GraphEditor& operator=(const GraphEditor&) = delete;

    /** @brief キャンバス1フレーム分の共有状態（ノード描画で集め、その後の入力処理で使う） */
    struct CanvasFrameState {
        bool canvasHovered = false; ///< カーソルがキャンバス上にあり、他のウィンドウやポップアップに遮られていないか
        bool nodePressedThisFrame = false; ///< このフレームにノードのタイトルが押されたか
        std::vector<GraphCanvasPin> pins; ///< このフレームに描いた全ピン
        const GraphCanvasPin* hoveredPin = nullptr; ///< カーソルの下のピン
        const GraphCanvasPin* snapPin = nullptr; ///< 配線中に吸着しているピン
        std::string hoveredNodeId; ///< カーソルの下のノード（無ければ空）
        std::optional<GraphCanvasPin> hoveredLink; ///< カーソルの下の配線（配線の持ち主のピンで表す）
        ImVec2 hoveredLinkFrom = { 0.0f, 0.0f };
        ImVec2 hoveredLinkTo = { 0.0f, 0.0f };
        float hoveredLinkDistance = 0.0f;

        // Run中ハイライト用（DrawCanvasが1フレームに1回だけ求めてDrawNodeへ配る）
        bool viewingActiveGraph = false; ///< 今表示中のグラフが実行チェーンの実際に動いている階層と一致するか
        std::string activeRunNodeId; ///< viewingActiveGraphがtrueの時だけ有効な、ハイライト対象ノードID
    };

    /** @brief キャンバス上でマウスを押したまま続く操作の種類 */
    enum class CanvasDragKind {
        kNone,
        kNodes,
        kCommentMove,
        kCommentResize,
        kBoxSelect,
    };

    /** @brief マウスを押したまま続く操作の開始時点の値（ホバーが外れても離すまで続ける） */
    struct CanvasDrag {
        CanvasDragKind kind = CanvasDragKind::kNone;
        ImVec2 startMouseGraph = { 0.0f, 0.0f }; ///< 押した時のマウス位置（グラフ座標）
        std::map<std::string, ImVec2> nodeStarts; ///< 動かすノードの開始位置
        std::string pressedNodeId; ///< 動かさずに離したら単独選択に絞るノード
        std::string commentId; ///< 操作中のコメント
        ImVec2 commentStart = { 0.0f, 0.0f }; ///< コメントの開始位置または開始サイズ
        std::set<std::string> baseSelection; ///< 範囲選択を始める前からの選択
        bool moved = false; ///< しきい値を超えて動いたか（クリックだけでは動かさない）
    };

    /** @brief コメント枠の画面上の矩形 */
    struct CommentScreenRect {
        ImVec2 min;
        ImVec2 max;
        ImVec2 headerMax; ///< つかんで動かす帯の右下
        ImVec2 handleMin; ///< 大きさを変える掴みの左上
    };

    void DrawCanvas();
    // DrawCanvas() 分割ヘルパー（呼び出し順に宣言）
    /** @brief ホイールズーム（カーソル位置が中心）・中ドラッグとスペース+左ドラッグのパン・端での自動スクロールを処理する */
    void UpdateCanvasView(bool canvasHovered);
    /** @brief 配線中とドラッグ中にカーソルがキャンバスの端へ寄ったら表示を流す */
    void UpdateEdgeAutoPan();
    /** @brief 背景の格子を描く（パンとズームの量を見て分かるようにする） */
    void DrawGrid(ImDrawList* dl);
    /** @brief ノード1つ分（タイトル・パラメータ・ピン・枠）を描画し、ドラッグ操作を処理する */
    void DrawNode(ImDrawList* dl, const ImVec2& origin, const std::string& id, GraphNode& node, CanvasFrameState& state);
    /** @brief ノード一件の表示と操作の実処理を実行する */
    /** @brief ノードのパラメータ編集ウィジェットと左端のデータ入力ピンを描画する */
    void DrawNodeParams(ImDrawList* dl, const std::string& id, GraphNode& node, const ImVec2& nodeScreenPos, CanvasFrameState& state);
    /** @brief カーソルの下のノードを求める */
    void UpdateHoveredNode(CanvasFrameState& state);
    /** @brief ピンのホバー・配線の開始と吸着と接続・右クリックの配線解除を処理する */
    void UpdatePinInteraction(ImDrawList* dl, CanvasFrameState& state);
    /** @brief ピンにつながる配線を外す（外す物が無ければ履歴に残さない） */
    void DisconnectPin(const GraphCanvasPin& pin);
    /** @brief 今の配線ドラッグをこのピンへつなげられるか */
    bool CanConnectLinkTo(const GraphCanvasPin& pin) const;
    /** @brief 配線ドラッグをピンへつなぐ */
    void ConnectLinkTo(const GraphCanvasPin& pin);
    /** @brief つなげない場所で離した時に、ピンの近くなら理由を表示する */
    void ReportRejectedDrop(const CanvasFrameState& state);
    /** @brief ドラッグ中のリンク／データ配線のプレビュー線を描く（吸着中は吸着先のピンまで引く） */
    void DrawLinkPreviews(ImDrawList* dl, const CanvasFrameState& state);
    /** @brief 配線を1本描き、カーソルが近ければホバー中の配線の候補にする */
    void DrawWire(ImDrawList* dl, const ImVec2& from, const ImVec2& to, ImU32 color, float thickness,
        const GraphCanvasPin& owner, CanvasFrameState& state);
    /** @brief 左クリックを押した場所に応じてノード移動・コメント操作・範囲選択を始める */
    void HandleCanvasPress(const CanvasFrameState& state);
    /** @brief ノードを押した時の選択（Ctrl/Shiftで追加と解除）と移動の準備をする */
    void BeginNodePress(const std::string& id);
    /** @brief コメントの掴みや帯を押していたら操作を始める */
    bool TryBeginCommentDrag();
    /** @brief 押したまま続くノード移動をマウス位置に合わせて進める */
    void UpdateNodeDrag();
    /** @brief 押したまま続くコメントの移動とリサイズをマウス位置に合わせて進める */
    void UpdateCommentDrag();
    /** @brief 押したまま続く範囲選択を進め、選択枠を描く */
    void UpdateBoxSelect(ImDrawList* dl);
    /** @brief カーソルの下の掴める物に合わせてカーソルの形を変える */
    void UpdateHoverCursor(const CanvasFrameState& state);
    /** @brief 右クリックメニュー・Delete・全体表示などの操作を処理する */
    void HandleCanvasShortcuts(const CanvasFrameState& state);
    /** @brief 選択中のノードを対象にする右クリックメニューを描く */
    void DrawNodeContextMenu();

    void DrawLinks(ImDrawList* dl, CanvasFrameState& state);
    void DrawAddNodeMenu();
    /** @brief pendingAddX_/Y_の位置に、型ごとの初期パラメータを埋めたtype型のノードを1つ追加する */
    void AddNodeOfType(const std::string& type);
    /** @brief 指定ノードと、そのノードへの参照を取り除く（履歴の記録は呼び出し側で行う） */
    void RemoveNode(const std::string& id);
    /** @brief 選択中のノードをまとめて1回の操作として削除する */
    void DeleteSelectedNodes();
    /** @brief 選択中のノードを複製する（選択の中どうしの配線は複製先へつなぎ替える） */
    void DuplicateSelectedNodes();
    /** @brief グラフ内で使われていないノードIDを作る */
    std::string MakeUniqueNodeId();
    /** @brief グラフ内で使われていないコメントIDを作る */
    std::string MakeUniqueCommentId();
    void ArrangeNodes();

    /** @brief ノードを1つだけ選択する */
    void SelectOnlyNode(const std::string& id);
    /** @brief ノードとコメントの選択をすべて解除する */
    void ClearSelection();
    /** @brief ノードが選択中か */
    bool IsNodeSelected(const std::string& id) const;
    /** @brief 全ノードを選択する */
    void SelectAllNodes();
    /** @brief 消えたノードを選択から外し、基準ノードを選び直す */
    void SyncSelection();
    /** @brief 進行中の配線とドラッグをすべて取り消す */
    void CancelCanvasInteractions();

    /** @brief 全ノード（selectedOnly なら選択中のノードだけ）が収まるように表示を合わせる */
    void FrameNodes(bool selectedOnly);

    /** @brief 画面座標をグラフ座標へ変換する */
    ImVec2 ScreenToGraph(const ImVec2& screen) const;
    /** @brief グラフ座標を画面座標へ変換する */
    ImVec2 GraphToScreen(const ImVec2& graph) const;

    /** @brief コメント枠の画面上の矩形を求める */
    CommentScreenRect GetCommentScreenRect(const GraphComment& comment) const;
    /** @brief コメント（注釈用の色付き矩形）の描画とテキスト編集（移動とリサイズは UpdateCommentDrag） */
    void DrawComments(ImDrawList* dl);
    void DeleteComment(const std::string& id);

    /** @brief データ配線（paramLinks）の線をまとめて描く（ピン位置はDrawCanvasが同フレームで記録済み） */
    void DrawDataLinks(ImDrawList* dl, CanvasFrameState& state);

    /** @brief Run中のグラフ変数を一覧表示し、その場で上書きテストできるパネル */
    void DrawVariablesPanel();

    // Undo/Redo（スナップショット方式、Ctrl+Z/Ctrl+Y）
    /** @brief 直前のgraph_を即座にUndoスタックへ積む（追加/削除/配線など単発で完結する変更の前に呼ぶ） */
    void RecordUndoSnapshotNow();
    /** @brief ドラッグ/テキスト編集の開始時に変更前を仮記録する。IsItemActivated() の直後に呼ぶ */
    void BeginUndoCapture();
    /** @brief BeginUndoCapture()後、実際に値が変わったことを記録する（変更が無ければCommit時に捨てられる） */
    void MarkUndoDirty();
    /** @brief ドラッグ/テキスト編集の終了時に呼ぶ。実際に変化していた場合だけ Undo スタックへ確定する */
    void CommitUndoCapture();
    /**
     * @brief 直前のImGui項目の操作開始・変更・終了に合わせて Begin/Mark/Commit をまとめて呼ぶ
     * @param changed この項目でこのフレームに値が変わったか
     * @return changed をそのまま返す
     */
    bool CaptureItemUndo(bool changed);
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

    char nodeSearchBuf_[textBufferCapacity::kIdentifier] = { }; // ノード追加メニューの絞り込み検索欄

    // F1で表示/非表示を切り替える全画面表示中はゲーム画面が透けて動いて見えないようにする
    bool visible_ = false;
    float savedTimeScale_ = 1.0f; // 開いた時点のTimeManagerのタイムスケール（閉じたら復元する）

    float panOffsetX_ = 40.0f;
    float panOffsetY_ = 40.0f;
    float zoom_ = 1.0f; // マウスホイールで変更（カーソル位置を中心に拡大縮小する）

    ImVec2 canvasOrigin_ = { 0.0f, 0.0f }; // キャンバスの原点（画面座標、毎フレーム更新）
    ImVec2 canvasSize_ = { 0.0f, 0.0f }; // キャンバスの大きさ（毎フレーム更新）
    bool requestFrameAll_ = false; // 次のキャンバス描画で全体表示に合わせる（グラフを開いた時など）
    bool panning_ = false; // パン中か（押した場所がキャンバスなら離すまで続ける）
    ImGuiMouseButton panButton_ = ImGuiMouseButton_Middle; // パンを始めたボタン
    CanvasDrag drag_; // 押したまま続く操作の状態
    bool showHelpPanel_ = true; // 右側の説明パネルを出すか
    std::string contextNodeId_; // 右クリックメニューの対象ノード

    std::string selectedNodeId_; // 選択の基準ノード（説明の表示や開始ノードの設定に使う）
    std::set<std::string> selectedNodeIds_; // 選択中の全ノード（selectedNodeId_ を含む）
    std::string selectedCommentId_; // ノードとコメントは排他選択（片方を選んだらもう片方はクリア）
    int nextCommentSerial_ = 0; // 新規コメントID生成用（"comment_0", "comment_1", ...）

    // リンク（実行フロー配線）をドラッグ中の状態
    bool linking_ = false;
    std::string linkFromNodeId_;
    std::string linkFromPin_; // "next" | "true" | "false"

    // データ配線（出力データピン→入力データピン）をドラッグ中の状態
    bool dataLinking_ = false;
    std::string dataLinkFromNodeId_;
    GraphValueType dataLinkFromType_ = GraphValueType::kAny;

    // Subgraphノードの開くボタン用ノード走査ループ中にOpen()を呼ぶとイテレータが壊れるため遅延させる
    std::string pendingOpenPath_;

    // ノード追加メニューを開いた位置（グラフ座標系）
    float pendingAddX_ = 0.0f;
    float pendingAddY_ = 0.0f;

    int nextNodeSerial_ = 0; // 新規ノードID生成用（"node_0", "node_1", ...）

    // 直近フレームで描画した各ノードのスクリーン矩形（min, max）DrawLinks()が正確なピン位置を出すために使う
    std::map<std::string, std::pair<ImVec2, ImVec2>> nodeRects_;

    std::string statusMessage_; // Save後などに表示する一時メッセージ
    float statusTimer_ = 0.0f;
    // 一時メッセージの表示秒数（内容の重要度に応じて使い分ける）
    static constexpr float kStatusBriefSeconds = 1.5f;
    static constexpr float kStatusShortSeconds = 2.0f;
    static constexpr float kStatusNormalSeconds = 3.0f;

    // Runボタンで動かす簡易実行インスタンス（表示・操作専用、GraphRuntime本体はGraphTypes.h参照）
    GraphRuntime testRuntime_;
    // 実行開始時に開いていたグラフのパス（GetActiveLeaf()のパスが空＝トップレベル実行中の判定に使う）
    std::string testRuntimeRootPath_;
};

} // namespace engine::game
#endif // USE_IMGUI
