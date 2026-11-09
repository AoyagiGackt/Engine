/**
 * @file GraphEditorDraw.cpp
 * @brief グラフのキャンバス・ノード値入力・リンク描画と編集操作を実装するファイル
 * @note GraphEditor のキャンバスの描画と編集操作だけを GraphEditor.cpp から分けて置いている（クラスは GraphEditor のまま）
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
#include <cfloat>
#include <cmath>
#include <cstring>
#include <imgui.h>
#include <optional>
#include <iterator>
#include <utility>
#include <vector>

namespace engine::game {
namespace {
constexpr float kBaseLinkCtrl = 60.0f; // ベジエ曲線の制御点オフセット
constexpr ImU32 kColLink = IM_COL32(220, 220, 220, 200);

// 同じ型同士、またはどちらかがAny（白）なら接続できる
bool TypesCompatible(GraphValueType a, GraphValueType b) {
    return a == b || a == GraphValueType::kAny || b == GraphValueType::kAny;
}

// ノード種別のスペックから、指定パラメータの型を引く（スペック未登録ならAny扱い）
GraphValueType ParamTypeOf(const std::string& nodeType, const std::string& key) {
    const NodeTypeSpec* spec = NodeRegistry::GetInstance()->FindSpec(nodeType);
    if (spec) {
        for (const auto& p : spec->params) {
            if (p.key == key) {
                return p.type;
            }
        }
    }
    return GraphValueType::kAny;
}

/** @brief 種類・ノード・名前が一致するピンを探す（無ければ nullptr） */
const GraphCanvasPin* FindPin(const std::vector<GraphCanvasPin>& pins, GraphPinKind kind, const std::string& nodeId, const std::string& name) {
    for (const GraphCanvasPin& pin : pins) {
        if (pin.kind == kind && pin.nodeId == nodeId && pin.name == name) {
            return &pin;
        }
    }
    return nullptr;
}

/** @brief 2点間の距離を返す */
float Distance(const ImVec2& a, const ImVec2& b) {
    return std::hypot(a.x - b.x, a.y - b.y);
}

/**
 * @brief 条件に合うピンのうち、カーソルに最も近いものを返す
 * @param minRadius 掴める距離の下限（見た目の半径＋余白がこれより大きければそちらを使う）
 */
template <class Filter>
const GraphCanvasPin* FindNearestPin(const std::vector<GraphCanvasPin>& pins, const ImVec2& mouse, float minRadius, Filter filter) {
    const GraphCanvasPin* nearest = nullptr;
    float nearestDistance = FLT_MAX;
    for (const GraphCanvasPin& pin : pins) {
        const float reach = (std::max)(pin.radius + GraphEditorDrawingStyle::kPinHoverPadding, minRadius);
        const float distance = Distance(pin.position, mouse);
        if (distance <= reach && distance < nearestDistance && filter(pin)) {
            nearest = &pin;
            nearestDistance = distance;
        }
    }
    return nearest;
}

/** @brief 点と線分の距離を返す */
float DistanceToSegment(const ImVec2& a, const ImVec2& b, const ImVec2& point) {
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float lengthSq = dx * dx + dy * dy;
    const float t = lengthSq > 0.0f ? std::clamp(((point.x - a.x) * dx + (point.y - a.y) * dy) / lengthSq, 0.0f, 1.0f) : 0.0f;
    return Distance({ a.x + dx * t, a.y + dy * t }, point);
}

/** @brief ベジエ曲線を折れ線に分けて、点との最短距離を返す */
float DistanceToBezier(const ImVec2& p0, const ImVec2& p1, const ImVec2& p2, const ImVec2& p3, const ImVec2& point) {
    float nearest = FLT_MAX;
    ImVec2 previous = p0;
    for (int i = 1; i <= GraphEditorDrawingStyle::kWireHoverSamples; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(GraphEditorDrawingStyle::kWireHoverSamples);
        const float u = 1.0f - t;
        const float w0 = u * u * u;
        const float w1 = 3.0f * u * u * t;
        const float w2 = 3.0f * u * t * t;
        const float w3 = t * t * t;
        const ImVec2 current(w0 * p0.x + w1 * p1.x + w2 * p2.x + w3 * p3.x, w0 * p0.y + w1 * p1.y + w2 * p2.y + w3 * p3.y);
        nearest = (std::min)(nearest, DistanceToSegment(previous, current, point));
        previous = current;
    }
    return nearest;
}

