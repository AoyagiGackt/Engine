/**
 * @file GraphEditor.cpp
 * @brief GraphEditorが担当する処理を実装するファイル
 */
#ifdef USE_IMGUI
#include "GraphEditor.h"
#include "GraphEditorServices.h"
#include "EditorUI.h"
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

namespace {
// ズーム1.0のときの基準サイズ実際の描画時は GraphEditor::zoom_ を掛けて使う
constexpr float kBaseNodeWidth = 200.0f;
constexpr float kBasePinRadius = 6.0f;
constexpr float kBasePinPad = 10.0f; // ノード枠からピンをどれだけ外側に出すか
constexpr float kBaseLinkCtrl = 60.0f; // ベジエ曲線の制御点オフセット
constexpr ImU32 kColBg = IM_COL32(45, 45, 55, 235);
constexpr ImU32 kColBgSel = IM_COL32(70, 70, 95, 235);
constexpr ImU32 kColBorder = IM_COL32(90, 90, 110, 255);
constexpr ImU32 kColStart = IM_COL32(90, 200, 120, 255);
constexpr ImU32 kColPinIn = IM_COL32(230, 230, 230, 255);
constexpr ImU32 kColPinOut = IM_COL32(230, 200, 90, 255);
constexpr ImU32 kColPinTrue = IM_COL32(90, 200, 120, 255);
constexpr ImU32 kColPinFalse = IM_COL32(210, 90, 90, 255);
constexpr ImU32 kColLink = IM_COL32(220, 220, 220, 200);
constexpr ImU32 kColRunning = IM_COL32(255, 210, 60, 255); // Run中、実行が今いるノードの枠

// データピンの型別カラー（使い方ガイドの説明と一致させること）
ImU32 ColorForType(GraphValueType t)
{
    switch (t) {
    case GraphValueType::Float:
        return IM_COL32(110, 220, 110, 255); // 緑
    case GraphValueType::Bool:
        return IM_COL32(220, 100, 100, 255); // 赤
    case GraphValueType::String:
        return IM_COL32(200, 120, 220, 255); // 紫
    default:
        return IM_COL32(240, 240, 240, 255); // 白（Any）
    }
}

// 同じ型同士、またはどちらかがAny（白）なら接続できる
bool TypesCompatible(GraphValueType a, GraphValueType b)
{
    return a == b || a == GraphValueType::Any || b == GraphValueType::Any;
}

// ノード種別のスペックから、指定パラメータの型を引く（スペック未登録ならAny扱い）
GraphValueType ParamTypeOf(const std::string& nodeType, const std::string& key)
{
    const NodeTypeSpec* spec = NodeRegistry::GetInstance()->FindSpec(nodeType);
    if (spec) {
        for (const auto& p : spec->params) {
            if (p.key == key) {
                return p.type;
            }
        }
    }
    return GraphValueType::Any;
}

// ASCIIの大文字小文字を無視した部分一致（日本語などマルチバイト部分はバイト列そのまま比較）
bool ContainsCI(const std::string& hay, const std::string& needle)
{
    if (needle.empty()) {
        return true;
    }
    auto eq = [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    };
    return std::search(hay.begin(), hay.end(), needle.begin(), needle.end(), eq) != hay.end();
}

bool IsHoveringCircle(const ImVec2& center, float radius)
{
    ImVec2 m = ImGui::GetIO().MousePos;
    float dx = m.x - center.x;
    float dy = m.y - center.y;
    return (dx * dx + dy * dy) <= (radius * radius);
}

} // namespace

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

