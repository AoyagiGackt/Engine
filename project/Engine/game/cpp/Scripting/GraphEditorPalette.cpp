/**
 * @file GraphEditorPalette.cpp
 * @brief グラフエディターのノード追加・コメント・変数パネルと、選択とドラッグの操作
 */
#ifdef USE_IMGUI
#include "GraphEditor.h"
#include "TextBufferCapacity.h"
#include "EditorUI.h"
#include "GraphEditorServices.h"
#include "GraphRuntime.h"
#include "Input.h"
#include "NodeRegistry.h"
#include "TimeManager.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <imgui.h>
#include <optional>
#include <utility>
#include <vector>

namespace engine::game {
namespace {
constexpr float kNodeSearchWidth = 200.0f;
constexpr ImVec2 kVariablesWindowPos = { 20.0f, 470.0f };
constexpr ImVec2 kVariablesWindowSize = { 300.0f, 220.0f };

// コメント枠
constexpr float kColorChannelMax = 255.0f;
constexpr int kCommentBorderAlpha = 255;
constexpr int kCommentFillAlpha = 45;
constexpr int kCommentHeaderAlpha = 90;
constexpr float kCommentBorderThickness = 2.0f;
constexpr float kCommentTextPadding = 4.0f;

} // namespace
// ══════════════════════════════════════════════════════
// キャンバスとノード描画
// ══════════════════════════════════════════════════════

/** @brief 指定した型のノードを、ノード定義の初期パラメータを入れて追加位置に1つ作る */
void GraphEditor::AddNodeOfType(const std::string& type) {
    std::string id = MakeUniqueNodeId();
    GraphNode node;
    node.id = id;
    node.type = type;
    node.editorX = pendingAddX_;
    node.editorY = pendingAddY_;

    // 初期パラメータはノード定義（NodeRegistryの表）から入れる（空だと編集の取っ掛かりが無いため）
    if (const NodeTypeSpec* spec = NodeRegistry::GetInstance()->FindSpec(type)) {
        for (const NodeParamSpec& param : spec->params) {
            node.params[param.key] = param.InitialValue();
        }
    }

    RecordUndoSnapshotNow();
    graph_.nodes[id] = std::move(node);
    if (graph_.startNodeId.empty()) {
        graph_.startNodeId = id;
    }
    SelectOnlyNode(id);
}

/** @brief ノードを追加するメニューを出す */
void GraphEditor::DrawAddNodeMenu() {
    if (ImGui::BeginPopup("AddNodePopup")) {
        // メニューを開いた直後は検索欄へフォーカスし、そのままタイプして絞り込めるようにする
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(kNodeSearchWidth);
        const bool enterPressed = ImGui::InputTextWithHint("##nodeSearch", "検索（Enterで先頭を追加）", nodeSearchBuf_, sizeof(nodeSearchBuf_), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::Separator();

        // メニュー項目を出し、マウスを乗せたら説明、押したらそのノードを追加する
        auto drawItem = [&](const std::string& type) {
            bool clicked = ImGui::MenuItem(type.c_str());
            if (ImGui::IsItemHovered()) {
                // 説明が登録されているノードだけツールチップを出す
                const NodeTypeSpec* spec = NodeRegistry::GetInstance()->FindSpec(type);
                if (spec && !spec->description.empty()) {
                    ImGui::SetTooltip("%s", spec->description.c_str());
                }
            }
            if (clicked) {
                AddNodeOfType(type);
            }
        };

        if (nodeSearchBuf_[0] != '\0') {
            // 検索中はカテゴリ分けをやめ、名前・ジャンル・説明のどれかに一致した物をフラットに並べる
            const std::string query = nodeSearchBuf_;
            namespace editorUI = graphics::editorUI;
            bool anyMatch = false;
            for (const std::string& type : NodeRegistry::GetInstance()->GetRegisteredTypes()) {
                // 大文字小文字を区別せずに比べる（説明の無いノードは名前だけで判定）
                const NodeTypeSpec* spec = NodeRegistry::GetInstance()->FindSpec(type);
                bool match = editorUI::ContainsIgnoreCase(type, query)
                    || (spec && (editorUI::ContainsIgnoreCase(spec->category, query) || editorUI::ContainsIgnoreCase(spec->description, query)));
                if (match) {
                    // 検索欄でEnterを押したら先頭の候補を追加して閉じる
                    if (enterPressed && !anyMatch) {
                        AddNodeOfType(type);
                        ImGui::CloseCurrentPopup();
                    }
                    drawItem(type);
                    anyMatch = true;
                }
            }
            // 1件も当たらなかったことを伝える
            if (!anyMatch) {
                ImGui::TextDisabled("該当なし");
            }
            // 検索中はカテゴリ一覧とコメント枠の項目を出さずに閉じる
            ImGui::EndPopup();
            return;
        }

        // カテゴリごとにグループ化する（よく使う分類は直接、他はサブメニューにまとめる）
        std::map<std::string, std::vector<std::string>> byCategory;
        for (const std::string& type : NodeRegistry::GetInstance()->GetRegisteredTypes()) {
            const NodeTypeSpec* spec = NodeRegistry::GetInstance()->FindSpec(type);
            std::string cat = (spec && !spec->category.empty()) ? spec->category : "その他";
            byCategory[cat].push_back(type);
        }

        // よく使う分類はサブメニューに入れず、先頭に直接並べる
        auto itFreq = byCategory.find("よく使う");
        if (itFreq != byCategory.end()) {
            for (const std::string& type : itFreq->second) {
                drawItem(type);
            }
            // 下のサブメニューに二重に出ないよう表から外す
            byCategory.erase(itFreq);
            ImGui::Separator();
        }

        // 残りの分類はサブメニューにする（map なので分類名の順に並ぶ）
        for (auto& [cat, types] : byCategory) {
            if (ImGui::BeginMenu(cat.c_str())) {
                for (const std::string& type : types) {
                    drawItem(type);
                }
                ImGui::EndMenu();
            }
        }

        ImGui::Separator();
        // 右クリックした位置にコメント枠を作って選択する
        if (ImGui::MenuItem("コメント枠")) {
            RecordUndoSnapshotNow();
            // 名前は通し番号で重ならないようにする
            std::string id = MakeUniqueCommentId();
            GraphComment c;
            c.id = id;
            c.x = pendingAddX_;
            c.y = pendingAddY_;
            graph_.comments[id] = std::move(c);
            // ノードの選択は外し、作ったコメント枠だけを選ぶ
            ClearSelection();
            selectedCommentId_ = id;
        }
        ImGui::EndPopup();
    }
}

/** @brief コメント（注釈用の色付き矩形）の描画とテキスト編集（移動とリサイズは UpdateCommentDrag） */
void GraphEditor::DrawComments(ImDrawList* dl) {
    // ノードより奥に重なるチャンネルへ描く
    dl->ChannelsSetCurrent(GraphEditorDrawingStyle::kCommentChannel);
    for (auto& [id, c] : graph_.comments) {
        // ボタンの名前がコメントごとに重ならないよう id で区別する
        ImGui::PushID(id.c_str());

        // グラフ上の座標を、スクロールと拡大率を掛けて画面の座標へ直す
        const CommentScreenRect rect = GetCommentScreenRect(c);
        const ImVec2 screenMin = rect.min;
        const ImVec2 screenMax = rect.max;

        // 枠と塗りはコメントの色を使い、透明度だけを変える（選択中は白い枠）
        auto commentColor = [&c](int alpha) {
            return IM_COL32(static_cast<int>(c.colorR * kColorChannelMax), static_cast<int>(c.colorG * kColorChannelMax),
                static_cast<int>(c.colorB * kColorChannelMax), alpha);
        };
        const ImU32 baseCol = commentColor(kCommentBorderAlpha);
        const ImU32 fillCol = commentColor(kCommentFillAlpha);
        constexpr ImU32 kSelectedBorderColor = IM_COL32(255, 255, 255, 255); // 選択中のコメントは白い枠
        const ImU32 borderCol = (id == selectedCommentId_) ? kSelectedBorderColor : baseCol;

        // 塗りを先に描き、枠をその上に重ねる
        dl->AddRectFilled(screenMin, screenMax, fillCol, GraphEditorDrawingStyle::kNodeCornerRadius * zoom_);
        dl->AddRect(screenMin, screenMax, borderCol, GraphEditorDrawingStyle::kNodeCornerRadius * zoom_, 0, kCommentBorderThickness * zoom_);

        // つかんで動かす帯だと分かるよう、ヘッダーを濃く塗る
        dl->AddRectFilled(screenMin, rect.headerMax, commentColor(kCommentHeaderAlpha), GraphEditorDrawingStyle::kNodeCornerRadius * zoom_);

        // テキスト編集欄（ヘッダーの下、ボックス幅いっぱい）
        ImGui::SetCursorScreenPos(ImVec2(screenMin.x + kCommentTextPadding, rect.headerMax.y));
        char buf[textBufferCapacity::kPath];
        strncpy_s(buf, c.text.c_str(), _TRUNCATE);
        ImGui::SetNextItemWidth(c.w * zoom_ - kCommentTextPadding * 2.0f);
        if (CaptureItemUndo(ImGui::InputText("##commentText", buf, sizeof(buf)))) {
            c.text = buf;
        }

        // つかめる場所だと分かるよう、リサイズの角を枠と同じ色で塗る
        dl->AddRectFilled(rect.handleMin, screenMax, borderCol);

        ImGui::PopID();
    }
}

/** @brief Run中のグラフ変数を一覧表示し、その場で上書きテストできるパネル */
void GraphEditor::DrawVariablesPanel() {
    if (!testRuntime_.IsRunning()) {
        return;
    }

    ImGui::SetNextWindowPos(kVariablesWindowPos, ImGuiCond_Once);
    ImGui::SetNextWindowSize(kVariablesWindowSize, ImGuiCond_FirstUseEver);
    ImGui::Begin("グラフ変数（実行中）");
    ImGui::TextDisabled("実行中の変数一覧値を直接書き換えて上書きテストできます");
    ImGui::Separator();

    testRuntime_.VisitVariables([this](const std::string& name, const GraphValue& value) {
        ImGui::PushID(name.c_str());
        GraphValueEditResult edit = DrawGraphValueInput(name.c_str(), value);
        if (edit.changed) {
            testRuntime_.SetVariable(name, std::move(edit.value));
        }
        ImGui::PopID();
    });
    ImGui::End();
}

// ══════════════════════════════════════════════════════
// 選択とドラッグ
// ══════════════════════════════════════════════════════

/** @brief 左クリックを押した場所に応じてノード移動・コメント操作・範囲選択を始める */
void GraphEditor::HandleCanvasPress(const CanvasFrameState& state) {
    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left) || !state.canvasHovered || state.nodePressedThisFrame) {
        return;
    }
    if (panning_ || linking_ || dataLinking_ || state.hoveredPin != nullptr || drag_.kind != CanvasDragKind::kNone) {
        return;
    }
    // 入力欄などの部品を押した時は操作を部品に任せ、そのノードを選ぶだけにする
    if (ImGui::IsAnyItemHovered()) {
        if (!state.hoveredNodeId.empty() && !IsNodeSelected(state.hoveredNodeId)) {
            SelectOnlyNode(state.hoveredNodeId);
        }
        return;
    }
    // ノードの余白を押したらタイトルと同じように掴む
    if (!state.hoveredNodeId.empty()) {
        BeginNodePress(state.hoveredNodeId);
        return;
    }
    if (TryBeginCommentDrag()) {
        return;
    }
    // 何もない所は範囲選択を始める（Ctrl/Shiftを押していなければ今の選択は外す）
    const ImGuiIO& io = ImGui::GetIO();
    if (!io.KeyCtrl && !io.KeyShift) {
        ClearSelection();
    }
    selectedCommentId_.clear();
    drag_ = CanvasDrag { };
    drag_.kind = CanvasDragKind::kBoxSelect;
    drag_.startMouseGraph = ScreenToGraph(io.MousePos);
    drag_.baseSelection = selectedNodeIds_;
}

