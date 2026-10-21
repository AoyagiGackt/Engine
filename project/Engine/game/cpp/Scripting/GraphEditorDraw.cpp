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
bool TypesCompatible(GraphValueType a, GraphValueType b)
{
    return a == b || a == GraphValueType::Any || b == GraphValueType::Any;
}

// ノード種別のスペックから、指定パラメータの型を引く（スペック未登録ならAny扱い）
GraphValueType ParamTypeOf(const std::string& nodeType, const std::string& key)
{
    const NodeTypeSpec* spec = NodeRegistry::GetInstance()->FindSpec(nodeType);
    if (spec)
    {
        for (const auto& p : spec->params)
        {
            if (p.key == key)
            {
                return p.type;
            }
        }
    }
    return GraphValueType::Any;
}

} // namespace
// ══════════════════════════════════════════════════════
// キャンバスとノード描画
// ══════════════════════════════════════════════════════

/** @brief グラフのキャンバス（視点操作・ノード・配線・プレビュー線）を描画する */
void GraphEditor::DrawCanvas()
{
    ImGui::BeginChild("GraphCanvas", ImVec2(0, 0), true,
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar);

    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    bool canvasHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);

    UpdateCanvasView(canvasHovered);

    // コメント矩形は最背面に描く（ここではまだChannelsSplit前なので、ノードより必ず下に来る）
    DrawComments(dl, origin);

    dl->ChannelsSplit(2);

    CanvasFrameState state;
    if (testRuntime_.IsRunning())
    {
        // サブグラフ実行中に子グラフを開いて見ている場合でも正しく追従させるため、
        // 実行チェーンの最深部（今実際に動いているグラフ）とパスで突き合わせる
        std::string leafPath;
        const GraphRuntime& leaf = testRuntime_.GetActiveLeaf(leafPath);
        const std::string& effectivePath = leafPath.empty() ? testRuntimeRootPath_ : leafPath;
        state.viewingActiveGraph = (effectivePath == graphPath_);
        state.activeRunNodeId = leaf.GetCurrentNodeId();
    }
    dataInPins_.clear();
    dataOutPins_.clear();
    for (auto& [id, node] : graph_.nodes)
    {
        DrawNode(dl, origin, id, node, state);
    }

    dl->ChannelsMerge();

    // ノード右クリックメニューのコピー/削除はここでまとめて処理する
    // （ノード走査ループ中にgraph_.nodesを直接書き換えるとイテレータが壊れるため）
    if (!pendingDuplicateNodeId_.empty())
    {
        std::string dupId = pendingDuplicateNodeId_;
        pendingDuplicateNodeId_.clear();
        DuplicateNode(dupId);
    }
    if (!pendingDeleteNodeId_.empty())
    {
        std::string delId = pendingDeleteNodeId_;
        pendingDeleteNodeId_.clear();
        DeleteNode(delId);
        if (selectedNodeId_ == delId)
        {
            selectedNodeId_.clear();
        }
    }

    // リンク線は全ノードの矩形（nodeRects_）が出そろった後にまとめて描く
    DrawLinks(dl);
    DrawDataLinks(dl);
    DrawLinkPreviews(dl, state);
    HandleCanvasShortcuts(origin, canvasHovered, state);

    ImGui::SetWindowFontScale(1.0f); // 他のImGuiウィジェット（ツールバー等）に影響しないよう戻す
    ImGui::EndChild();
}

/** @brief マウスホイールズームと中ボタンドラッグパンを処理する */
void GraphEditor::UpdateCanvasView(bool canvasHovered)
{
    // マウスホイールでズーム（カーソル下のキャンバスにいる時だけ）
    if (canvasHovered)
    {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f)
        {
            zoom_ = std::clamp(zoom_ + wheel * GraphEditorDrawingStyle::kZoomStep, GraphEditorDrawingStyle::kMinZoom, GraphEditorDrawingStyle::kMaxZoom);
        }
    }
    ImGui::SetWindowFontScale(zoom_);

    // 中ボタンドラッグでパン（既存のStageEditor系ツールと同じ操作感に合わせる）
    // パンはグラフ座標系（ズームの影響を受けない単位）で持つので、スクリーン距離をzoomで割って変換する
    if (canvasHovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
    {
        ImVec2 d = ImGui::GetIO().MouseDelta;
        panOffsetX_ += d.x / zoom_;
        panOffsetY_ += d.y / zoom_;
    }
}