void GraphEditor::UpdateContent(Input* input)
{
    bool wasVisible = visible_;
    if (input && input->TriggerKey(DIK_F1)) {
        visible_ = !visible_;
    }
    if (visible_ != wasVisible) {
        if (visible_) {
            // 開いている間は背後のゲームを止めるTimeManagerのタイムスケールを流用する
            savedTimeScale_ = TimeManager::GetInstance()->GetTimeScale();
            TimeManager::GetInstance()->SetTimeScale(0.0f);
        } else {
            TimeManager::GetInstance()->SetTimeScale(savedTimeScale_);
        }
    }
    if (!visible_) {
        return;
    }

    // エディタ内蔵のタイマー類は、開いている間はTimeManagerのdtが0になる（背後のゲームを止めるため）ので、
    // ImGuiが持つ実時間ベースのdt（io.DeltaTime）を使う
    const float realDt = ImGui::GetIO().DeltaTime;
    if (statusTimer_ > 0.0f) {
        statusTimer_ -= realDt;
    }

    // Ctrl系ショートカット（テキスト入力欄にフォーカスがある間はImGui自身の入力欄内編集に譲る）
    if (!ImGui::GetIO().WantTextInput) {
        ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)) {
            Undo();
        } else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y)) {
            Redo();
        } else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) {
            Save();
        } else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D)) {
            // DrawCanvas()のノード走査ループの外なので、ここでは直接複製してよい
            if (!selectedNodeId_.empty() && graph_.FindNode(selectedNodeId_)) {
                DuplicateNode(selectedNodeId_);
            }
        }
    }

    // 画面全体を覆う一枚のウィンドウとして開く背景を完全不透明にして後ろのゲーム画面を隠す
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::SetNextWindowBgAlpha(1.0f);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse;
    if (!ImGui::Begin("ノードエディタ", nullptr, flags)) {
        ImGui::End();
        return;
    }

    // ツールバー・使い方
    ImGui::TextDisabled("F1: 表示/非表示    右クリック: ノード/コメント追加    ホイール: ズーム    中ドラッグ: パン");
    if (ImGui::CollapsingHeader("使い方")) {
        ImGui::BulletText("何もない所を右クリック → ノードやコメント枠を追加（よく使う物は直接、他はジャンル別サブメニューに分類）");
        ImGui::BulletText("追加メニュー上部の検索欄に入力すると、名前・ジャンル・説明で絞り込める");
        ImGui::BulletText("ノードにマウスを乗せると、何ができるノードか説明がツールチップで出る");
        ImGui::BulletText("Ctrl+Z: 元に戻す  Ctrl+Y: やり直す  Ctrl+S: 保存  Ctrl+D: 選択ノードを複製");
        ImGui::BulletText("実行の流れ: ノード右上の黄ピンを、次ノード左上の白ピンへドラッグして接続");
        ImGui::BulletText("Ifノードは緑ピン=条件が真のとき、赤ピン=偽のときの行き先");
        ImGui::BulletText("値の受け渡し: ノード右下の出力ピンを、パラメータ左の同じ色のピンへドラッグ");
        ImGui::BulletText("　ピンの色 = 型: 緑=数値  赤=ON/OFF  紫=文字列  白=なんでも接続可");
        ImGui::BulletText("パラメータ左のピンを右クリック → データ配線を解除");
        ImGui::BulletText("タイトルをドラッグ: ノード移動    ノード選択+Deleteキー: 削除");
        ImGui::BulletText("何もない所を左クリック → 選択解除");
        ImGui::BulletText("実行ボタンでこのグラフをその場でテスト黄色い枠 = 今実行中のノード");
        ImGui::BulletText("Subgraphノード: 別のグラフJSONを部品として呼び出す（カプセル化）");
        ImGui::BulletText("フラグはステージエディタ(F2)のトリガーと共有GetFlag/SetFlagで読み書き");
    }
    if (ImGui::CollapsingHeader("はじめてのイベント作成", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextWrapped("例として、ステージのトリガーがONになったら1秒待ち、効果音を鳴らす流れを作ります。");
        ImGui::BulletText("1  空白を右クリックし、GetFlag、Wait、PlaySEを追加する");
        ImGui::BulletText("2  GetFlagのflagへステージエディタで設定したフラグ名を入力する");
        ImGui::BulletText("3  ノード上部の実行ピンを GetFlag から Wait、PlaySE の順につなぐ");
        ImGui::BulletText("4  Waitのsecondsへ1、PlaySEのpathへ音声ファイルを指定する");
        ImGui::BulletText("5  最初のGetFlagを選択し、開始ノードに設定する");
        ImGui::BulletText("6  実行で黄色い枠が順に進むことを確認して保存する");
        ImGui::TextDisabled("値ピンは計算結果を渡す時に使います。固定値だけなら入力欄へ直接入力できます。");
    }
    if (!selectedNodeId_.empty()) {
        const GraphNode* selected = graph_.FindNode(selectedNodeId_);
        const NodeTypeSpec* selectedSpec = selected
            ? NodeRegistry::GetInstance()->FindSpec(selected->type)
            : nullptr;
        if (selected && selectedSpec && ImGui::CollapsingHeader("選択中ノードの説明", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("%s  %s", selected->type.c_str(), selectedSpec->category.c_str());
            ImGui::TextWrapped("%s", selectedSpec->description.c_str());
            auto parameterHelp = [](const std::string& key) -> const char* {
                if (key == "flag") return "ステージエディタのトリガーと共有するフラグ名";
                if (key == "name" || key == "var" || key == "into") return "グラフ内で値を識別するための変数名";
                if (key == "value") return "保存、比較、または設定する値";
                if (key == "seconds" || key == "duration") return "処理を待つ時間  単位は秒";
                if (key == "frames") return "処理を止める時間  単位はフレーム";
                if (key == "path") return "読み込むグラフ、音声などのファイルパス";
                if (key == "target") return "ステージに配置した敵などの名前";
                if (key == "amount") return "ダメージ量または回復量";
                if (key == "op") return "計算または比較に使う記号";
                if (key == "x" || key == "y" || key == "z") return "移動先または位置のワールド座標";
                if (key == "a" || key == "b") return "計算や比較へ渡す入力値";
                if (key == "loop") return "ONなら停止するまで繰り返し再生する";
                if (key == "visible") return "ONなら表示し、OFFなら非表示にする";
                return "ノードが処理に使用する入力値";
            };
            for (const NodeParamSpec& param : selectedSpec->params) {
                ImGui::BulletText("%s  %s", param.key.c_str(), parameterHelp(param.key));
            }
            ImGui::TextDisabled(selectedSpec->hasOutput
                    ? "右下の色付きピンから処理結果を別ノードへ渡せます"
                    : "このノードは値を出力せず、上部の実行線だけで次へ進みます");
        }
    }
    char pathBuf[256];
    strncpy_s(pathBuf, graphPath_.c_str(), _TRUNCATE);
    ImGui::SetNextItemWidth(360.0f);
    if (ImGui::InputText("##path", pathBuf, sizeof(pathBuf))) {
        graphPath_ = pathBuf;
    }
    ImGui::SameLine();
    if (ImGui::Button("開く")) {
        // 未保存の編集がある時は黙って破棄せず、確認モーダルを挟む
        if (dirty_) {
            pendingConfirmOpenPath_ = graphPath_;
            requestConfirmOpen_ = true;
        } else {
            Open(graphPath_);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("保存 (Ctrl+S)")) {
        Save();
    }
    if (dirty_) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "未保存");
    }

    // Subgraphの開くボタン等、ウィンドウスコープ外で予約された分もここでモーダルを開く
    if (requestConfirmOpen_) {
        ImGui::OpenPopup("グラフを開く確認");
        requestConfirmOpen_ = false;
    }
    if (graphics::EditorUI::ConfirmModal("グラフを開く確認",
            "未保存の変更があります。\n変更を破棄して開き直しますか？",
            "破棄して開く")
        == graphics::EditorUI::ConfirmResult::Ok) {
        Open(pendingConfirmOpenPath_);
    }

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    bool hasSelection = !selectedNodeId_.empty() && graph_.FindNode(selectedNodeId_);
    ImGui::BeginDisabled(!hasSelection);
    if (ImGui::Button("開始ノードに設定")) {
        RecordUndoSnapshotNow();
        graph_.startNodeId = selectedNodeId_;
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("自動整列")) {
        ArrangeNodes();
    }

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::BeginDisabled(!history_.CanUndo());
    if (ImGui::Button("元に戻す (Ctrl+Z)")) {
        Undo();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!history_.CanRedo());
    if (ImGui::Button("やり直す (Ctrl+Y)")) {
        Redo();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    if (!testRuntime_.IsRunning()) {
        if (ImGui::Button("実行")) {
            testRuntime_.Start(&graph_);
            testRuntimeRootPath_ = graphPath_;
        }
    } else {
        if (ImGui::Button("停止")) {
            testRuntime_ = GraphRuntime { };
        }
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.6f, 1.0f), "実行中...");
    }

    if (statusTimer_ > 0.0f) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", statusMessage_.c_str());
    }

    ImGui::Separator();

    DrawCanvas();

    ImGui::End();

    DrawVariablesPanel();

    // Subgraphノードの開くボタン（ノード走査ループ内で押される）はここでまとめて処理する
    if (!pendingOpenPath_.empty()) {
        std::string path = pendingOpenPath_;
        pendingOpenPath_.clear();
        if (dirty_) {
            // ここはウィンドウのIDスコープ外なので、モーダルは次フレームのツールバー側で開く
            pendingConfirmOpenPath_ = path;
            requestConfirmOpen_ = true;
        } else {
            Open(path);
        }
    }

    if (testRuntime_.IsRunning()) {
        testRuntime_.Update(realDt);
    }
}

