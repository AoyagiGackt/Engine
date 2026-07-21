/**
 * @file GraphEditorServices.cpp
 * @brief グラフ編集の入力処理とノード表示を個別に実行する
 */
#ifdef USE_IMGUI
#include "GraphEditorServices.h"
#include "GraphEditor.h"
#include "EditorUI.h"
#include "GraphRuntime.h"
#include "Input.h"
#include "NodeRegistry.h"
#include "TimeManager.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <optional>
#include <vector>

namespace {
constexpr float kBaseNodeWidth = 200.0f;
constexpr float kBasePinRadius = 6.0f;
constexpr float kBasePinPad = 10.0f;
constexpr ImU32 kColBg = IM_COL32(45, 45, 55, 235);
constexpr ImU32 kColBgSel = IM_COL32(70, 70, 95, 235);
constexpr ImU32 kColBorder = IM_COL32(90, 90, 110, 255);
constexpr ImU32 kColStart = IM_COL32(90, 200, 120, 255);
constexpr ImU32 kColPinIn = IM_COL32(230, 230, 230, 255);
constexpr ImU32 kColPinOut = IM_COL32(230, 200, 90, 255);
constexpr ImU32 kColPinTrue = IM_COL32(90, 200, 120, 255);
constexpr ImU32 kColPinFalse = IM_COL32(210, 90, 90, 255);
constexpr ImU32 kColRunning = IM_COL32(255, 210, 60, 255);

ImU32 ColorForType(engine::game::GraphValueType type)
{
    switch (type) {
    case engine::game::GraphValueType::Float: return IM_COL32(110, 220, 110, 255);
    case engine::game::GraphValueType::Bool: return IM_COL32(220, 100, 100, 255);
    case engine::game::GraphValueType::String: return IM_COL32(200, 120, 220, 255);
    default: return IM_COL32(240, 240, 240, 255);
    }
}

bool IsHoveringCircle(const ImVec2& center, float radius)
{
    const ImVec2 mousePosition = ImGui::GetIO().MousePos;
    const float deltaX = mousePosition.x - center.x;
    const float deltaY = mousePosition.y - center.y;
    return deltaX * deltaX + deltaY * deltaY <= radius * radius;
}
} // namespace

