/**
 * @file GraphEditor.cpp
 * @brief GraphEditorのイベントグラフのデータ、編集、実行に関する具体的な処理を実装するファイル
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
using namespace engine::game;
using namespace engine;


// ══════════════════════════════════════════════════════
// ファイル操作とエディタ更新
// ══════════════════════════════════════════════════════

GraphEditor* GraphEditor::GetInstance()
{
    static GraphEditor instance;
    return &instance;
}

void GraphEditor::Open(const std::string& path)
{
    graphPath_ = path;
    graph_ = GraphIO::Load(path);
    selectedNodeId_.clear();
    linking_ = false;

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

    statusMessage_ = "Opened: " + path;
    statusTimer_ = 2.0f;
}

void GraphEditor::Save()
{
    GraphIO::Save(graphPath_, graph_);
    dirty_ = false;
    statusMessage_ = "Saved: " + graphPath_;
    statusTimer_ = 2.0f;
}

void GraphEditor::Update(Input* input)
{
    GraphEditorInteraction::Update(*this, input);
}

// ══════════════════════════════════════════════════════
// 編集履歴
// ══════════════════════════════════════════════════════

void GraphEditor::RecordUndoSnapshotNow()
{
    history_.Record(graph_);
    dirty_ = true;
}

void GraphEditor::BeginUndoCapture()
{
    if (!history_.IsCapturing()) {
        history_.Begin(graph_);
    }
}

void GraphEditor::MarkUndoDirty()
{
    history_.MarkChanged();
    dirty_ = true;
}

void GraphEditor::CommitUndoCapture()
{
    history_.Commit();
}

void GraphEditor::Undo()
{
    auto snapshot = history_.Undo(graph_);
    if (!snapshot) {
        return;
    }
    graph_ = std::move(*snapshot);
    selectedNodeId_.clear();
    selectedCommentId_.clear();
    dirty_ = true;
    statusMessage_ = "元に戻しました";
    statusTimer_ = 1.5f;
}

void GraphEditor::Redo()
{
    auto snapshot = history_.Redo(graph_);
    if (!snapshot) {
        return;
    }
    graph_ = std::move(*snapshot);
    selectedNodeId_.clear();
    selectedCommentId_.clear();
    dirty_ = true;
    statusMessage_ = "やり直しました";
    statusTimer_ = 1.5f;
}

// ══════════════════════════════════════════════════════
// ノードとコメントの編集
// ══════════════════════════════════════════════════════

void GraphEditor::DeleteNode(const std::string& id)
{
    RecordUndoSnapshotNow();
    graph_.nodes.erase(id);
    if (graph_.startNodeId == id) {
        graph_.startNodeId.clear();
    }
    for (auto& [otherId, node] : graph_.nodes) {
        if (node.next == id) {
            node.next.clear();
        }
        if (node.nextTrue == id) {
            node.nextTrue.clear();
        }
        if (node.nextFalse == id) {
            node.nextFalse.clear();
        }
        // 削除したノードを供給元にしているデータ配線も切る
        for (auto it = node.paramLinks.begin(); it != node.paramLinks.end();) {
            if (it->second == id) {
                it = node.paramLinks.erase(it);
            } else {
                ++it;
            }
        }
    }
}

void GraphEditor::DuplicateNode(const std::string& id)
{
    const GraphNode* src = graph_.FindNode(id);
    if (!src) {
        return;
    }

    RecordUndoSnapshotNow();
    std::string newId = "node_" + std::to_string(nextNodeSerial_++);
    GraphNode copy = *src; // params/paramLinks/next系も含めてまるごと複製する
    copy.id = newId;
    copy.editorX = src->editorX + 30.0f; // 元と重ならないよう少しずらして置く
    copy.editorY = src->editorY + 30.0f;

    graph_.nodes[newId] = std::move(copy);
    selectedNodeId_ = newId;
    selectedCommentId_.clear();
}

void GraphEditor::DeleteComment(const std::string& id)
{
    RecordUndoSnapshotNow();
    graph_.comments.erase(id);
}

void GraphEditor::ArrangeNodes()
{
    RecordUndoSnapshotNow();
    // 開始ノードからのBFSで各ノードの深さ（開始から何手先か）を求め、深さごとに列を作って並べる
    std::map<std::string, int> depth;
    std::vector<std::string> queue;
    size_t queueHead = 0;

    if (!graph_.startNodeId.empty() && graph_.nodes.count(graph_.startNodeId)) {
        depth[graph_.startNodeId] = 0;
        queue.push_back(graph_.startNodeId);
    }

    while (queueHead < queue.size()) {
        const std::string cur = queue[queueHead++];
        const GraphNode* node = graph_.FindNode(cur);
        if (!node) {
            continue;
        }

        std::vector<std::string> nexts;
        if (node->type == "If") {
            nexts = { node->nextTrue, node->nextFalse };
        } else {
            nexts = { node->next };
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

    statusMessage_ = "Arranged";
    statusTimer_ = 2.0f;
}

#endif // USE_IMGUI