// ══════════════════════════════════════════════════════
// キャンバスとノード描画
// ══════════════════════════════════════════════════════

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
    if (testRuntime_.IsRunning()) {
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
    for (auto& [id, node] : graph_.nodes) {
        DrawNode(dl, origin, id, node, state);
    }

    dl->ChannelsMerge();

    // ノード右クリックメニューのコピー/削除はここでまとめて処理する
    // （ノード走査ループ中にgraph_.nodesを直接書き換えるとイテレータが壊れるため）
    if (!pendingDuplicateNodeId_.empty()) {
        std::string dupId = pendingDuplicateNodeId_;
        pendingDuplicateNodeId_.clear();
        DuplicateNode(dupId);
    }
    if (!pendingDeleteNodeId_.empty()) {
        std::string delId = pendingDeleteNodeId_;
        pendingDeleteNodeId_.clear();
        DeleteNode(delId);
        if (selectedNodeId_ == delId) {
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

void GraphEditor::UpdateCanvasView(bool canvasHovered)
{
    // マウスホイールでズーム（カーソル下のキャンバスにいる時だけ）
    if (canvasHovered) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            zoom_ = std::clamp(zoom_ + wheel * 0.1f, 0.3f, 2.5f);
        }
    }
    ImGui::SetWindowFontScale(zoom_);

    // 中ボタンドラッグでパン（既存のStageEditor系ツールと同じ操作感に合わせる）
    // パンはグラフ座標系（ズームの影響を受けない単位）で持つので、スクリーン距離をzoomで割って変換する
    if (canvasHovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        ImVec2 d = ImGui::GetIO().MouseDelta;
        panOffsetX_ += d.x / zoom_;
        panOffsetY_ += d.y / zoom_;
    }
}

void GraphEditor::DrawNode(ImDrawList* dl, const ImVec2& origin, const std::string& id, GraphNode& node, CanvasFrameState& state)
{
    GraphNodeRenderer::Draw(*this, dl, origin, id, node, &state);
}

void GraphEditor::DrawNodeContent(ImDrawList* dl, const ImVec2& origin, const std::string& id, GraphNode& node, CanvasFrameState& state)
{
    const float nodeW = kBaseNodeWidth * zoom_;
    const float pinR = kBasePinRadius * zoom_;
    const float pinPad = kBasePinPad * zoom_;
    const float boxPad = 6.0f * zoom_;

    ImGui::PushID(id.c_str());
    dl->ChannelsSetCurrent(1);

    ImVec2 nodeScreenPos(origin.x + (panOffsetX_ + node.editorX) * zoom_, origin.y + (panOffsetY_ + node.editorY) * zoom_);
    ImGui::SetCursorScreenPos(nodeScreenPos);
    ImGui::BeginGroup();

    // タイトル（このボタン自体がドラッグハンドル兼用）
    ImGui::Button(node.type.c_str(), ImVec2(nodeW, 0));
    if (ImGui::IsItemHovered()) {
        state.hoveringNode = true; // キャンバス右クリックのノード追加メニューと衝突しないよう抑制する
        const NodeTypeSpec* spec = NodeRegistry::GetInstance()->FindSpec(node.type);
        if (spec && !spec->description.empty()) {
            ImGui::SetTooltip("%s", spec->description.c_str());
        }
    }
    if (ImGui::IsItemActivated()) {
        selectedNodeId_ = id;
        selectedCommentId_.clear();
        BeginUndoCapture();
    }
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        ImVec2 d = ImGui::GetIO().MouseDelta;
        node.editorX += d.x / zoom_;
        node.editorY += d.y / zoom_;
        MarkUndoDirty();
    }
    if (ImGui::IsItemDeactivated()) {
        CommitUndoCapture();
    }
    ImGui::TextDisabled("%s", id.c_str());

    DrawNodeParams(dl, id, node, nodeScreenPos, state);

    // Subgraphノードは中身をワンクリックで開けるようにする（Open()はループ後に遅延実行）
    if (node.type == "Subgraph") {
        if (ImGui::SmallButton("サブグラフを開く")) {
            auto it = node.params.find("path");
            if (it != node.params.end()) {
                pendingOpenPath_ = AsString(it->second);
            }
        }
    }

    ImGui::EndGroup();
    ImVec2 nodeMin = ImGui::GetItemRectMin();
    ImVec2 nodeMax = ImGui::GetItemRectMax();
    nodeRects_[id] = { nodeMin, nodeMax };

    // タイトルやパラメータを含むノード全体で右クリックメニューを開く
    // ノード走査中にコンテナを変更しないよう、複製と削除はキャンバス描画後に遅延実行する
    const ImVec2 interactionMin(nodeMin.x - boxPad, nodeMin.y - boxPad * 0.67f);
    const ImVec2 interactionMax(nodeMax.x + boxPad, nodeMax.y + boxPad);
    if (ImGui::IsMouseHoveringRect(interactionMin, interactionMax)) {
        state.hoveringNode = true;
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            selectedNodeId_ = id;
            selectedCommentId_.clear();
            ImGui::OpenPopup("NodeContextMenu");
        }
    }
    if (ImGui::BeginPopup("NodeContextMenu")) {
        if (ImGui::MenuItem("開始ノードに設定")) {
            RecordUndoSnapshotNow();
            graph_.startNodeId = id;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("複製")) {
            pendingDuplicateNodeId_ = id;
        }
        if (ImGui::MenuItem("削除")) {
            pendingDeleteNodeId_ = id;
        }
        ImGui::EndPopup();
    }

    // 背景（枠・塗り、開始ノードは緑枠で強調）
    dl->ChannelsSetCurrent(0);
    ImU32 bg = (id == selectedNodeId_) ? kColBgSel : kColBg;
    dl->AddRectFilled(ImVec2(nodeMin.x - boxPad, nodeMin.y - boxPad * 0.67f), ImVec2(nodeMax.x + boxPad, nodeMax.y + boxPad), bg, 4.0f * zoom_);
    ImU32 border = (id == graph_.startNodeId) ? kColStart : kColBorder;
    float borderThickness = 2.0f * zoom_;
    if (state.viewingActiveGraph && id == state.activeRunNodeId) {
        border = kColRunning;
        borderThickness = 3.5f * zoom_; // Run中は太くして一目で分かるようにする
    }
    dl->AddRect(ImVec2(nodeMin.x - boxPad, nodeMin.y - boxPad * 0.67f), ImVec2(nodeMax.x + boxPad, nodeMax.y + boxPad), border, 4.0f * zoom_, 0, borderThickness);

    // 開始ノードは緑枠だけだと気づきにくいため、枠の上にラベルも出す
    if (id == graph_.startNodeId) {
        ImVec2 labelPos(nodeMin.x - boxPad, nodeMin.y - boxPad * 0.67f - ImGui::GetFontSize() - 2.0f * zoom_);
        dl->AddText(labelPos, kColStart, "開始");
    }

    dl->ChannelsSetCurrent(1);

    // 実行入力ピン（左上。タイトル行の高さに合わせる）
    ImVec2 inPin(nodeMin.x - pinPad, nodeMin.y + 10.0f * zoom_);
    dl->AddCircleFilled(inPin, pinR, kColPinIn);
    if (IsHoveringCircle(inPin, pinR + 2.0f)) {
        state.hoveringAnyPin = true;
        if (linking_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && linkFromNodeId_ != id) {
            auto fromIt = graph_.nodes.find(linkFromNodeId_);
            if (fromIt != graph_.nodes.end()) {
                RecordUndoSnapshotNow();
                GraphNode& fromNode = fromIt->second;
                if (linkFromPin_ == "next") {
                    fromNode.next = id;
                } else if (linkFromPin_ == "true") {
                    fromNode.nextTrue = id;
                } else if (linkFromPin_ == "false") {
                    fromNode.nextFalse = id;
                }
            }
            linking_ = false;
            state.linkCompletedThisFrame = true;
        }
    }

    // 実行出力ピン（右上。Ifのみtrue/falseの2つ、他は1つ）
    auto drawOutputPin = [&](const char* pinKind, ImVec2 pos, ImU32 col) {
        dl->AddCircleFilled(pos, pinR, col);
        if (IsHoveringCircle(pos, pinR + 2.0f)) {
            state.hoveringAnyPin = true;
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !linking_ && !dataLinking_) {
                linking_ = true;
                linkFromNodeId_ = id;
                linkFromPin_ = pinKind;
            }
        }
        if (linking_ && linkFromNodeId_ == id && linkFromPin_ == pinKind) {
            state.linkFromScreenPos = pos;
            state.linkFromFound = true;
        }
    };

    if (node.type == "If") {
        drawOutputPin("true", ImVec2(nodeMax.x + pinPad, nodeMin.y + (nodeMax.y - nodeMin.y) * 0.33f), kColPinTrue);
        drawOutputPin("false", ImVec2(nodeMax.x + pinPad, nodeMin.y + (nodeMax.y - nodeMin.y) * 0.66f), kColPinFalse);
    } else {
        drawOutputPin("next", ImVec2(nodeMax.x + pinPad, nodeMin.y + 10.0f * zoom_), kColPinOut);
    }

    // データ出力ピン（右下。値を出力するノードだけ）
    const NodeTypeSpec* spec = NodeRegistry::GetInstance()->FindSpec(node.type);
    if (spec && spec->hasOutput) {
        ImVec2 outPin(nodeMax.x + pinPad, nodeMax.y - 10.0f * zoom_);
        dataOutPins_[id] = outPin;

        float dataPinR = pinR * 0.75f;
        dl->AddCircleFilled(outPin, dataPinR, ColorForType(spec->outputType));
        if (IsHoveringCircle(outPin, dataPinR + 3.0f)) {
            state.hoveringAnyPin = true;
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !linking_ && !dataLinking_) {
                dataLinking_ = true;
                dataLinkFromNodeId_ = id;
                dataLinkFromType_ = spec->outputType;
            }
        }
        if (dataLinking_ && dataLinkFromNodeId_ == id) {
            state.dataLinkFromScreenPos = outPin;
            state.dataLinkFromFound = true;
        }
    }

    ImGui::PopID();
}