/** @brief ノードを押した時の選択（Ctrl/Shiftで追加と解除）と移動の準備をする */
void GraphEditor::BeginNodePress(const std::string& id) {
    const ImGuiIO& io = ImGui::GetIO();
    const bool additive = io.KeyCtrl || io.KeyShift;
    selectedCommentId_.clear();
    if (additive && IsNodeSelected(id)) {
        // 追加選択中に選択済みのノードを押したら選択から外す
        selectedNodeIds_.erase(id);
        SyncSelection();
        return;
    }
    if (additive) {
        selectedNodeIds_.insert(id);
        selectedNodeId_ = id;
    } else if (!IsNodeSelected(id)) {
        SelectOnlyNode(id);
    }

    drag_ = CanvasDrag { };
    drag_.kind = CanvasDragKind::kNodes;
    drag_.startMouseGraph = ScreenToGraph(io.MousePos);
    drag_.pressedNodeId = additive ? std::string() : id;
    for (const std::string& selectedId : selectedNodeIds_) {
        if (const GraphNode* node = graph_.FindNode(selectedId)) {
            drag_.nodeStarts[selectedId] = ImVec2(node->editorX, node->editorY);
        }
    }
}

/** @brief コメント枠の画面上の矩形を求める */
GraphEditor::CommentScreenRect GraphEditor::GetCommentScreenRect(const GraphComment& comment) const {
    CommentScreenRect rect;
    rect.min = GraphToScreen({ comment.x, comment.y });
    rect.max = ImVec2(rect.min.x + comment.w * zoom_, rect.min.y + comment.h * zoom_);
    // 縮小しても掴める大きさを保つ
    const float headerHeight = (std::max)(GraphEditorDrawingStyle::kCommentHeaderHeight * zoom_, GraphEditorDrawingStyle::kMinCommentHeaderPixels);
    const float handleSize = (std::max)(GraphEditorDrawingStyle::kCommentHandleSize * zoom_, GraphEditorDrawingStyle::kMinCommentHandlePixels);
    rect.headerMax = ImVec2(rect.max.x, rect.min.y + headerHeight);
    rect.handleMin = ImVec2(rect.max.x - handleSize, rect.max.y - handleSize);
    return rect;
}