/** @brief 実行出力ピンの配線を外す */
bool ClearExecOutLink(GraphDesc& graph, const GraphCanvasPin& pin) {
    const auto node = graph.nodes.find(pin.nodeId);
    if (node == graph.nodes.end()) {
        return false;
    }
    bool changed = false;
    for (const ExecPinDef& def : kExecPins) {
        if (pin.name == def.name && !(node->second.*def.target).empty()) {
            (node->second.*def.target).clear();
            changed = true;
        }
    }
    return changed;
}

/** @brief データ入力ピンの配線を外す */
bool ClearDataInLink(GraphDesc& graph, const GraphCanvasPin& pin) {
    const auto node = graph.nodes.find(pin.nodeId);
    return node != graph.nodes.end() && node->second.paramLinks.erase(pin.name) > 0;
}

/** @brief ピンの配線を外す処理（GraphPinKind の並びと同じ） */
using PinDisconnector = bool (*)(GraphDesc&, const GraphCanvasPin&);
constexpr PinDisconnector kPinDisconnectors[] = {
    [](GraphDesc& graph, const GraphCanvasPin& pin) {
        return ClearExecLinksTo(graph, pin.nodeId);
    },
    ClearExecOutLink,
    ClearDataInLink,
    [](GraphDesc& graph, const GraphCanvasPin& pin) {
        return ClearDataLinksFrom(graph, pin.nodeId);
    },
};

} // namespace
// ══════════════════════════════════════════════════════
// キャンバスとノード描画
// ══════════════════════════════════════════════════════

/** @brief グラフのキャンバス（視点操作・ノード・配線・プレビュー線）を描画する */
void GraphEditor::DrawCanvas() {
    const float canvasWidth = showHelpPanel_ ? -GraphEditorDrawingStyle::kHelpPanelWidth : 0.0f;
    ImGui::BeginChild("GraphCanvas", ImVec2(canvasWidth, 0), true,
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar);

    canvasOrigin_ = ImGui::GetCursorScreenPos();
    canvasSize_ = ImGui::GetContentRegionAvail();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // ドラッグ中や入力欄の編集中もキャンバスの上にいれば操作を受けられるようにする
    const bool canvasHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

    if (requestFrameAll_) {
        requestFrameAll_ = false;
        FrameNodes(false);
    }
    UpdateCanvasView(canvasHovered);
    DrawGrid(dl);

    dl->ChannelsSplit(GraphEditorDrawingStyle::kChannelCount);

    CanvasFrameState state;
    state.canvasHovered = canvasHovered;
    if (testRuntime_.IsRunning()) {
        // サブグラフ実行中に子グラフを開いて見ている場合でも正しく追従させるため、
        // 実行チェーンの最深部（今実際に動いているグラフ）とパスで突き合わせる
        std::string leafPath;
        const GraphRuntime& leaf = testRuntime_.GetActiveLeaf(leafPath);
        const std::string& effectivePath = leafPath.empty() ? testRuntimeRootPath_ : leafPath;
        state.viewingActiveGraph = (effectivePath == graphPath_);
        state.activeRunNodeId = leaf.GetCurrentNodeId();
    }
    nodeRects_.clear();
    for (auto& [id, node] : graph_.nodes) {
        DrawNode(dl, canvasOrigin_, id, node, state);
    }

    // コメントの入力欄はノードの後に出してノードの操作を優先し、描画はチャンネルで最背面に置く
    DrawComments(dl);

    dl->ChannelsMerge();

    // ここから先はノード走査の外なので、ノードの追加・削除をその場で行える
    UpdateHoveredNode(state);
    UpdatePinInteraction(dl, state);

    // リンク線は全ノードのピンが出そろった後にまとめて描く
    DrawLinks(dl, state);
    DrawDataLinks(dl, state);
    if (state.hoveredLink) {
        const float ctrl = kBaseLinkCtrl * zoom_;
        dl->AddBezierCubic(state.hoveredLinkFrom, ImVec2(state.hoveredLinkFrom.x + ctrl, state.hoveredLinkFrom.y),
            ImVec2(state.hoveredLinkTo.x - ctrl, state.hoveredLinkTo.y), state.hoveredLinkTo,
            GraphEditorDrawingStyle::kWireHoverColor, GraphEditorDrawingStyle::kWireHoverThickness);
    }
    DrawLinkPreviews(dl, state);

    HandleCanvasPress(state);
    UpdateNodeDrag();
    UpdateCommentDrag();
    UpdateBoxSelect(dl);
    UpdateHoverCursor(state);
    HandleCanvasShortcuts(state);

    ImGui::SetWindowFontScale(1.0f); // 他のImGuiウィジェット（ツールバー等）に影響しないよう戻す
    ImGui::EndChild();
}