namespace engine::game {
void GraphEditorInteraction::Update(GraphEditor& editor, engine::Input* input)
{
    bool wasVisible = editor.visible_;
    if (input && input->TriggerKey(DIK_F1)) {
        editor.visible_ = !editor.visible_;
    }
    if (editor.visible_ != wasVisible) {
        if (editor.visible_) {
            // 開いている間は背後のゲームを止めるTimeManagerのタイムスケールを流用する
            editor.savedTimeScale_ = TimeManager::GetInstance()->GetTimeScale();
            TimeManager::GetInstance()->SetTimeScale(0.0f);
        } else {
            TimeManager::GetInstance()->SetTimeScale(editor.savedTimeScale_);
        }
    }
    if (!editor.visible_) {
        return;
    }

    // エディタ内蔵のタイマー類は、開いている間はTimeManagerのdtが0になる（背後のゲームを止めるため）ので、
    // ImGuiが持つ実時間ベースのdt（io.DeltaTime）を使う
    const float realDt = ImGui::GetIO().DeltaTime;
    if (editor.statusTimer_ > 0.0f) {
        editor.statusTimer_ -= realDt;
    }

    // Ctrl系ショートカット（テキスト入力欄にフォーカスがある間はImGui自身の入力欄内編集に譲る）
    if (!ImGui::GetIO().WantTextInput) {
        ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)) {
            editor.Undo();
        } else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y)) {
            editor.Redo();
        } else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) {
            editor.Save();
        } else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D)) {
            // editor.DrawCanvas()のノード走査ループの外なので、ここでは直接複製してよい
            if (!editor.selectedNodeId_.empty() && editor.graph_.FindNode(editor.selectedNodeId_)) {
                editor.DuplicateNode(editor.selectedNodeId_);
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
    if (!editor.selectedNodeId_.empty()) {
        const GraphNode* selected = editor.graph_.FindNode(editor.selectedNodeId_);
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
    strncpy_s(pathBuf, editor.graphPath_.c_str(), _TRUNCATE);
    ImGui::SetNextItemWidth(360.0f);
    if (ImGui::InputText("##path", pathBuf, sizeof(pathBuf))) {
        editor.graphPath_ = pathBuf;
    }
    ImGui::SameLine();
    if (ImGui::Button("開く")) {
        // 未保存の編集がある時は黙って破棄せず、確認モーダルを挟む
        if (editor.dirty_) {
            editor.pendingConfirmOpenPath_ = editor.graphPath_;
            editor.requestConfirmOpen_ = true;
        } else {
            editor.Open(editor.graphPath_);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("保存 (Ctrl+S)")) {
        editor.Save();
    }
    if (editor.dirty_) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "未保存");
    }

    // Subgraphの開くボタン等、ウィンドウスコープ外で予約された分もここでモーダルを開く
    if (editor.requestConfirmOpen_) {
        ImGui::OpenPopup("グラフを開く確認");
        editor.requestConfirmOpen_ = false;
    }
    if (graphics::EditorUI::ConfirmModal("グラフを開く確認",
            "未保存の変更があります。\n変更を破棄して開き直しますか？",
            "破棄して開く")
        == graphics::EditorUI::ConfirmResult::Ok) {
        editor.Open(editor.pendingConfirmOpenPath_);
    }

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    bool hasSelection = !editor.selectedNodeId_.empty() && editor.graph_.FindNode(editor.selectedNodeId_);
    ImGui::BeginDisabled(!hasSelection);
    if (ImGui::Button("開始ノードに設定")) {
        editor.RecordUndoSnapshotNow();
        editor.graph_.startNodeId = editor.selectedNodeId_;
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("自動整列")) {
        editor.ArrangeNodes();
    }

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::BeginDisabled(!editor.history_.CanUndo());
    if (ImGui::Button("元に戻す (Ctrl+Z)")) {
        editor.Undo();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!editor.history_.CanRedo());
    if (ImGui::Button("やり直す (Ctrl+Y)")) {
        editor.Redo();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    if (!editor.testRuntime_.IsRunning()) {
        if (ImGui::Button("実行")) {
            editor.testRuntime_.Start(&editor.graph_);
            editor.testRuntimeRootPath_ = editor.graphPath_;
        }
    } else {
        if (ImGui::Button("停止")) {
            editor.testRuntime_ = GraphRuntime { };
        }
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.6f, 1.0f), "実行中...");
    }

    if (editor.statusTimer_ > 0.0f) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", editor.statusMessage_.c_str());
    }

    ImGui::Separator();

    editor.DrawCanvas();

    ImGui::End();

    editor.DrawVariablesPanel();

    // Subgraphノードの開くボタン（ノード走査ループ内で押される）はここでまとめて処理する
    if (!editor.pendingOpenPath_.empty()) {
        std::string path = editor.pendingOpenPath_;
        editor.pendingOpenPath_.clear();
        if (editor.dirty_) {
            // ここはウィンドウのIDスコープ外なので、モーダルは次フレームのツールバー側で開く
            editor.pendingConfirmOpenPath_ = path;
            editor.requestConfirmOpen_ = true;
        } else {
            editor.Open(path);
        }
    }

    if (editor.testRuntime_.IsRunning()) {
        editor.testRuntime_.Update(realDt);
    }

}