/** @brief コメントの掴みや帯を押していたら操作を始める */
bool GraphEditor::TryBeginCommentDrag() {
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    for (const auto& [id, comment] : graph_.comments) {
        const CommentScreenRect rect = GetCommentScreenRect(comment);
        const bool onHandle = GraphEditorDrawingStyle::RectContains(rect.handleMin, rect.max, mouse);
        if (!onHandle && !GraphEditorDrawingStyle::RectContains(rect.min, rect.headerMax, mouse)) {
            continue;
        }
        ClearSelection();
        selectedCommentId_ = id;
        drag_ = CanvasDrag { };
        drag_.kind = onHandle ? CanvasDragKind::kCommentResize : CanvasDragKind::kCommentMove;
        drag_.startMouseGraph = ScreenToGraph(mouse);
        drag_.commentId = id;
        drag_.commentStart = onHandle ? ImVec2(comment.w, comment.h) : ImVec2(comment.x, comment.y);
        return true;
    }
    return false;
}

/** @brief 押したまま続くノード移動をマウス位置に合わせて進める */
void GraphEditor::UpdateNodeDrag() {
    if (drag_.kind != CanvasDragKind::kNodes) {
        return;
    }
    const ImGuiIO& io = ImGui::GetIO();
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        // 動かさずに離したら、複数選択の中の1つを押した時はそのノードだけの選択に絞る
        if (drag_.moved) {
            CommitUndoCapture();
        } else if (!drag_.pressedNodeId.empty()) {
            SelectOnlyNode(drag_.pressedNodeId);
        }
        drag_ = CanvasDrag { };
        return;
    }
    // しきい値を超えるまでは動かさない（クリックだけで位置がずれないように）
    if (!drag_.moved) {
        if (!ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            return;
        }
        drag_.moved = true;
        BeginUndoCapture();
    }
    const ImVec2 mouse = ScreenToGraph(io.MousePos);
    const float dx = mouse.x - drag_.startMouseGraph.x;
    const float dy = mouse.y - drag_.startMouseGraph.y;
    // Ctrlを押している間は格子に合わせる
    const auto snap = [&io](float value) {
        return io.KeyCtrl ? std::round(value / GraphEditorDrawingStyle::kGridStep) * GraphEditorDrawingStyle::kGridStep : value;
    };
    for (const auto& [id, start] : drag_.nodeStarts) {
        const auto node = graph_.nodes.find(id);
        if (node != graph_.nodes.end()) {
            node->second.editorX = snap(start.x + dx);
            node->second.editorY = snap(start.y + dy);
        }
    }
    MarkUndoDirty();
}