/** @brief 画面座標をグラフ座標へ変換する */
ImVec2 GraphEditor::ScreenToGraph(const ImVec2& screen) const {
    return ImVec2((screen.x - canvasOrigin_.x) / zoom_ - panOffsetX_, (screen.y - canvasOrigin_.y) / zoom_ - panOffsetY_);
}

/** @brief グラフ座標を画面座標へ変換する */
ImVec2 GraphEditor::GraphToScreen(const ImVec2& graph) const {
    return ImVec2(canvasOrigin_.x + (panOffsetX_ + graph.x) * zoom_, canvasOrigin_.y + (panOffsetY_ + graph.y) * zoom_);
}

/** @brief ホイールズーム（カーソル位置が中心）・中ドラッグとスペース+左ドラッグのパン・端での自動スクロールを処理する */
void GraphEditor::UpdateCanvasView(bool canvasHovered) {
    const ImGuiIO& io = ImGui::GetIO();
    // カーソルの下のグラフ座標が動かないようにパンを合わせ直す
    if (canvasHovered && io.MouseWheel != 0.0f) {
        const ImVec2 anchor = ScreenToGraph(io.MousePos);
        const float scaled = zoom_ * std::pow(1.0f + GraphEditorDrawingStyle::kZoomStep, io.MouseWheel);
        zoom_ = std::clamp(scaled, GraphEditorDrawingStyle::kMinZoom, GraphEditorDrawingStyle::kMaxZoom);
        panOffsetX_ = (io.MousePos.x - canvasOrigin_.x) / zoom_ - anchor.x;
        panOffsetY_ = (io.MousePos.y - canvasOrigin_.y) / zoom_ - anchor.y;
    }
    ImGui::SetWindowFontScale(zoom_);

    // キャンバスの上で押したら、カーソルが外へ出ても離すまでパンを続ける
    if (canvasHovered && !panning_) {
        const bool spaceHeld = !io.WantTextInput && ImGui::IsKeyDown(ImGuiKey_Space);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
            panning_ = true;
            panButton_ = ImGuiMouseButton_Middle;
        } else if (spaceHeld && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            panning_ = true;
            panButton_ = ImGuiMouseButton_Left;
        }
    }
    if (panning_) {
        if (ImGui::IsMouseDown(panButton_)) {
            // パンはグラフ座標系で持つので、画面の移動量をズームで割って変換する
            panOffsetX_ += io.MouseDelta.x / zoom_;
            panOffsetY_ += io.MouseDelta.y / zoom_;
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        } else {
            panning_ = false;
        }
    }
    UpdateEdgeAutoPan();
}

