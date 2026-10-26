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
    graphIO::Save(graphPath_, graph_);
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
    // 古いグラフの選択を解除し、保存が必要な状態へ切り替える
    graph_ = std::move(*snapshot);
    selectedNodeId_.clear();
    selectedCommentId_.clear();
    dirty_ = true;
    statusMessage_ = message;
    statusTimer_ = kStatusBriefSeconds;
}

/** @brief 1つ前に記録したグラフの状態へ戻す */
void GraphEditor::Undo() {
    ApplyHistorySnapshot(history_.Undo(graph_), "元に戻しました");
}

/** @brief Undo で戻した状態をやり直す */
void GraphEditor::Redo() {
    ApplyHistorySnapshot(history_.Redo(graph_), "やり直しました");
}

// ══════════════════════════════════════════════════════
// ノードとコメントの編集
// ══════════════════════════════════════════════════════

/** @brief 指定ノードを削除し、開始ノードや他ノードからの参照も外す */
void GraphEditor::DeleteNode(const std::string& id) {
    RecordUndoSnapshotNow();
    graph_.nodes.erase(id);
    if (graph_.startNodeId == id) {
        graph_.startNodeId.clear();
    }
    for (auto& [otherId, node] : graph_.nodes) {
        for (const ExecPinDef& pin : kExecPins) {
            if (node.*pin.target == id) {
                (node.*pin.target).clear();
            }
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

/** @brief idのノードを丸ごと複製する（新しいidを振り、少しずらした位置に置く） */
void GraphEditor::DuplicateNode(const std::string& id) {
    const GraphNode* src = graph_.FindNode(id);
    if (!src) {
        return;
    }

    RecordUndoSnapshotNow();
    std::string newId = "node_" + std::to_string(nextNodeSerial_++);
    GraphNode copy = *src; // params/paramLinks/next系も含めてまるごと複製する
    copy.id = newId;
    copy.editorX = src->editorX + kDuplicateOffset; // 元と重ならないよう少しずらして置く
    copy.editorY = src->editorY + kDuplicateOffset;

    graph_.nodes[newId] = std::move(copy);
    selectedNodeId_ = newId;
    selectedCommentId_.clear();
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