/** @brief 押したまま続くコメントの移動とリサイズをマウス位置に合わせて進める */
void GraphEditor::UpdateCommentDrag() {
    if (drag_.kind != CanvasDragKind::kCommentMove && drag_.kind != CanvasDragKind::kCommentResize) {
        return;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (drag_.moved) {
            CommitUndoCapture();
        }
        drag_ = CanvasDrag { };
        return;
    }
    const auto found = graph_.comments.find(drag_.commentId);
    if (found == graph_.comments.end()) {
        drag_ = CanvasDrag { };
        return;
    }
    if (!drag_.moved) {
        if (!ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            return;
        }
        drag_.moved = true;
        BeginUndoCapture();
    }
    const ImVec2 mouse = ScreenToGraph(ImGui::GetIO().MousePos);
    const float dx = mouse.x - drag_.startMouseGraph.x;
    const float dy = mouse.y - drag_.startMouseGraph.y;
    GraphComment& comment = found->second;
    if (drag_.kind == CanvasDragKind::kCommentMove) {
        comment.x = drag_.commentStart.x + dx;
        comment.y = drag_.commentStart.y + dy;
    } else {
        comment.w = (std::max)(GraphEditorDrawingStyle::kCommentMinWidth, drag_.commentStart.x + dx);
        comment.h = (std::max)(GraphEditorDrawingStyle::kCommentMinHeight, drag_.commentStart.y + dy);
    }
    MarkUndoDirty();
}