/** @brief 配線中とドラッグ中にカーソルがキャンバスの端へ寄ったら表示を流す */
void GraphEditor::UpdateEdgeAutoPan() {
    if (!linking_ && !dataLinking_ && !drag_.moved) {
        return;
    }
    const ImGuiIO& io = ImGui::GetIO();
    // 端に近いほど速く流す（中身をカーソルと反対へ動かす）
    const auto edgeSpeed = [](float position, float minEdge, float maxEdge) {
        const float margin = GraphEditorDrawingStyle::kEdgePanMargin;
        if (position < minEdge + margin) {
            return (std::min)((minEdge + margin - position) / margin, 1.0f) * GraphEditorDrawingStyle::kEdgePanSpeed;
        }
        if (position > maxEdge - margin) {
            return -(std::min)((position - (maxEdge - margin)) / margin, 1.0f) * GraphEditorDrawingStyle::kEdgePanSpeed;
        }
        return 0.0f;
    };
    panOffsetX_ += edgeSpeed(io.MousePos.x, canvasOrigin_.x, canvasOrigin_.x + canvasSize_.x) * io.DeltaTime / zoom_;
    panOffsetY_ += edgeSpeed(io.MousePos.y, canvasOrigin_.y, canvasOrigin_.y + canvasSize_.y) * io.DeltaTime / zoom_;
}

/** @brief 背景の格子を描く（パンとズームの量を見て分かるようにする） */
void GraphEditor::DrawGrid(ImDrawList* dl) {
    // 縮小して線が詰まる時は間引く
    float step = GraphEditorDrawingStyle::kGridStep;
    while (step * zoom_ < GraphEditorDrawingStyle::kMinGridPixels) {
        step *= 2.0f;
    }
    const ImVec2 topLeft = ScreenToGraph(canvasOrigin_);
    const ImVec2 canvasEnd(canvasOrigin_.x + canvasSize_.x, canvasOrigin_.y + canvasSize_.y);
    for (float x = std::floor(topLeft.x / step) * step; GraphToScreen({ x, 0.0f }).x < canvasEnd.x; x += step) {
        const float screenX = GraphToScreen({ x, 0.0f }).x;
        dl->AddLine({ screenX, canvasOrigin_.y }, { screenX, canvasEnd.y }, GraphEditorDrawingStyle::kGridColor);
    }
    for (float y = std::floor(topLeft.y / step) * step; GraphToScreen({ 0.0f, y }).y < canvasEnd.y; y += step) {
        const float screenY = GraphToScreen({ 0.0f, y }).y;
        dl->AddLine({ canvasOrigin_.x, screenY }, { canvasEnd.x, screenY }, GraphEditorDrawingStyle::kGridColor);
    }
}

/** @brief 全ノード（selectedOnly なら選択中のノードだけ）が収まるように表示を合わせる */
void GraphEditor::FrameNodes(bool selectedOnly) {
    ImVec2 boundsMin(FLT_MAX, FLT_MAX);
    ImVec2 boundsMax(-FLT_MAX, -FLT_MAX);
    const auto extend = [&](float x, float y, float width, float height) {
        boundsMin = ImVec2((std::min)(boundsMin.x, x), (std::min)(boundsMin.y, y));
        boundsMax = ImVec2((std::max)(boundsMax.x, x + width), (std::max)(boundsMax.y, y + height));
    };
    for (const auto& [id, node] : graph_.nodes) {
        if (selectedOnly && !IsNodeSelected(id)) {
            continue;
        }
        // 描いたことのあるノードは実際の大きさ、まだなら目安の大きさで見積もる
        const auto rect = nodeRects_.find(id);
        const bool measured = rect != nodeRects_.end();
        const float width = measured ? (rect->second.second.x - rect->second.first.x) / zoom_ : GraphEditorDrawingStyle::kBaseNodeWidth;
        const float height = measured ? (rect->second.second.y - rect->second.first.y) / zoom_ : GraphEditorDrawingStyle::kFallbackNodeHeight;
        extend(node.editorX, node.editorY, width, height);
    }
    if (!selectedOnly) {
        for (const auto& [id, comment] : graph_.comments) {
            extend(comment.x, comment.y, comment.w, comment.h);
        }
    }
    // 何も無ければ今の表示のままにする
    if (boundsMin.x > boundsMax.x || canvasSize_.x <= 0.0f || canvasSize_.y <= 0.0f) {
        return;
    }
    const float margin = GraphEditorDrawingStyle::kFrameMargin;
    const float fitWidth = boundsMax.x - boundsMin.x + margin * 2.0f;
    const float fitHeight = boundsMax.y - boundsMin.y + margin * 2.0f;
    const float fitZoom = (std::min)(canvasSize_.x / fitWidth, canvasSize_.y / fitHeight);
    zoom_ = std::clamp(fitZoom, GraphEditorDrawingStyle::kMinZoom, GraphEditorDrawingStyle::kMaxFrameZoom);
    // 範囲の中心がキャンバスの中心に来るようにする
    panOffsetX_ = canvasSize_.x * 0.5f / zoom_ - (boundsMin.x + boundsMax.x) * 0.5f;
    panOffsetY_ = canvasSize_.y * 0.5f / zoom_ - (boundsMin.y + boundsMax.y) * 0.5f;
}

