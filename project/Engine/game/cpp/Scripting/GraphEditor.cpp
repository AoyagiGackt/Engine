/**
 * @file GraphEditor.cpp
 * @brief ノードグラフのエディタ（GraphEditor）のファイル操作・Undo・編集操作の実装
 */
#ifdef USE_IMGUI
#include "GraphEditor.h"
#include "EditorUI.h"
#include "GraphEditorServices.h"
#include "GraphRuntime.h"
#include "Input.h"
#include "NodeRegistry.h"
#include "TimeManager.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <imgui.h>
#include <optional>
#include <vector>

namespace engine::game {
namespace {
constexpr float kDuplicateOffset = 30.0f; // 複製したノードを元からずらして置く距離
}


// ══════════════════════════════════════════════════════
// ファイル操作とエディタ更新
// ══════════════════════════════════════════════════════

/** @brief 唯一の GraphEditor インスタンスを返す */
GraphEditor* GraphEditor::GetInstance() {
    static GraphEditor sInstance;
    return &sInstance;
}

/** @brief 指定パスのグラフを読み込んで編集対象にする（失敗時は空のグラフになる） */
void GraphEditor::Open(const std::string& path) {
    graphPath_ = path;
    graph_ = graphIO::Load(path);
    ClearSelection();
    CancelCanvasInteractions();

    // 手書きJSON等でeditorX/Yが無い（全ノードが原点に重なっている）場合は自動整列する
    bool allAtOrigin = !graph_.nodes.empty();
    for (const auto& [id, node] : graph_.nodes) {
        if (node.editorX != 0.0f || node.editorY != 0.0f) {
            allAtOrigin = false;
            break;
        }
    }
    if (allAtOrigin) {
        ArrangeNodes();
    }

    // 別ファイルを開いたら、直前のグラフに対するUndo/Redo履歴は無関係になるため破棄する
    history_.Clear();
    dirty_ = false;
    // 開いたグラフが画面の外に置かれていても見失わないよう、次の描画で全体を表示する
    requestFrameAll_ = true;

    statusMessage_ = "Opened: " + path;
    statusTimer_ = kStatusShortSeconds;
}

/** @brief 指定パスのグラフを開き、エディタを表示状態にする（ステージエディタのグラフパネルから呼ぶ） */
void GraphEditor::OpenAndShow(const std::string& path) {
    if (path.empty()) {
        return;
    }
    if (path != graphPath_ || graph_.nodes.empty()) {
        Open(path);
    }
    if (!visible_) {
        // F1トグルと同じ手順で背後のゲームを止める（閉じる時の復元はGraphEditorInteraction側）
        savedTimeScale_ = TimeManager::GetInstance()->GetTimeScale();
        TimeManager::GetInstance()->SetTimeScale(0.0f);
        visible_ = true;
    }
}

/** @brief 現在編集中のグラフをOpen()したパスへ保存する */
void GraphEditor::Save() {
    if (!graphIO::Save(graphPath_, graph_)) {
        statusMessage_ = "保存に失敗しました: " + graphPath_;
        statusTimer_ = kStatusShortSeconds;
        return;
    }
    dirty_ = false;
    statusMessage_ = "Saved: " + graphPath_;
    statusTimer_ = kStatusShortSeconds;
}

/** @brief 毎フレーム呼ぶF1で表示/非表示を切り替える（非表示中はImGui呼び出し自体を行わない） */
void GraphEditor::Update(Input* input) {
    GraphEditorInteraction::Update(*this, input);
}

// ══════════════════════════════════════════════════════
// 編集履歴
// ══════════════════════════════════════════════════════

/** @brief 今のグラフをすぐ Undo 履歴に積み、未保存にする（追加・削除・配線など単発の変更の前に呼ぶ） */
void GraphEditor::RecordUndoSnapshotNow() {
    history_.Record(graph_);
    dirty_ = true;
}

/** @brief ドラッグ/テキスト編集の開始時に変更前を仮記録する。IsItemActivated() の直後に呼ぶ */
void GraphEditor::BeginUndoCapture() {
    if (!history_.IsCapturing()) {
        history_.Begin(graph_);
    }
}

/** @brief BeginUndoCapture()後、実際に値が変わったことを記録する（変更が無ければCommit時に捨てられる） */
void GraphEditor::MarkUndoDirty() {
    history_.MarkChanged();
    dirty_ = true;
}

/** @brief ドラッグ/テキスト編集の終了時に呼ぶ。実際に変化していた場合だけ Undo スタックへ確定する */
void GraphEditor::CommitUndoCapture() {
    history_.Commit();
}

/** @brief 直前のImGui項目の操作開始・変更・終了に合わせて Begin/Mark/Commit をまとめて呼ぶ */
bool GraphEditor::CaptureItemUndo(bool changed) {
    // 操作を始めた時に変更前を記録し、値が変わったら印を付け、操作を終えた時に確定する
    if (ImGui::IsItemActivated()) {
        BeginUndoCapture();
    }
    if (changed) {
        MarkUndoDirty();
    }
    if (ImGui::IsItemDeactivated()) {
        CommitUndoCapture();
    }
    return changed;
}

/** @brief 復元可能なスナップショットだけを反映し、操作結果を表示する */
void GraphEditor::ApplyHistorySnapshot(std::optional<GraphDesc> snapshot, const std::string& message) {
    if (!snapshot) {
        return;
    }
    // 残っているノードの選択は保ち、保存が必要な状態へ切り替える
    graph_ = std::move(*snapshot);
    SyncSelection();
    if (graph_.comments.count(selectedCommentId_) == 0) {
        selectedCommentId_.clear();
    }
    CancelCanvasInteractions();
    dirty_ = true;
    statusMessage_ = message;
    statusTimer_ = kStatusBriefSeconds;
}

/** @brief 1つ前に記録したグラフの状態へ戻す */
void GraphEditor::Undo() {
    // ドラッグの途中で戻すと、離した時に履歴の順番が崩れるので受け付けない
    if (drag_.kind != CanvasDragKind::kNone) {
        return;
    }
    ApplyHistorySnapshot(history_.Undo(graph_), "元に戻しました");
}

/** @brief Undo で戻した状態をやり直す */
void GraphEditor::Redo() {
    if (drag_.kind != CanvasDragKind::kNone) {
        return;
    }
    ApplyHistorySnapshot(history_.Redo(graph_), "やり直しました");
}

// ══════════════════════════════════════════════════════
// ノードとコメントの編集
// ══════════════════════════════════════════════════════

/** @brief 指定ノードへ向かう実行の配線をすべて外す（外した物があれば true） */
bool ClearExecLinksTo(GraphDesc& graph, const std::string& nodeId) {
    bool changed = false;
    for (auto& [otherId, node] : graph.nodes) {
        for (const ExecPinDef& pin : kExecPins) {
            if (node.*pin.target == nodeId) {
                (node.*pin.target).clear();
                changed = true;
            }
        }
    }
    return changed;
}

/** @brief 指定ノードを供給元にするデータ配線をすべて外す（外した物があれば true） */
bool ClearDataLinksFrom(GraphDesc& graph, const std::string& nodeId) {
    size_t removed = 0;
    for (auto& [otherId, node] : graph.nodes) {
        removed += std::erase_if(node.paramLinks, [&nodeId](const auto& link) {
            return link.second == nodeId;
        });
    }
    return removed > 0;
}

/** @brief 指定ノードと、そのノードへの参照を取り除く（履歴の記録は呼び出し側で行う） */
void GraphEditor::RemoveNode(const std::string& id) {
    graph_.nodes.erase(id);
    if (graph_.startNodeId == id) {
        graph_.startNodeId.clear();
    }
    ClearExecLinksTo(graph_, id);
    ClearDataLinksFrom(graph_, id);
}

/** @brief 選択中のノードをまとめて1回の操作として削除する */
void GraphEditor::DeleteSelectedNodes() {
    if (selectedNodeIds_.empty()) {
        return;
    }
    RecordUndoSnapshotNow();
    for (const std::string& id : selectedNodeIds_) {
        RemoveNode(id);
    }
    ClearSelection();
}

/** @brief 選択中のノードを複製する（選択の中どうしの配線は複製先へつなぎ替える） */
void GraphEditor::DuplicateSelectedNodes() {
    if (selectedNodeIds_.empty()) {
        return;
    }
    RecordUndoSnapshotNow();

    // 新しいIDを振って、元と重ならないよう少しずらして置く
    std::map<std::string, std::string> newIds;
    for (const std::string& id : selectedNodeIds_) {
        const GraphNode* source = graph_.FindNode(id);
        if (source == nullptr) {
            continue;
        }
        GraphNode copy = *source; // params/paramLinks/next系も含めてまるごと複製する
        copy.id = MakeUniqueNodeId();
        copy.editorX += kDuplicateOffset;
        copy.editorY += kDuplicateOffset;
        newIds[id] = copy.id;
        graph_.nodes[copy.id] = std::move(copy);
    }

    // 選択の中どうしの配線は複製先どうしをつなぐ（選択の外への配線はそのまま残す）
    const auto remap = [&newIds](std::string& target) {
        const auto found = newIds.find(target);
        if (found != newIds.end()) {
            target = found->second;
        }
    };
    selectedNodeIds_.clear();
    for (const auto& [oldId, newId] : newIds) {
        GraphNode& copy = graph_.nodes[newId];
        for (const ExecPinDef& pin : kExecPins) {
            remap(copy.*pin.target);
        }
        for (auto& [key, sourceId] : copy.paramLinks) {
            remap(sourceId);
        }
        selectedNodeIds_.insert(newId);
    }
    remap(selectedNodeId_);
    SyncSelection();
    selectedCommentId_.clear();
}

/** @brief グラフ内で使われていないノードIDを作る */
std::string GraphEditor::MakeUniqueNodeId() {
    // 読み込んだグラフに同じ番号があると上書きしてしまうので、空いている番号まで進める
    std::string id;
    do {
        id = "node_" + std::to_string(nextNodeSerial_++);
    } while (graph_.nodes.count(id) > 0);
    return id;
}

/** @brief グラフ内で使われていないコメントIDを作る */
std::string GraphEditor::MakeUniqueCommentId() {
    std::string id;
    do {
        id = "comment_" + std::to_string(nextCommentSerial_++);
    } while (graph_.comments.count(id) > 0);
    return id;
}

/** @brief ノードを1つだけ選択する */
void GraphEditor::SelectOnlyNode(const std::string& id) {
    selectedNodeIds_.clear();
    selectedNodeIds_.insert(id);
    selectedNodeId_ = id;
    selectedCommentId_.clear();
}

/** @brief ノードとコメントの選択をすべて解除する */
void GraphEditor::ClearSelection() {
    selectedNodeIds_.clear();
    selectedNodeId_.clear();
    selectedCommentId_.clear();
}

/** @brief ノードが選択中か */
bool GraphEditor::IsNodeSelected(const std::string& id) const {
    return selectedNodeIds_.count(id) > 0;
}

/** @brief 全ノードを選択する */
void GraphEditor::SelectAllNodes() {
    selectedNodeIds_.clear();
    for (const auto& [id, node] : graph_.nodes) {
        selectedNodeIds_.insert(id);
    }
    selectedCommentId_.clear();
    SyncSelection();
}

/** @brief 消えたノードを選択から外し、基準ノードを選び直す */
void GraphEditor::SyncSelection() {
    std::erase_if(selectedNodeIds_, [this](const std::string& id) {
        return graph_.nodes.count(id) == 0;
    });
    if (selectedNodeIds_.count(selectedNodeId_) == 0) {
        selectedNodeId_ = selectedNodeIds_.empty() ? std::string() : *selectedNodeIds_.begin();
    }
}

/** @brief 進行中の配線とドラッグをすべて取り消す */
void GraphEditor::CancelCanvasInteractions() {
    linking_ = false;
    dataLinking_ = false;
    panning_ = false;
    drag_ = CanvasDrag { };
}

/** @brief 指定したコメントをグラフから削除する */
void GraphEditor::DeleteComment(const std::string& id) {
    RecordUndoSnapshotNow();
    graph_.comments.erase(id);
}

/** @brief 開始ノードからの深さごとに列を作ってノードを並べ直す（Undo できるように記録する） */
void GraphEditor::ArrangeNodes() {
    // 並べ替え前の配置を元に戻せるよう残しておく
    RecordUndoSnapshotNow();
    // 開始ノードからのBFSで各ノードの深さ（開始から何手先か）を求め、深さごとに列を作って並べる
    std::map<std::string, int> depth;
    std::vector<std::string> queue;
    size_t queueHead = 0;

    if (!graph_.startNodeId.empty() && graph_.nodes.count(graph_.startNodeId)) {
        depth[graph_.startNodeId] = 0;
        queue.push_back(graph_.startNodeId);
    }

    // 分岐ノードは真/偽、それ以外は次へ の行き先をたどり、初めて着いたノードに深さを付ける
    while (queueHead < queue.size()) {
        const std::string cur = queue[queueHead++];
        const GraphNode* node = graph_.FindNode(cur);
        if (!node) {
            continue;
        }

        std::vector<std::string> nexts;
        const bool branches = NodeRegistry::GetInstance()->IsBranchType(node->type);
        for (const ExecPinDef& pin : kExecPins) {
            if (pin.branch == branches) {
                nexts.push_back(node->*pin.target);
            }
        }

        for (const std::string& n : nexts) {
            if (n.empty() || depth.count(n)) {
                continue;
            }
            depth[n] = depth[cur] + 1;
            queue.push_back(n);
        }
    }

    // 開始ノードから辿り着けないノードは、右端にまとめて別の列として置く
    int maxDepth = -1;
    for (const auto& [id, d] : depth) {
        maxDepth = (std::max)(maxDepth, d);
    }

    std::map<int, std::vector<std::string>> columns;
    for (const auto& [id, node] : graph_.nodes) {
        auto it = depth.find(id);
        int col = (it != depth.end()) ? it->second : (maxDepth + 2);
        columns[col].push_back(id);
    }

    // 深さを列、同じ深さの中の順番を行として格子状に置く
    constexpr float kColSpacing = 260.0f;
    constexpr float kRowSpacing = 140.0f;
    for (auto& [col, ids] : columns) {
        float x = static_cast<float>(col) * kColSpacing;
        float y = 0.0f;
        for (const std::string& id : ids) {
            GraphNode& node = graph_.nodes[id];
            node.editorX = x;
            node.editorY = y;
            y += kRowSpacing;
        }
    }

    // 並べ終えたことを短く知らせる
    statusMessage_ = "Arranged";
    statusTimer_ = kStatusShortSeconds;
}

} // namespace engine::game

#endif // USE_IMGUI