/** @brief 押したまま続く範囲選択を進め、選択枠を描く */
void GraphEditor::UpdateBoxSelect(ImDrawList* dl) {
    if (drag_.kind != CanvasDragKind::kBoxSelect) {
        return;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        drag_ = CanvasDrag { };
        return;
    }
    if (!drag_.moved) {
        if (!ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            return;
        }
        drag_.moved = true;
    }
    // 開始点はグラフ座標で持っているので、途中でパンしても枠がずれない
    const ImVec2 start = GraphToScreen(drag_.startMouseGraph);
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const ImVec2 boxMin((std::min)(start.x, mouse.x), (std::min)(start.y, mouse.y));
    const ImVec2 boxMax((std::max)(start.x, mouse.x), (std::max)(start.y, mouse.y));
    dl->AddRectFilled(boxMin, boxMax, GraphEditorDrawingStyle::kBoxSelectFillColor);
    dl->AddRect(boxMin, boxMax, GraphEditorDrawingStyle::kBoxSelectBorderColor);

    // 枠に少しでも重なるノードを選択する
    selectedNodeIds_ = drag_.baseSelection;
    for (const auto& [id, rect] : nodeRects_) {
        if (rect.first.x <= boxMax.x && rect.second.x >= boxMin.x && rect.first.y <= boxMax.y && rect.second.y >= boxMin.y) {
            selectedNodeIds_.insert(id);
        }
    }
    SyncSelection();
}