/** @brief ノード1つ分（タイトル・パラメータ・ピン・枠）を描画し、ドラッグ操作を処理する */
void GraphEditor::DrawNode(ImDrawList* dl, const ImVec2& origin, const std::string& id, GraphNode& node, CanvasFrameState& state) {
    GraphNodeRenderer::Draw(*this, dl, origin, id, node, &state);
}

/** @brief ノードのパラメータ編集ウィジェットと左端のデータ入力ピンを描画する */
void GraphEditor::DrawNodeParams(ImDrawList* dl, const std::string& id, GraphNode& node, const ImVec2& nodeScreenPos, CanvasFrameState& state) {
    const float nodeW = GraphEditorDrawingStyle::kBaseNodeWidth * zoom_;
    const float pinR = GraphEditorDrawingStyle::kBasePinRadius * zoom_;
    const float pinPad = GraphEditorDrawingStyle::kBasePinPad * zoom_;

    // パラメータ編集（型に応じてウィジェットを出し分ける）
    // 各行の左端にデータ入力ピンを置くため、行の矩形を記録する
    for (auto& [key, val] : node.params) {
        ImGui::PushID(key.c_str());
        bool isLinked = node.paramLinks.count(key) > 0;
        // 表示は日本語名にする（ImGui の識別子は PushID の key で区別しているので表示名が重なってもよい）
        const std::string paramLabel = GraphNodeLabels::ParamName(node.type, key);
        if (isLinked) {
            // データ配線されているパラメータはリテラル編集を出さず、接続元を表示する
            ImGui::TextDisabled("%s ← %s", paramLabel.c_str(), node.paramLinks[key].c_str());
        } else {
            ImGui::SetNextItemWidth(nodeW);
            GraphValueEditResult edit = DrawGraphValueInput(paramLabel.c_str(), val);
            // チェック操作は1回、テキストと数値は入力開始から終了までを1回のUndoにする
            if (!edit.discrete && edit.activated) {
                BeginUndoCapture();
            }
            if (edit.changed) {
                if (edit.discrete) {
                    RecordUndoSnapshotNow();
                } else {
                    MarkUndoDirty();
                }
                val = std::move(edit.value);
            }
            if (!edit.discrete && edit.deactivated) {
                CommitUndoCapture();
            }
        }

        // データ入力ピン（この行の左端）。配線の操作は全ノードを描いた後に UpdatePinInteraction で行う
        float rowCenterY = (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5f;
        ImVec2 pinPos(nodeScreenPos.x - pinPad, rowCenterY);

        GraphValueType paramType = ParamTypeOf(node.type, key);
        float dataPinR = pinR * GraphEditorDrawingStyle::kDataPinRadiusRatio;
        dl->AddCircleFilled(pinPos, dataPinR, GraphEditorDrawingStyle::ColorForType(paramType));
        state.pins.push_back({ GraphPinKind::kDataIn, id, key, pinPos, dataPinR, paramType });
        ImGui::PopID();
    }
}

// ══════════════════════════════════════════════════════
// ピンと配線の操作
// ══════════════════════════════════════════════════════

/** @brief カーソルの下のノードを求める */
void GraphEditor::UpdateHoveredNode(CanvasFrameState& state) {
    if (!state.canvasHovered) {
        return;
    }
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const float boxPad = GraphEditorDrawingStyle::kNodeBoxPadding * zoom_;
    for (const auto& [id, rect] : nodeRects_) {
        const ImVec2 frameMin(rect.first.x - boxPad, rect.first.y - boxPad * GraphEditorDrawingStyle::kNodeTopPaddingRatio);
        const ImVec2 frameMax(rect.second.x + boxPad, rect.second.y + boxPad);
        if (GraphEditorDrawingStyle::RectContains(frameMin, frameMax, mouse)) {
            state.hoveredNodeId = id;
            return;
        }
    }
}

/** @brief 今の配線ドラッグをこのピンへつなげられるか */
bool GraphEditor::CanConnectLinkTo(const GraphCanvasPin& pin) const {
    if (linking_) {
        return pin.kind == GraphPinKind::kExecIn && pin.nodeId != linkFromNodeId_;
    }
    return dataLinking_ && pin.kind == GraphPinKind::kDataIn && pin.nodeId != dataLinkFromNodeId_
        && TypesCompatible(dataLinkFromType_, pin.type);
}

/** @brief 配線ドラッグをピンへつなぐ */
void GraphEditor::ConnectLinkTo(const GraphCanvasPin& pin) {
    RecordUndoSnapshotNow();
    if (linking_) {
        const auto from = graph_.nodes.find(linkFromNodeId_);
        if (from == graph_.nodes.end()) {
            return;
        }
        for (const ExecPinDef& def : kExecPins) {
            if (linkFromPin_ == def.name) {
                from->second.*def.target = pin.nodeId;
            }
        }
        return;
    }
    const auto to = graph_.nodes.find(pin.nodeId);
    if (to != graph_.nodes.end()) {
        to->second.paramLinks[pin.name] = dataLinkFromNodeId_;
    }
}

/** @brief つなげない場所で離した時に、ピンの近くなら理由を表示する */
void GraphEditor::ReportRejectedDrop(const CanvasFrameState& state) {
    if (!dataLinking_ || !state.canvasHovered) {
        return;
    }
    const GraphCanvasPin* nearby = FindNearestPin(state.pins, ImGui::GetIO().MousePos, GraphEditorDrawingStyle::kLinkSnapRadius, [](const GraphCanvasPin& pin) {
        return pin.kind == GraphPinKind::kDataIn;
    });
    if (nearby == nullptr) {
        return;
    }
    statusMessage_ = nearby->nodeId == dataLinkFromNodeId_
        ? "自分自身のピンへは接続できません"
        : "型が違うため接続できません。同じ色のピン同士をつないでください";
    statusTimer_ = kStatusNormalSeconds;
}

/** @brief ピンのホバー・配線の開始と吸着と接続・右クリックの配線解除を処理する */
void GraphEditor::UpdatePinInteraction(ImDrawList* dl, CanvasFrameState& state) {
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    if (state.canvasHovered) {
        state.hoveredPin = FindNearestPin(state.pins, mouse, GraphEditorDrawingStyle::kMinPinGrabRadius, [](const GraphCanvasPin&) {
            return true;
        });
    }

    if (linking_ || dataLinking_) {
        // カーソルの近くでつなげるピンへ吸着する（実行の線はノードのどこに落としても入力ピンへつなぐ）
        if (state.canvasHovered) {
            state.snapPin = FindNearestPin(state.pins, mouse, GraphEditorDrawingStyle::kLinkSnapRadius, [this](const GraphCanvasPin& pin) {
                return CanConnectLinkTo(pin);
            });
            if (state.snapPin == nullptr && linking_ && !state.hoveredNodeId.empty() && state.hoveredNodeId != linkFromNodeId_) {
                state.snapPin = FindPin(state.pins, GraphPinKind::kExecIn, state.hoveredNodeId, GraphEditorDrawingStyle::kExecInPinName);
            }
        }
        // つなげるピンに輪を付けて、どこへ落とせばよいかを見せる
        for (const GraphCanvasPin& pin : state.pins) {
            if (CanConnectLinkTo(pin)) {
                dl->AddCircle(pin.position, pin.radius + GraphEditorDrawingStyle::kPinRingPadding,
                    &pin == state.snapPin ? GraphEditorDrawingStyle::kPinSnapColor : GraphEditorDrawingStyle::kPinRingColor,
                    0, GraphEditorDrawingStyle::kPinRingThickness);
            }
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            if (state.snapPin != nullptr) {
                ConnectLinkTo(*state.snapPin);
            } else {
                ReportRejectedDrop(state);
            }
            linking_ = false;
            dataLinking_ = false;
        }
        return;
    }

    const GraphCanvasPin* pin = state.hoveredPin;
    if (pin == nullptr || panning_ || drag_.kind != CanvasDragKind::kNone) {
        return;
    }
    dl->AddCircle(pin->position, pin->radius + GraphEditorDrawingStyle::kPinRingPadding, GraphEditorDrawingStyle::kPinSnapColor, 0, GraphEditorDrawingStyle::kPinRingThickness);

    // 左ドラッグの開始（GraphPinKind の並びと同じ）
    using PinPress = void (*)(GraphEditor&, const GraphCanvasPin&);
    static constexpr PinPress kPinPressActions[] = {
        // 実行入力ピンからは線を引かない
        [](GraphEditor&, const GraphCanvasPin&) {
        },
        // 実行出力ピンから線を引く
        [](GraphEditor& e, const GraphCanvasPin& p) {
            e.linking_ = true;
            e.linkFromNodeId_ = p.nodeId;
            e.linkFromPin_ = p.name;
        },
        // つながっているデータ入力ピンを掴むと、線を抜いて持ち直す（空振りで離せば切断になる）
        [](GraphEditor& e, const GraphCanvasPin& p) {
            const auto node = e.graph_.nodes.find(p.nodeId);
            if (node == e.graph_.nodes.end()) {
                return;
            }
            const auto link = node->second.paramLinks.find(p.name);
            if (link == node->second.paramLinks.end()) {
                return;
            }
            const std::string sourceId = link->second;
            e.RecordUndoSnapshotNow();
            node->second.paramLinks.erase(link);
            const GraphNode* source = e.graph_.FindNode(sourceId);
            const NodeTypeSpec* spec = source != nullptr ? NodeRegistry::GetInstance()->FindSpec(source->type) : nullptr;
            e.dataLinking_ = true;
            e.dataLinkFromNodeId_ = sourceId;
            e.dataLinkFromType_ = spec != nullptr ? spec->outputType : GraphValueType::kAny;
        },
        // データ出力ピンから線を引く
        [](GraphEditor& e, const GraphCanvasPin& p) {
            e.dataLinking_ = true;
            e.dataLinkFromNodeId_ = p.nodeId;
            e.dataLinkFromType_ = p.type;
        },
    };
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered()) {
        kPinPressActions[static_cast<size_t>(pin->kind)](*this, *pin);
    }
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        DisconnectPin(*pin);
    }
}