/** @brief ノード1つ分（タイトル・パラメータ・ピン・枠）を描画し、ドラッグ操作を処理する */
void GraphEditor::DrawNode(ImDrawList* dl, const ImVec2& origin, const std::string& id, GraphNode& node, CanvasFrameState& state)
{
    GraphNodeRenderer::Draw(*this, dl, origin, id, node, &state);
}

/** @brief ノードのパラメータ編集ウィジェットと左端のデータ入力ピンを描画する */
void GraphEditor::DrawNodeParams(ImDrawList* dl, const std::string& id, GraphNode& node, const ImVec2& nodeScreenPos, CanvasFrameState& state)
{
    const float nodeW = GraphEditorDrawingStyle::kBaseNodeWidth * zoom_;
    const float pinR = GraphEditorDrawingStyle::kBasePinRadius * zoom_;
    const float pinPad = GraphEditorDrawingStyle::kBasePinPad * zoom_;

    // パラメータ編集（型に応じてウィジェットを出し分ける）
    // 各行の左端にデータ入力ピンを置くため、行の矩形を記録する
    for (auto& [key, val] : node.params)
    {
        ImGui::PushID(key.c_str());
        bool isLinked = node.paramLinks.count(key) > 0;
        if (isLinked)
        {
            // データ配線されているパラメータはリテラル編集を出さず、接続元を表示する
            ImGui::TextDisabled("%s ← %s", key.c_str(), node.paramLinks[key].c_str());
        }
        else
        {
            ImGui::SetNextItemWidth(nodeW);
            GraphValueEditResult edit = DrawGraphValueInput(key.c_str(), val);
            // チェック操作は1回、テキストと数値は入力開始から終了までを1回のUndoにする
            if (!edit.discrete && edit.activated)
            {
                BeginUndoCapture();
            }
            if (edit.changed)
            {
                if (edit.discrete)
                {
                    RecordUndoSnapshotNow();
                }
                else
                {
                    MarkUndoDirty();
                }
                val = std::move(edit.value);
            }
            if (!edit.discrete && edit.deactivated)
            {
                CommitUndoCapture();
            }
        }

        // データ入力ピン（この行の左端）
        float rowCenterY = (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5f;
        ImVec2 pinPos(nodeScreenPos.x - pinPad, rowCenterY);
        dataInPins_[id][key] = pinPos;

        GraphValueType paramType = ParamTypeOf(node.type, key);
        float dataPinR = pinR * GraphEditorDrawingStyle::kDataPinRadiusRatio;
        dl->AddCircleFilled(pinPos, dataPinR, GraphEditorDrawingStyle::ColorForType(paramType));
        if (GraphEditorDrawingStyle::IsHoveringCircle(pinPos, dataPinR + GraphEditorDrawingStyle::kPinHoverPadding))
        {
            state.hoveringAnyPin = true;
            // ドロップで接続（型が合うときだけ自分自身への接続は不可）
            // つなげない場合も無反応にせず、理由をステータス表示する
            if (dataLinking_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            {
                if (dataLinkFromNodeId_ == id)
                {
                    statusMessage_ = "自分自身のピンへは接続できません";
                    statusTimer_ = kStatusNormalSeconds;
                }
                else if (!TypesCompatible(dataLinkFromType_, paramType))
                {
                    statusMessage_ = "型が違うため接続できません。同じ色のピン同士をつないでください";
                    statusTimer_ = kStatusNormalSeconds;
                }
                else
                {
                    RecordUndoSnapshotNow();
                    node.paramLinks[key] = dataLinkFromNodeId_;
                    dataLinking_ = false;
                    state.dataLinkCompletedThisFrame = true;
                }
            }
            // 右クリックで配線解除
            if (isLinked && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            {
                RecordUndoSnapshotNow();
                node.paramLinks.erase(key);
            }
        }
        ImGui::PopID();
    }
}

// ══════════════════════════════════════════════════════
// リンク描画とキャンバス操作
// ══════════════════════════════════════════════════════

/** @brief ドラッグ中のリンク・データ配線のプレビュー線を描き、空振りならキャンセルする */
void GraphEditor::DrawLinkPreviews(ImDrawList* dl, const CanvasFrameState& state)
{
    const float linkCtrl = kBaseLinkCtrl * zoom_;

    // ドラッグ中のリンクのプレビュー線
    if (linking_ && state.linkFromFound)
    {
        ImVec2 mouse = ImGui::GetIO().MousePos;
        ImVec2 c1(state.linkFromScreenPos.x + linkCtrl, state.linkFromScreenPos.y);
        ImVec2 c2(mouse.x - linkCtrl, mouse.y);
        dl->AddBezierCubic(state.linkFromScreenPos, c1, c2, mouse, kColLink, GraphEditorDrawingStyle::kLinkThickness);
    }
    if (linking_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && !state.linkCompletedThisFrame)
    {
        linking_ = false; // ピン以外の場所で離したらキャンセル
    }

    // ドラッグ中のデータ配線のプレビュー線（型の色で描く）
    if (dataLinking_ && state.dataLinkFromFound)
    {
        ImVec2 mouse = ImGui::GetIO().MousePos;
        ImVec2 c1(state.dataLinkFromScreenPos.x + linkCtrl, state.dataLinkFromScreenPos.y);
        ImVec2 c2(mouse.x - linkCtrl, mouse.y);
        dl->AddBezierCubic(state.dataLinkFromScreenPos, c1, c2, mouse, GraphEditorDrawingStyle::ColorForType(dataLinkFromType_), GraphEditorDrawingStyle::kDataLinkThickness);
    }
    if (dataLinking_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && !state.dataLinkCompletedThisFrame)
    {
        dataLinking_ = false; // ピン以外の場所で離したらキャンセル
    }
}

/** @brief 右クリックのノード追加メニューとDeleteキー削除を処理する */
void GraphEditor::HandleCanvasShortcuts(const ImVec2& origin, bool canvasHovered, const CanvasFrameState& state)
{
    // 右クリックでノード追加メニュー（クリック位置をスクリーン座標からグラフ座標へ逆変換する）
    // ノード本体やピンの上で右クリックした場合はそちら側の専用メニューに譲る
    if (canvasHovered && !state.hoveringAnyPin && !state.hoveringNode && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        ImVec2 m = ImGui::GetIO().MousePos;
        pendingAddX_ = (m.x - origin.x) / zoom_ - panOffsetX_;
        pendingAddY_ = (m.y - origin.y) / zoom_ - panOffsetY_;
        nodeSearchBuf_[0] = '\0'; // 前回の検索文字列を持ち越さない
        ImGui::OpenPopup("AddNodePopup");
    }
    DrawAddNodeMenu();

    // 何もない所を左クリックしたら選択解除（誤選択のままDeleteで消してしまう事故を防ぐ）
    // コメント枠の掴み手やパラメータ欄はImGuiのアイテムなので、IsAnyItemHovered/Activeで除外できる
    if (canvasHovered && !state.hoveringAnyPin && !state.hoveringNode
        && !ImGui::IsAnyItemHovered() && !ImGui::IsAnyItemActive()
        && !linking_ && !dataLinking_
        && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        selectedNodeId_.clear();
        selectedCommentId_.clear();
    }

    // Deleteキーで選択中のノード／コメントを削除
    if (canvasHovered && ImGui::IsKeyPressed(ImGuiKey_Delete))
    {
        if (!selectedNodeId_.empty())
        {
            DeleteNode(selectedNodeId_);
            selectedNodeId_.clear();
        }
        else if (!selectedCommentId_.empty())
        {
            DeleteComment(selectedCommentId_);
            selectedCommentId_.clear();
        }
    }
}

/** @brief ノードどうしの接続線を描く */
void GraphEditor::DrawLinks(ImDrawList* dl)
{
    // ピンの位置とベジェ曲線の膨らみはズームに合わせる
    const float pinPad = GraphEditorDrawingStyle::kBasePinPad * zoom_;
    const float linkCtrl = kBaseLinkCtrl * zoom_;

    // 同じフレームでDrawCanvas()が埋めたnodeRects_（実際の描画結果の矩形）を使うので、
    // パラメータ数によるノードの高さ変化を近似せずに正確なピン位置で結線できる
    auto pinPosFor = [&](const std::string& id, const char* pinKind) -> std::optional<ImVec2> {
        auto it = nodeRects_.find(id);
        if (it == nodeRects_.end())
        {
            return std::nullopt;
        }
        const ImVec2& nMin = it->second.first;
        const ImVec2& nMax = it->second.second;
        /** @brief フローピンの種類名と、描画矩形から求めた接続位置 */
        struct FlowPinPosition {
            const char* name;
            ImVec2 position;
        };
        const FlowPinPosition positions[] = {
            { "in", { nMin.x - pinPad, nMin.y + GraphEditorDrawingStyle::kPinRowOffset * zoom_ } },
            { "true", { nMax.x + pinPad, nMin.y + (nMax.y - nMin.y) * GraphEditorDrawingStyle::kTruePinHeightRatio } },
            { "false", { nMax.x + pinPad, nMin.y + (nMax.y - nMin.y) * GraphEditorDrawingStyle::kFalsePinHeightRatio } },
            { "next", { nMax.x + pinPad, nMin.y + GraphEditorDrawingStyle::kPinRowOffset * zoom_ } },
        };
        const auto found = std::find_if(std::begin(positions), std::end(positions), [&](const FlowPinPosition& pin)
        {
            return std::strcmp(pinKind, pin.name) == 0;
        });
        return found != std::end(positions) ? found->position : positions[std::size(positions) - 1].position;
    };

    // 出力ピンから行き先ノードの入力ピンへ曲線を引く（行き先が無ければ引かない）
    auto drawLink = [&](const std::string& fromId, const char* fromPin, const std::string& toId) {
        if (toId.empty())
        {
            return;
        }
        std::optional<ImVec2> from = pinPosFor(fromId, fromPin);
        std::optional<ImVec2> to = pinPosFor(toId, "in");
        if (!from || !to)
        {
            return;
        }
        ImVec2 c1(from->x + linkCtrl, from->y);
        ImVec2 c2(to->x - linkCtrl, to->y);
        dl->AddBezierCubic(*from, c1, c2, *to, kColLink, GraphEditorDrawingStyle::kLinkThickness);
    };

    // 全ノードの実行の流れを、ノードの種類に合ったピンから引く
    for (const auto& [id, node] : graph_.nodes)
    {
        const bool branches = NodeRegistry::GetInstance()->IsBranchType(node.type);
        for (const ExecPinDef& pin : kExecPins)
        {
            if (pin.branch == branches)
            {
                drawLink(id, pin.name, node.*pin.target);
            }
        }
    }
}

/** @brief データ配線（paramLinks）の線をまとめて描く（ピン位置はDrawCanvasが同フレームで記録済み） */
void GraphEditor::DrawDataLinks(ImDrawList* dl)
{
    const float linkCtrl = kBaseLinkCtrl * zoom_;

    for (const auto& [id, node] : graph_.nodes)
    {
        auto inIt = dataInPins_.find(id);
        if (inIt == dataInPins_.end())
        {
            continue;
        }

        for (const auto& [key, srcId] : node.paramLinks)
        {
            auto toIt = inIt->second.find(key);
            auto fromIt = dataOutPins_.find(srcId);
            if (toIt == inIt->second.end() || fromIt == dataOutPins_.end())
            {
                continue;
            }

            const ImVec2& from = fromIt->second;
            const ImVec2& to = toIt->second;
            ImVec2 c1(from.x + linkCtrl, from.y);
            ImVec2 c2(to.x - linkCtrl, to.y);
            dl->AddBezierCubic(from, c1, c2, to, GraphEditorDrawingStyle::ColorForType(ParamTypeOf(node.type, key)), GraphEditorDrawingStyle::kDataLinkThickness);
        }
    }
}

// ══════════════════════════════════════════════════════
// ノード生成
// ══════════════════════════════════════════════════════

} // namespace engine::game

#endif // USE_IMGUI