/** @brief カーソルの下の掴める物に合わせてカーソルの形を変える */
void GraphEditor::UpdateHoverCursor(const CanvasFrameState& state) {
    // 操作中はその操作の形を保つ
    if (drag_.moved) {
        ImGui::SetMouseCursor(drag_.kind == CanvasDragKind::kCommentResize ? ImGuiMouseCursor_ResizeNWSE : ImGuiMouseCursor_ResizeAll);
        return;
    }
    if (panning_ || !state.canvasHovered || drag_.kind != CanvasDragKind::kNone) {
        return;
    }
    if (state.hoveredPin != nullptr || state.snapPin != nullptr || state.hoveredLink) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        return;
    }
    if (ImGui::IsAnyItemHovered()) {
        return;
    }
    if (!state.hoveredNodeId.empty()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        return;
    }
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    for (const auto& [id, comment] : graph_.comments) {
        const CommentScreenRect rect = GetCommentScreenRect(comment);
        if (GraphEditorDrawingStyle::RectContains(rect.handleMin, rect.max, mouse)) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
            return;
        }
        if (GraphEditorDrawingStyle::RectContains(rect.min, rect.headerMax, mouse)) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
            return;
        }
    }
}

/** @brief 右クリックメニュー・Delete・全体表示などの操作を処理する */
void GraphEditor::HandleCanvasShortcuts(const CanvasFrameState& state) {
    // 右クリックはカーソルの下の物で分ける（ピンの配線解除は UpdatePinInteraction で処理済み）
    if (state.canvasHovered && state.hoveredPin == nullptr && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        if (state.hoveredLink) {
            DisconnectPin(*state.hoveredLink);
        } else if (!state.hoveredNodeId.empty()) {
            if (!IsNodeSelected(state.hoveredNodeId)) {
                SelectOnlyNode(state.hoveredNodeId);
            }
            contextNodeId_ = state.hoveredNodeId;
            ImGui::OpenPopup("NodeContextMenu");
        } else {
            // クリック位置をグラフ座標へ変換して、追加するノードの置き場所にする
            const ImVec2 graphPos = ScreenToGraph(ImGui::GetIO().MousePos);
            pendingAddX_ = graphPos.x;
            pendingAddY_ = graphPos.y;
            nodeSearchBuf_[0] = '\0'; // 前回の検索文字列を持ち越さない
            ImGui::OpenPopup("AddNodePopup");
        }
    }
    DrawAddNodeMenu();
    DrawNodeContextMenu();

    // 文字入力中とメニュー表示中は単独キーを奪わない
    if (ImGui::GetIO().WantTextInput || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup)) {
        return;
    }
    // Deleteキーで選択中のノード／コメントを削除
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
        if (!selectedNodeIds_.empty()) {
            DeleteSelectedNodes();
        } else if (!selectedCommentId_.empty()) {
            DeleteComment(selectedCommentId_);
            selectedCommentId_.clear();
        }
    }
    // Fキーで選択中のノード（無ければ全体）が収まるように表示を合わせる
    if (ImGui::IsKeyPressed(ImGuiKey_F, false) && !ImGui::GetIO().KeyCtrl) {
        FrameNodes(!selectedNodeIds_.empty());
    }
}

/** @brief 選択中のノードを対象にする右クリックメニューを描く */
void GraphEditor::DrawNodeContextMenu() {
    if (!ImGui::BeginPopup("NodeContextMenu")) {
        return;
    }
    if (ImGui::MenuItem("開始ノードに設定")) {
        RecordUndoSnapshotNow();
        graph_.startNodeId = contextNodeId_;
    }
    ImGui::Separator();
    if (ImGui::MenuItem("複製", "Ctrl+D")) {
        DuplicateSelectedNodes();
    }
    if (ImGui::MenuItem("削除", "Delete")) {
        DeleteSelectedNodes();
    }
    ImGui::EndPopup();
}

} // namespace engine::game
#endif