/** @brief ピンにつながる配線を外す（外す物が無ければ履歴に残さない） */
void GraphEditor::DisconnectPin(const GraphCanvasPin& pin) {
    GraphDesc before = graph_;
    if (kPinDisconnectors[static_cast<size_t>(pin.kind)](graph_, pin)) {
        history_.Record(std::move(before));
        dirty_ = true;
        statusMessage_ = "配線を外しました";
        statusTimer_ = kStatusShortSeconds;
    }
}

/** @brief ドラッグ中のリンク・データ配線のプレビュー線を描く（吸着中は吸着先のピンまで引く） */
void GraphEditor::DrawLinkPreviews(ImDrawList* dl, const CanvasFrameState& state) {
    const float linkCtrl = kBaseLinkCtrl * zoom_;
    const ImVec2 end = state.snapPin != nullptr ? state.snapPin->position : ImGui::GetIO().MousePos;
    const auto drawPreview = [&](const GraphCanvasPin* from, ImU32 color, float thickness) {
        if (from != nullptr) {
            dl->AddBezierCubic(from->position, ImVec2(from->position.x + linkCtrl, from->position.y), ImVec2(end.x - linkCtrl, end.y), end, color, thickness);
        }
    };
    if (linking_) {
        drawPreview(FindPin(state.pins, GraphPinKind::kExecOut, linkFromNodeId_, linkFromPin_), kColLink, GraphEditorDrawingStyle::kLinkThickness);
    }
    // データ配線は型の色で描く
    if (dataLinking_) {
        drawPreview(FindPin(state.pins, GraphPinKind::kDataOut, dataLinkFromNodeId_, ""), GraphEditorDrawingStyle::ColorForType(dataLinkFromType_), GraphEditorDrawingStyle::kDataLinkThickness);
    }
}