void GraphNodeRenderer::Draw(GraphEditor& editor, ImDrawList* dl, const ImVec2& origin,
    const std::string& id, GraphNode& node, void* opaqueState)
{
    auto& state = *static_cast<GraphEditor::CanvasFrameState*>(opaqueState);
    const float nodeW = kBaseNodeWidth * editor.zoom_;
    const float pinR = kBasePinRadius * editor.zoom_;
    const float pinPad = kBasePinPad * editor.zoom_;
    const float boxPad = 6.0f * editor.zoom_;

    ImGui::PushID(id.c_str());
    dl->ChannelsSetCurrent(1);

    ImVec2 nodeScreenPos(origin.x + (editor.panOffsetX_ + node.editorX) * editor.zoom_, origin.y + (editor.panOffsetY_ + node.editorY) * editor.zoom_);
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
        editor.selectedNodeId_ = id;
        editor.selectedCommentId_.clear();
        editor.BeginUndoCapture();
    }
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        ImVec2 d = ImGui::GetIO().MouseDelta;
        node.editorX += d.x / editor.zoom_;
        node.editorY += d.y / editor.zoom_;
        editor.MarkUndoDirty();
    }
    if (ImGui::IsItemDeactivated()) {
        editor.CommitUndoCapture();
    }
    ImGui::TextDisabled("%s", id.c_str());

    editor.DrawNodeParams(dl, id, node, nodeScreenPos, state);

    // Subgraphノードは中身をワンクリックで開けるようにする（editor.Open()はループ後に遅延実行）
    if (node.type == "Subgraph") {
        if (ImGui::SmallButton("サブグラフを開く")) {
            auto it = node.params.find("path");
            if (it != node.params.end()) {
                editor.pendingOpenPath_ = AsString(it->second);
            }
        }
    }

    ImGui::EndGroup();
    ImVec2 nodeMin = ImGui::GetItemRectMin();
    ImVec2 nodeMax = ImGui::GetItemRectMax();
    editor.nodeRects_[id] = { nodeMin, nodeMax };

    // タイトルやパラメータを含むノード全体で右クリックメニューを開く
    // ノード走査中にコンテナを変更しないよう、複製と削除はキャンバス描画後に遅延実行する
    const ImVec2 interactionMin(nodeMin.x - boxPad, nodeMin.y - boxPad * 0.67f);
    const ImVec2 interactionMax(nodeMax.x + boxPad, nodeMax.y + boxPad);
    if (ImGui::IsMouseHoveringRect(interactionMin, interactionMax)) {
        state.hoveringNode = true;
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            editor.selectedNodeId_ = id;
            editor.selectedCommentId_.clear();
            ImGui::OpenPopup("NodeContextMenu");
        }
    }
    if (ImGui::BeginPopup("NodeContextMenu")) {
        if (ImGui::MenuItem("開始ノードに設定")) {
            editor.RecordUndoSnapshotNow();
            editor.graph_.startNodeId = id;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("複製")) {
            editor.pendingDuplicateNodeId_ = id;
        }
        if (ImGui::MenuItem("削除")) {
            editor.pendingDeleteNodeId_ = id;
        }
        ImGui::EndPopup();
    }

    // 背景（枠・塗り、開始ノードは緑枠で強調）
    dl->ChannelsSetCurrent(0);
    ImU32 bg = (id == editor.selectedNodeId_) ? kColBgSel : kColBg;
    dl->AddRectFilled(ImVec2(nodeMin.x - boxPad, nodeMin.y - boxPad * 0.67f), ImVec2(nodeMax.x + boxPad, nodeMax.y + boxPad), bg, 4.0f * editor.zoom_);
    ImU32 border = (id == editor.graph_.startNodeId) ? kColStart : kColBorder;
    float borderThickness = 2.0f * editor.zoom_;
    if (state.viewingActiveGraph && id == state.activeRunNodeId) {
        border = kColRunning;
        borderThickness = 3.5f * editor.zoom_; // Run中は太くして一目で分かるようにする
    }
    dl->AddRect(ImVec2(nodeMin.x - boxPad, nodeMin.y - boxPad * 0.67f), ImVec2(nodeMax.x + boxPad, nodeMax.y + boxPad), border, 4.0f * editor.zoom_, 0, borderThickness);

    // 開始ノードは緑枠だけだと気づきにくいため、枠の上にラベルも出す
    if (id == editor.graph_.startNodeId) {
        ImVec2 labelPos(nodeMin.x - boxPad, nodeMin.y - boxPad * 0.67f - ImGui::GetFontSize() - 2.0f * editor.zoom_);
        dl->AddText(labelPos, kColStart, "開始");
    }

    dl->ChannelsSetCurrent(1);

    // 実行入力ピン（左上。タイトル行の高さに合わせる）
    ImVec2 inPin(nodeMin.x - pinPad, nodeMin.y + 10.0f * editor.zoom_);
    dl->AddCircleFilled(inPin, pinR, kColPinIn);
    if (IsHoveringCircle(inPin, pinR + 2.0f)) {
        state.hoveringAnyPin = true;
        if (editor.linking_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && editor.linkFromNodeId_ != id) {
            auto fromIt = editor.graph_.nodes.find(editor.linkFromNodeId_);
            if (fromIt != editor.graph_.nodes.end()) {
                editor.RecordUndoSnapshotNow();
                GraphNode& fromNode = fromIt->second;
                if (editor.linkFromPin_ == "next") {
                    fromNode.next = id;
                } else if (editor.linkFromPin_ == "true") {
                    fromNode.nextTrue = id;
                } else if (editor.linkFromPin_ == "false") {
                    fromNode.nextFalse = id;
                }
            }
            editor.linking_ = false;
            state.linkCompletedThisFrame = true;
        }
    }

    // 実行出力ピン（右上。Ifのみtrue/falseの2つ、他は1つ）
    auto drawOutputPin = [&](const char* pinKind, ImVec2 pos, ImU32 col) {
        dl->AddCircleFilled(pos, pinR, col);
        if (IsHoveringCircle(pos, pinR + 2.0f)) {
            state.hoveringAnyPin = true;
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !editor.linking_ && !editor.dataLinking_) {
                editor.linking_ = true;
                editor.linkFromNodeId_ = id;
                editor.linkFromPin_ = pinKind;
            }
        }
        if (editor.linking_ && editor.linkFromNodeId_ == id && editor.linkFromPin_ == pinKind) {
            state.linkFromScreenPos = pos;
            state.linkFromFound = true;
        }
    };

    if (node.type == "If") {
        drawOutputPin("true", ImVec2(nodeMax.x + pinPad, nodeMin.y + (nodeMax.y - nodeMin.y) * 0.33f), kColPinTrue);
        drawOutputPin("false", ImVec2(nodeMax.x + pinPad, nodeMin.y + (nodeMax.y - nodeMin.y) * 0.66f), kColPinFalse);
    } else {
        drawOutputPin("next", ImVec2(nodeMax.x + pinPad, nodeMin.y + 10.0f * editor.zoom_), kColPinOut);
    }

    // データ出力ピン（右下。値を出力するノードだけ）
    const NodeTypeSpec* spec = NodeRegistry::GetInstance()->FindSpec(node.type);
    if (spec && spec->hasOutput) {
        ImVec2 outPin(nodeMax.x + pinPad, nodeMax.y - 10.0f * editor.zoom_);
        editor.dataOutPins_[id] = outPin;

        float dataPinR = pinR * 0.75f;
        dl->AddCircleFilled(outPin, dataPinR, ColorForType(spec->outputType));
        if (IsHoveringCircle(outPin, dataPinR + 3.0f)) {
            state.hoveringAnyPin = true;
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !editor.linking_ && !editor.dataLinking_) {
                editor.dataLinking_ = true;
                editor.dataLinkFromNodeId_ = id;
                editor.dataLinkFromType_ = spec->outputType;
            }
        }
        if (editor.dataLinking_ && editor.dataLinkFromNodeId_ == id) {
            state.dataLinkFromScreenPos = outPin;
            state.dataLinkFromFound = true;
        }
    }

    ImGui::PopID();

}
} // namespace engine::game
#endif