void GraphEditor::DrawNodeParams(ImDrawList* dl, const std::string& id, GraphNode& node, const ImVec2& nodeScreenPos, CanvasFrameState& state)
{
    const float nodeW = kBaseNodeWidth * zoom_;
    const float pinR = kBasePinRadius * zoom_;
    const float pinPad = kBasePinPad * zoom_;

    // パラメータ編集（型に応じてウィジェットを出し分ける）
    // 各行の左端にデータ入力ピンを置くため、行の矩形を記録する
    for (auto& [key, val] : node.params) {
        ImGui::PushID(key.c_str());
        bool isLinked = node.paramLinks.count(key) > 0;
        if (isLinked) {
            // データ配線されているパラメータはリテラル編集を出さず、接続元を表示する
            ImGui::TextDisabled("%s ← %s", key.c_str(), node.paramLinks[key].c_str());
        } else {
            ImGui::SetNextItemWidth(nodeW);
            if (std::holds_alternative<float>(val)) {
                float f = std::get<float>(val);
                bool changed = ImGui::InputFloat(key.c_str(), &f);
                if (ImGui::IsItemActivated()) {
                    BeginUndoCapture();
                }
                if (changed) {
                    MarkUndoDirty();
                    val = f;
                }
                if (ImGui::IsItemDeactivated()) {
                    CommitUndoCapture();
                }
            } else if (std::holds_alternative<bool>(val)) {
                bool b = std::get<bool>(val);
                if (ImGui::Checkbox(key.c_str(), &b)) {
                    RecordUndoSnapshotNow();
                    val = b;
                }
            } else {
                std::string s = std::get<std::string>(val);
                char buf[256];
                strncpy_s(buf, s.c_str(), _TRUNCATE);
                bool changed = ImGui::InputText(key.c_str(), buf, sizeof(buf));
                if (ImGui::IsItemActivated()) {
                    BeginUndoCapture();
                }
                if (changed) {
                    MarkUndoDirty();
                    val = std::string(buf);
                }
                if (ImGui::IsItemDeactivated()) {
                    CommitUndoCapture();
                }
            }
        }

        // データ入力ピン（この行の左端）
        float rowCenterY = (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5f;
        ImVec2 pinPos(nodeScreenPos.x - pinPad, rowCenterY);
        dataInPins_[id][key] = pinPos;

        GraphValueType paramType = ParamTypeOf(node.type, key);
        float dataPinR = pinR * 0.75f;
        dl->AddCircleFilled(pinPos, dataPinR, ColorForType(paramType));
        if (IsHoveringCircle(pinPos, dataPinR + 3.0f)) {
            state.hoveringAnyPin = true;
            // ドロップで接続（型が合うときだけ自分自身への接続は不可）
            // つなげない場合も無反応にせず、理由をステータス表示する
            if (dataLinking_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                if (dataLinkFromNodeId_ == id) {
                    statusMessage_ = "自分自身のピンへは接続できません";
                    statusTimer_ = 3.0f;
                } else if (!TypesCompatible(dataLinkFromType_, paramType)) {
                    statusMessage_ = "型が違うため接続できません。同じ色のピン同士をつないでください";
                    statusTimer_ = 3.0f;
                } else {
                    RecordUndoSnapshotNow();
                    node.paramLinks[key] = dataLinkFromNodeId_;
                    dataLinking_ = false;
                    state.dataLinkCompletedThisFrame = true;
                }
            }
            // 右クリックで配線解除
            if (isLinked && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
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

void GraphEditor::DrawLinkPreviews(ImDrawList* dl, const CanvasFrameState& state)
{
    const float linkCtrl = kBaseLinkCtrl * zoom_;

    // ドラッグ中のリンクのプレビュー線
    if (linking_ && state.linkFromFound) {
        ImVec2 mouse = ImGui::GetIO().MousePos;
        ImVec2 c1(state.linkFromScreenPos.x + linkCtrl, state.linkFromScreenPos.y);
        ImVec2 c2(mouse.x - linkCtrl, mouse.y);
        dl->AddBezierCubic(state.linkFromScreenPos, c1, c2, mouse, kColLink, 2.5f);
    }
    if (linking_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && !state.linkCompletedThisFrame) {
        linking_ = false; // ピン以外の場所で離したらキャンセル
    }

    // ドラッグ中のデータ配線のプレビュー線（型の色で描く）
    if (dataLinking_ && state.dataLinkFromFound) {
        ImVec2 mouse = ImGui::GetIO().MousePos;
        ImVec2 c1(state.dataLinkFromScreenPos.x + linkCtrl, state.dataLinkFromScreenPos.y);
        ImVec2 c2(mouse.x - linkCtrl, mouse.y);
        dl->AddBezierCubic(state.dataLinkFromScreenPos, c1, c2, mouse, ColorForType(dataLinkFromType_), 2.0f);
    }
    if (dataLinking_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && !state.dataLinkCompletedThisFrame) {
        dataLinking_ = false; // ピン以外の場所で離したらキャンセル
    }
}

void GraphEditor::HandleCanvasShortcuts(const ImVec2& origin, bool canvasHovered, const CanvasFrameState& state)
{
    // 右クリックでノード追加メニュー（クリック位置をスクリーン座標からグラフ座標へ逆変換する）
    // ノード本体やピンの上で右クリックした場合はそちら側の専用メニューに譲る
    if (canvasHovered && !state.hoveringAnyPin && !state.hoveringNode && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
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
        && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        selectedNodeId_.clear();
        selectedCommentId_.clear();
    }

    // Deleteキーで選択中のノード／コメントを削除
    if (canvasHovered && ImGui::IsKeyPressed(ImGuiKey_Delete)) {
        if (!selectedNodeId_.empty()) {
            DeleteNode(selectedNodeId_);
            selectedNodeId_.clear();
        } else if (!selectedCommentId_.empty()) {
            DeleteComment(selectedCommentId_);
            selectedCommentId_.clear();
        }
    }
}

void GraphEditor::DrawLinks(ImDrawList* dl)
{
    const float pinPad = kBasePinPad * zoom_;
    const float linkCtrl = kBaseLinkCtrl * zoom_;

    // 同じフレームでDrawCanvas()が埋めたnodeRects_（実際の描画結果の矩形）を使うので、
    // パラメータ数によるノードの高さ変化を近似せずに正確なピン位置で結線できる
    auto pinPosFor = [&](const std::string& id, const char* pinKind) -> std::optional<ImVec2> {
        auto it = nodeRects_.find(id);
        if (it == nodeRects_.end()) {
            return std::nullopt;
        }
        const ImVec2& nMin = it->second.first;
        const ImVec2& nMax = it->second.second;
        if (std::strcmp(pinKind, "in") == 0) {
            return ImVec2(nMin.x - pinPad, nMin.y + 10.0f * zoom_);
        }
        if (std::strcmp(pinKind, "true") == 0) {
            return ImVec2(nMax.x + pinPad, nMin.y + (nMax.y - nMin.y) * 0.33f);
        }
        if (std::strcmp(pinKind, "false") == 0) {
            return ImVec2(nMax.x + pinPad, nMin.y + (nMax.y - nMin.y) * 0.66f);
        }
        return ImVec2(nMax.x + pinPad, nMin.y + 10.0f * zoom_); // "next"
    };

    auto drawLink = [&](const std::string& fromId, const char* fromPin, const std::string& toId) {
        if (toId.empty()) {
            return;
        }
        std::optional<ImVec2> from = pinPosFor(fromId, fromPin);
        std::optional<ImVec2> to = pinPosFor(toId, "in");
        if (!from || !to) {
            return;
        }
        ImVec2 c1(from->x + linkCtrl, from->y);
        ImVec2 c2(to->x - linkCtrl, to->y);
        dl->AddBezierCubic(*from, c1, c2, *to, kColLink, 2.5f);
    };

    for (const auto& [id, node] : graph_.nodes) {
        if (node.type == "If") {
            drawLink(id, "true", node.nextTrue);
            drawLink(id, "false", node.nextFalse);
        } else {
            drawLink(id, "next", node.next);
        }
    }
}

void GraphEditor::DrawDataLinks(ImDrawList* dl)
{
    const float linkCtrl = kBaseLinkCtrl * zoom_;

    for (const auto& [id, node] : graph_.nodes) {
        auto inIt = dataInPins_.find(id);
        if (inIt == dataInPins_.end()) {
            continue;
        }

        for (const auto& [key, srcId] : node.paramLinks) {
            auto toIt = inIt->second.find(key);
            auto fromIt = dataOutPins_.find(srcId);
            if (toIt == inIt->second.end() || fromIt == dataOutPins_.end()) {
                continue;
            }

            const ImVec2& from = fromIt->second;
            const ImVec2& to = toIt->second;
            ImVec2 c1(from.x + linkCtrl, from.y);
            ImVec2 c2(to.x - linkCtrl, to.y);
            dl->AddBezierCubic(from, c1, c2, to, ColorForType(ParamTypeOf(node.type, key)), 2.0f);
        }
    }
}

// ══════════════════════════════════════════════════════
// ノード生成
// ══════════════════════════════════════════════════════

void GraphEditor::AddNodeOfType(const std::string& type)
{
    std::string id = "node_" + std::to_string(nextNodeSerial_++);
    GraphNode node;
    node.id = id;
    node.type = type;
    node.editorX = pendingAddX_;
    node.editorY = pendingAddY_;

    // 型ごとの初期パラメータ（空だと編集の取っ掛かりが無いため最低限入れておく）
    if (type == "SetVariable") {
        node.params["name"] = std::string("var");
        node.params["value"] = 0.0f;
    } else if (type == "If") {
        node.params["var"] = std::string("var");
        node.params["op"] = std::string("==");
        node.params["value"] = 0.0f;
    } else if (type == "Wait") {
        node.params["seconds"] = 1.0f;
    } else if (type == "EmitEvent") {
        node.params["event"] = std::string("event_name");
    } else if (type == "SetFlag") {
        node.params["flag"] = std::string("flag_name");
        node.params["value"] = false;
    } else if (type == "GetFlag") {
        node.params["flag"] = std::string("flag_name");
        node.params["into"] = std::string("var");
    } else if (type == "Subgraph") {
        node.params["path"] = std::string("Resources/Graphs/sub_graph.json");
    } else if (type == "Math") {
        node.params["a"] = 0.0f;
        node.params["b"] = 0.0f;
        node.params["op"] = std::string("+");
    } else if (type == "Compare") {
        node.params["a"] = 0.0f;
        node.params["b"] = 0.0f;
        node.params["op"] = std::string("==");
    } else if (type == "Random") {
        node.params["min"] = 0.0f;
        node.params["max"] = 1.0f;
    } else if (type == "DamagePlayer" || type == "HealPlayer") {
        node.params["amount"] = 1.0f;
    } else if (type == "DamageEnemy" || type == "HealEnemy") {
        node.params["target"] = std::string("enemy");
        node.params["amount"] = 1.0f;
    } else if (type == "PlaySE" || type == "PlayBGM") {
        node.params["path"] = std::string("Resources/sound/se.wav");
        if (type == "PlayBGM") {
            node.params["loop"] = true;
        } else {
            node.params["volume"] = 1.0f;
        }
    } else if (type == "ScreenFlash") {
        node.params["r"] = 1.0f;
        node.params["g"] = 1.0f;
        node.params["b"] = 1.0f;
        node.params["a"] = 0.5f;
        node.params["duration"] = 0.15f;
    } else if (type == "HitStop") {
        node.params["frames"] = 3.0f;
    } else if (type == "SetEnemyVisible") {
        node.params["target"] = std::string("enemy");
        node.params["visible"] = true;
    } else if (type == "TeleportEnemy") {
        node.params["target"] = std::string("enemy");
        node.params["x"] = 0.0f;
        node.params["y"] = 0.0f;
        node.params["z"] = 0.0f;
    } else if (type == "TeleportPlayer") {
        node.params["x"] = 0.0f;
        node.params["y"] = 0.0f;
        node.params["z"] = 0.0f;
    } else if (type == "And" || type == "Or") {
        node.params["a"] = false;
        node.params["b"] = false;
    } else if (type == "Not") {
        node.params["a"] = false;
    }

    RecordUndoSnapshotNow();
    graph_.nodes[id] = std::move(node);
    if (graph_.startNodeId.empty()) {
        graph_.startNodeId = id;
    }
    selectedNodeId_ = id;
    selectedCommentId_.clear();
}

void GraphEditor::DrawAddNodeMenu()
{
    if (ImGui::BeginPopup("AddNodePopup")) {
        // メニューを開いた直後は検索欄へフォーカスし、そのままタイプして絞り込めるようにする
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(200.0f);
        ImGui::InputTextWithHint("##nodeSearch", "検索...", nodeSearchBuf_, sizeof(nodeSearchBuf_));
        ImGui::Separator();

        auto drawItem = [&](const std::string& type) {
            bool clicked = ImGui::MenuItem(type.c_str());
            if (ImGui::IsItemHovered()) {
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
            bool anyMatch = false;
            for (const std::string& type : NodeRegistry::GetInstance()->GetRegisteredTypes()) {
                const NodeTypeSpec* spec = NodeRegistry::GetInstance()->FindSpec(type);
                bool match = ContainsCI(type, query)
                    || (spec && (ContainsCI(spec->category, query) || ContainsCI(spec->description, query)));
                if (match) {
                    drawItem(type);
                    anyMatch = true;
                }
            }
            if (!anyMatch) {
                ImGui::TextDisabled("該当なし");
            }
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

        auto itFreq = byCategory.find("よく使う");
        if (itFreq != byCategory.end()) {
            for (const std::string& type : itFreq->second) {
                drawItem(type);
            }
            byCategory.erase(itFreq);
            ImGui::Separator();
        }

        for (auto& [cat, types] : byCategory) {
            if (ImGui::BeginMenu(cat.c_str())) {
                for (const std::string& type : types) {
                    drawItem(type);
                }
                ImGui::EndMenu();
            }
        }

        ImGui::Separator();
        if (ImGui::MenuItem("コメント枠")) {
            RecordUndoSnapshotNow();
            std::string id = "comment_" + std::to_string(nextCommentSerial_++);
            GraphComment c;
            c.id = id;
            c.x = pendingAddX_;
            c.y = pendingAddY_;
            graph_.comments[id] = std::move(c);
            selectedCommentId_ = id;
            selectedNodeId_.clear();
        }
        ImGui::EndPopup();
    }
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

void GraphEditor::DrawComments(ImDrawList* dl, const ImVec2& origin)
{
    constexpr float kHeaderH = 20.0f;
    constexpr float kHandleSize = 10.0f;

    for (auto& [id, c] : graph_.comments) {
        ImGui::PushID(id.c_str());

        ImVec2 screenMin(origin.x + (panOffsetX_ + c.x) * zoom_, origin.y + (panOffsetY_ + c.y) * zoom_);
        ImVec2 screenMax(screenMin.x + c.w * zoom_, screenMin.y + c.h * zoom_);

        ImU32 baseCol = IM_COL32(
            static_cast<int>(c.colorR * 255.0f), static_cast<int>(c.colorG * 255.0f), static_cast<int>(c.colorB * 255.0f), 255);
        ImU32 fillCol = IM_COL32(static_cast<int>(c.colorR * 255.0f), static_cast<int>(c.colorG * 255.0f), static_cast<int>(c.colorB * 255.0f), 45);
        ImU32 borderCol = (id == selectedCommentId_) ? IM_COL32(255, 255, 255, 255) : baseCol;

        dl->AddRectFilled(screenMin, screenMax, fillCol, 4.0f * zoom_);
        dl->AddRect(screenMin, screenMax, borderCol, 4.0f * zoom_, 0, 2.0f * zoom_);

        // ヘッダー（ドラッグハンドル兼選択領域）
        ImGui::SetCursorScreenPos(screenMin);
        ImGui::InvisibleButton("##commentDrag", ImVec2(c.w * zoom_, kHeaderH * zoom_));
        if (ImGui::IsItemActivated()) {
            selectedCommentId_ = id;
            selectedNodeId_.clear();
            BeginUndoCapture();
        }
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            ImVec2 d = ImGui::GetIO().MouseDelta;
            c.x += d.x / zoom_;
            c.y += d.y / zoom_;
            MarkUndoDirty();
        }
        if (ImGui::IsItemDeactivated()) {
            CommitUndoCapture();
        }

        // テキスト編集欄（ヘッダーの下、ボックス幅いっぱい）
        ImGui::SetCursorScreenPos(ImVec2(screenMin.x + 4.0f, screenMin.y + kHeaderH * zoom_));
        char buf[256];
        strncpy_s(buf, c.text.c_str(), _TRUNCATE);
        ImGui::SetNextItemWidth(c.w * zoom_ - 8.0f);
        bool textChanged = ImGui::InputText("##commentText", buf, sizeof(buf));
        if (ImGui::IsItemActivated()) {
            BeginUndoCapture();
        }
        if (textChanged) {
            MarkUndoDirty();
            c.text = buf;
        }
        if (ImGui::IsItemDeactivated()) {
            CommitUndoCapture();
        }

        // リサイズハンドル（右下角）
        ImVec2 handleMin(screenMax.x - kHandleSize * zoom_, screenMax.y - kHandleSize * zoom_);
        ImGui::SetCursorScreenPos(handleMin);
        ImGui::InvisibleButton("##commentResize", ImVec2(kHandleSize * zoom_, kHandleSize * zoom_));
        if (ImGui::IsItemActivated()) {
            BeginUndoCapture();
        }
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            ImVec2 d = ImGui::GetIO().MouseDelta;
            c.w = (std::max)(80.0f, c.w + d.x / zoom_);
            c.h = (std::max)(60.0f, c.h + d.y / zoom_);
            MarkUndoDirty();
        }
        if (ImGui::IsItemDeactivated()) {
            CommitUndoCapture();
        }
        dl->AddRectFilled(handleMin, screenMax, borderCol);

        ImGui::PopID();
    }
}

void GraphEditor::DrawVariablesPanel()
{
    if (!testRuntime_.IsRunning()) {
        return;
    }

    ImGui::SetNextWindowPos(ImVec2(20.0f, 470.0f), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(300.0f, 220.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("グラフ変数（実行中）");
    ImGui::TextDisabled("実行中の変数一覧値を直接書き換えて上書きテストできます");
    ImGui::Separator();

    for (const auto& [name, value] : testRuntime_.GetVariables()) {
        ImGui::PushID(name.c_str());
        if (std::holds_alternative<float>(value)) {
            float f = std::get<float>(value);
            if (ImGui::InputFloat(name.c_str(), &f)) {
                testRuntime_.SetVariable(name, f);
            }
        } else if (std::holds_alternative<bool>(value)) {
            bool b = std::get<bool>(value);
            if (ImGui::Checkbox(name.c_str(), &b)) {
                testRuntime_.SetVariable(name, b);
            }
        } else {
            std::string s = std::get<std::string>(value);
            char buf[256];
            strncpy_s(buf, s.c_str(), _TRUNCATE);
            if (ImGui::InputText(name.c_str(), buf, sizeof(buf))) {
                testRuntime_.SetVariable(name, std::string(buf));
            }
        }
        ImGui::PopID();
    }
    ImGui::End();
}
#endif // USE_IMGUI