/** @brief 配線を1本描き、カーソルが近ければホバー中の配線の候補にする */
void GraphEditor::DrawWire(ImDrawList* dl, const ImVec2& from, const ImVec2& to, ImU32 color, float thickness,
    const GraphCanvasPin& owner, CanvasFrameState& state) {
    const float linkCtrl = kBaseLinkCtrl * zoom_;
    const ImVec2 c1(from.x + linkCtrl, from.y);
    const ImVec2 c2(to.x - linkCtrl, to.y);
    dl->AddBezierCubic(from, c1, c2, to, color, thickness);

    // ピン・ノード・入力欄の上や、他の操作の最中は配線を掴まない
    const bool canHover = state.canvasHovered && state.hoveredPin == nullptr && state.hoveredNodeId.empty()
        && !linking_ && !dataLinking_ && !panning_ && drag_.kind == CanvasDragKind::kNone && !ImGui::IsAnyItemHovered();
    if (!canHover) {
        return;
    }
    const float distance = DistanceToBezier(from, c1, c2, to, ImGui::GetIO().MousePos);
    if (distance <= GraphEditorDrawingStyle::kWireHoverDistance && (!state.hoveredLink || distance < state.hoveredLinkDistance)) {
        state.hoveredLink = owner;
        state.hoveredLinkFrom = from;
        state.hoveredLinkTo = to;
        state.hoveredLinkDistance = distance;
    }
}

/** @brief ノードどうしの接続線を描く */
void GraphEditor::DrawLinks(ImDrawList* dl, CanvasFrameState& state) {
    // 全ノードの実行の流れを、ノードの種類に合ったピンから引く（行き先が無ければ引かない）
    for (const auto& [id, node] : graph_.nodes) {
        const bool branches = NodeRegistry::GetInstance()->IsBranchType(node.type);
        for (const ExecPinDef& def : kExecPins) {
            if (def.branch != branches || (node.*def.target).empty()) {
                continue;
            }
            const GraphCanvasPin* from = FindPin(state.pins, GraphPinKind::kExecOut, id, def.name);
            const GraphCanvasPin* to = FindPin(state.pins, GraphPinKind::kExecIn, node.*def.target, GraphEditorDrawingStyle::kExecInPinName);
            if (from != nullptr && to != nullptr) {
                DrawWire(dl, from->position, to->position, kColLink, GraphEditorDrawingStyle::kLinkThickness, *from, state);
            }
        }
    }
}

/** @brief データ配線（paramLinks）の線をまとめて描く（ピン位置はDrawCanvasが同フレームで記録済み） */
void GraphEditor::DrawDataLinks(ImDrawList* dl, CanvasFrameState& state) {
    for (const auto& [id, node] : graph_.nodes) {
        for (const auto& [key, srcId] : node.paramLinks) {
            const GraphCanvasPin* from = FindPin(state.pins, GraphPinKind::kDataOut, srcId, "");
            const GraphCanvasPin* to = FindPin(state.pins, GraphPinKind::kDataIn, id, key);
            if (from != nullptr && to != nullptr) {
                DrawWire(dl, from->position, to->position, GraphEditorDrawingStyle::ColorForType(to->type), GraphEditorDrawingStyle::kDataLinkThickness, *to, state);
            }
        }
    }
}

} // namespace engine::game

#endif // USE_IMGUI
