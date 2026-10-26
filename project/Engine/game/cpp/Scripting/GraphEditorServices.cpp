/**
 * @file GraphEditorServices.cpp
 * @brief グラフ編集の入力処理とノード表示を個別に実行する
 */
#ifdef USE_IMGUI
#include "GraphEditorServices.h"
#include "TextBufferCapacity.h"
#include "EditorUI.h"
#include "GraphEditor.h"
#include "GraphRuntime.h"
#include "Input.h"
#include "NodeRegistry.h"
#include "TimeManager.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <optional>
#include <vector>

namespace engine::game {
namespace {
/** @brief 入力欄の変更有無と単発操作かどうかの通知 */
struct GraphWidgetEdit {
    bool changed;
    bool discrete;
};
/** @brief 数値入力欄を表示する */
GraphWidgetEdit DrawValueWidget(const char* label, float& value) {
    return { ImGui::InputFloat(label, &value), false };
}
/** @brief 真偽値の切り替え欄を表示する */
GraphWidgetEdit DrawValueWidget(const char* label, bool& value) {
    return { ImGui::Checkbox(label, &value), true };
}
/** @brief 一時バッファで文字列を編集し、変更時に値へ反映する */
GraphWidgetEdit DrawValueWidget(const char* label, std::string& value) {
    char buffer[textBufferCapacity::kPath];
    strncpy_s(buffer, value.c_str(), _TRUNCATE);
    const bool changed = ImGui::InputText(label, buffer, sizeof(buffer));
    if (changed) {
        value = buffer;
    }
    return { changed, false };
}
} // namespace

/** @brief 型別の入力を共通の通知へまとめ、元の値を変更せず結果を返す */
GraphValueEditResult DrawGraphValueInput(const char* label, const GraphValue& value) {
    GraphValueEditResult result { value };
    const GraphWidgetEdit edit = std::visit([label](auto& current) {
        return DrawValueWidget(label, current);
    }, result.value);
    result.changed = edit.changed;
    result.discrete = edit.discrete;
    result.activated = ImGui::IsItemActivated();
    result.deactivated = ImGui::IsItemDeactivated();
    return result;
}

/** @brief 値型に対応するピン色を返す */
ImU32 GraphEditorDrawingStyle::ColorForType(GraphValueType type) {
    // GraphValueType の並び順
    static constexpr std::array<ImU32, 4> kTypeColors = {
        IM_COL32(110, 220, 110, 255), // Float
        IM_COL32(220, 100, 100, 255), // Bool
        IM_COL32(200, 120, 220, 255), // String
        IM_COL32(240, 240, 240, 255), // Any
    };
    const size_t index = static_cast<size_t>(type);
    return index < kTypeColors.size() ? kTypeColors[index] : kTypeColors.back();
}

/** @brief マウス座標が円形ピンの判定範囲内か調べる */
bool GraphEditorDrawingStyle::IsHoveringCircle(const ImVec2& center, float radius) {
    const ImVec2 mousePosition = ImGui::GetIO().MousePos;
    const float deltaX = mousePosition.x - center.x;
    const float deltaY = mousePosition.y - center.y;
    return deltaX * deltaX + deltaY * deltaY <= radius * radius;
}
/** @brief F1 でエディタの表示を切り替え、開いている間はゲームの時間を止める（表示中は true） */
bool GraphEditorInteraction::PrepareFrame(GraphEditor& editor, engine::Input* input, float& realDeltaTime) {

    bool wasVisible = editor.visible_;
    if (input && input->TriggerKey(DIK_F1)) {
        editor.visible_ = !editor.visible_;
    }
    if (editor.visible_ != wasVisible) {
        if (editor.visible_) {
            // 開いている間は背後のゲームを止める（TimeManager のタイムスケールを流用する）
            editor.savedTimeScale_ = TimeManager::GetInstance()->GetTimeScale();
            TimeManager::GetInstance()->SetTimeScale(0.0f);
        } else {
            TimeManager::GetInstance()->SetTimeScale(editor.savedTimeScale_);
        }
    }
    if (!editor.visible_) {
        return false;
    }

    // エディタ内蔵のタイマー類は、開いている間はTimeManagerのdtが0になる（背後のゲームを止めるため）ので、
    // ImGuiが持つ実時間ベースのdt（io.DeltaTime）を使う
    realDeltaTime = ImGui::GetIO().DeltaTime;
    if (editor.statusTimer_ > 0.0f) {
        editor.statusTimer_ -= realDeltaTime;
    }

    // Ctrl系ショートカット（テキスト入力欄にフォーカスがある間はImGui自身の入力欄内編集に譲る）
    if (!ImGui::GetIO().WantTextInput) {
        /** @brief 修飾キー付きのショートカットと実行操作の対応 */
        struct CtrlShortcut {
            ImGuiKey key;
            void (*action)(GraphEditor&);
        };
        static constexpr CtrlShortcut kCtrlShortcuts[] = {
            {
                ImGuiKey_Z, [](GraphEditor& e) {
                    e.Undo();
                }
            },
            {
                ImGuiKey_Y, [](GraphEditor& e) {
                    e.Redo();
                }
            },
            {
                ImGuiKey_S, [](GraphEditor& e) {
                    e.Save();
                }
            },
            // editor.DrawCanvas()のノード走査ループの外なので、ここでは直接複製してよい
            { ImGuiKey_D, [](GraphEditor& e) {
                 if (!e.selectedNodeId_.empty() && e.graph_.FindNode(e.selectedNodeId_)) {
                     e.DuplicateNode(e.selectedNodeId_);
                 }
             } },
        };
        if (ImGui::GetIO().KeyCtrl) {
            for (const CtrlShortcut& shortcut : kCtrlShortcuts) {
                if (ImGui::IsKeyPressed(shortcut.key)) {
                    shortcut.action(editor);
                    break; // 1フレームに実行するのは1つだけ
                }
            }
        }
    }
    return true;
}

/** @brief 操作の案内を出す */
void GraphEditorInteraction::DrawGuidance(GraphEditor& editor) {
    // 基本操作の1行案内と、折りたためる使い方一覧
    ImGui::TextDisabled("F1: 表示/非表示    右クリック: ノード/コメント追加    ホイール: ズーム    中ドラッグ: パン");
    if (ImGui::CollapsingHeader("使い方")) {
        // ノードの追加と探し方
        ImGui::BulletText("何もない所を右クリック → ノードやコメント枠を追加（よく使う物は直接、他はジャンル別サブメニューに分類）");
        ImGui::BulletText("追加メニュー上部の検索欄に入力すると、名前・ジャンル・説明で絞り込める");
        ImGui::BulletText("ノードにマウスを乗せると、何ができるノードか説明がツールチップで出る");
        ImGui::BulletText("Ctrl+Z: 元に戻す  Ctrl+Y: やり直す  Ctrl+S: 保存  Ctrl+D: 選択ノードを複製");
        // 実行の線と値の線のつなぎ方
        ImGui::BulletText("実行の流れ: ノード右上の黄ピンを、次ノード左上の白ピンへドラッグして接続");
        ImGui::BulletText("Ifノードは緑ピン=条件が真のとき、赤ピン=偽のときの行き先");
        ImGui::BulletText("値の受け渡し: ノード右下の出力ピンを、パラメータ左の同じ色のピンへドラッグ");
        ImGui::BulletText("　ピンの色 = 型: 緑=数値  赤=ON/OFF  紫=文字列  白=なんでも接続可");
        ImGui::BulletText("パラメータ左のピンを右クリック → データ配線を解除");
        // ノードの移動・削除と、テスト実行・ほかの機能との連携
        ImGui::BulletText("タイトルをドラッグ: ノード移動    ノード選択+Deleteキー: 削除");
        ImGui::BulletText("何もない所を左クリック → 選択解除");
        ImGui::BulletText("実行ボタンでこのグラフをその場でテスト（黄色い枠 = 今実行中のノード）");
        ImGui::BulletText("Subgraphノード: 別のグラフJSONを部品として呼び出す（カプセル化）");
        ImGui::BulletText("フラグはステージエディタ(F2)のトリガーと共有（GetFlag/SetFlagで読み書き）");
    }
    // 初めて使う人向けに、イベントを1本作る手順を番号付きで出す
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
    // ノードを選んでいる時は、その種類の説明とパラメーターごとの意味を出す
    if (!editor.selectedNodeId_.empty()) {
        // 選択中の id のノードが消えていたり、種類が未登録なら説明は出さない
        const GraphNode* selected = editor.graph_.FindNode(editor.selectedNodeId_);
        const NodeTypeSpec* selectedSpec = selected
            ? NodeRegistry::GetInstance()->FindSpec(selected->type)
            : nullptr;
        if (selected && selectedSpec && ImGui::CollapsingHeader("選択中ノードの説明", ImGuiTreeNodeFlags_DefaultOpen)) {
            // 種類名と分類、ノード全体の説明
            ImGui::Text("%s  %s", selected->type.c_str(), selectedSpec->category.c_str());
            ImGui::TextWrapped("%s", selectedSpec->description.c_str());
            /** @brief ノードのパラメーター名と編集時に表示する説明の対応 */
            struct ParamHelp {
                const char* key;
                const char* text;
            };
            static constexpr ParamHelp kParamHelps[] = {
                { "flag", "ステージエディタのトリガーと共有するフラグ名" },
                { "name", "グラフ内で値を識別するための変数名" },
                { "var", "グラフ内で値を識別するための変数名" },
                { "into", "グラフ内で値を識別するための変数名" },
                { "value", "保存、比較、または設定する値" },
                { "seconds", "処理を待つ時間  単位は秒" },
                { "duration", "処理を待つ時間  単位は秒" },
                { "frames", "処理を止める時間  単位はフレーム" },
                { "path", "読み込むグラフ、音声などのファイルパス" },
                { "target", "ステージに配置した敵などの名前" },
                { "amount", "ダメージ量または回復量" },
                { "op", "計算または比較に使う記号" },
                { "x", "移動先または位置のワールド座標" },
                { "y", "移動先または位置のワールド座標" },
                { "z", "移動先または位置のワールド座標" },
                { "a", "計算や比較へ渡す入力値" },
                { "b", "計算や比較へ渡す入力値" },
                { "loop", "ONなら停止するまで繰り返し再生する" },
                { "visible", "ONなら表示し、OFFなら非表示にする" },
            };
            // パラメーター名から説明文を引く
            auto parameterHelp = [](const std::string& key) -> const char* {
                for (const ParamHelp& help : kParamHelps) {
                    if (key == help.key) {
                        return help.text;
                    }
                }
                return "ノードが処理に使用する入力値";
            };
            // 表に無いパラメーター名は汎用の説明を出す
            for (const NodeParamSpec& param : selectedSpec->params) {
                ImGui::BulletText("%s  %s", param.key.c_str(), parameterHelp(param.key));
            }
            // 値を出力するノードかどうかで、次のノードへの渡し方の案内を変える
            ImGui::TextDisabled(selectedSpec->hasOutput
                    ? "右下の色付きピンから処理結果を別ノードへ渡せます"
                    : "このノードは値を出力せず、上部の実行線だけで次へ進みます");
        }
    }
}

/** @brief ツールバーを出す */
void GraphEditorInteraction::DrawToolbar(GraphEditor& editor) {
    // 編集中のグラフのパスと、開く・保存
    char pathBuf[textBufferCapacity::kPath];
    strncpy_s(pathBuf, editor.graphPath_.c_str(), _TRUNCATE);
    ImGui::SetNextItemWidth(GraphEditorDrawingStyle::kGraphPathFieldWidth);
    // パスは入力した時点で書き換え、開くボタンで読み込む
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
    // 保存し忘れに気付けるよう、未保存の変更がある時だけ表示する
    if (editor.dirty_) {
        ImGui::SameLine();
        ImGui::TextColored(GraphEditorDrawingStyle::kUnsavedTextColor, "未保存");
    }

    // Subgraphの開くボタン等、ウィンドウスコープ外で予約された分もここでモーダルを開く
    if (editor.requestConfirmOpen_) {
        ImGui::OpenPopup("グラフを開く確認");
        editor.requestConfirmOpen_ = false;
    }
    // 確認で破棄を選んだ時だけ、予約したパスを開く
    if (graphics::editorUI::ConfirmModal("グラフを開く確認",
            "未保存の変更があります。\n変更を破棄して開き直しますか？",
            "破棄して開く")
        == graphics::editorUI::ConfirmResult::kOk) {
        editor.Open(editor.pendingConfirmOpenPath_);
    }

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    // 選択中のノードを開始ノードにする・自動整列
    bool hasSelection = !editor.selectedNodeId_.empty() && editor.graph_.FindNode(editor.selectedNodeId_);
    ImGui::BeginDisabled(!hasSelection);
    if (ImGui::Button("開始ノードに設定")) {
        // 元に戻せるよう、変える前に履歴を取る
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
    // 元に戻す・やり直す（戻せる履歴が無い間はボタンを押せなくする）
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
    // このグラフをその場で試しに動かす・止める
    if (!editor.testRuntime_.IsRunning()) {
        if (ImGui::Button("実行")) {
            // 実行中のノードを光らせる時に、今見ているグラフが実行中のものか比べるためパスを覚えておく
            editor.testRuntime_.Start(&editor.graph_);
            editor.testRuntimeRootPath_ = editor.graphPath_;
        }
    } else {
        if (ImGui::Button("停止")) {
            // 実行状態を作り直して、途中の待ちや変数も捨てる
            editor.testRuntime_ = GraphRuntime { };
        }
        ImGui::SameLine();
        ImGui::TextColored(GraphEditorDrawingStyle::kRunningTextColor, "実行中...");
    }

    if (editor.statusTimer_ > 0.0f) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", editor.statusMessage_.c_str());
    }
}

/** @brief フレームの最後に、ノードの操作で予約した処理（サブグラフを開くなど）をまとめて行う */
void GraphEditorInteraction::FinishFrame(GraphEditor& editor, float realDeltaTime) {
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
        editor.testRuntime_.Update(realDeltaTime);
    }
}

/** @brief 1フレーム分の編集入力を処理する */
void GraphEditorInteraction::Update(GraphEditor& editor, engine::Input* input) {
    float realDeltaTime = 0.0f;
    if (!PrepareFrame(editor, input, realDeltaTime)) {
        return;
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

    DrawGuidance(editor);
    DrawToolbar(editor);
    ImGui::Separator();
    editor.DrawCanvas();
    ImGui::End();
    editor.DrawVariablesPanel();
    FinishFrame(editor, realDeltaTime);
}

/** @brief ノード一件の表示と操作を処理する */
void GraphNodeRenderer::Draw(GraphEditor& editor, ImDrawList* dl, const ImVec2& origin,
    const std::string& id, GraphNode& node, void* opaqueState) {
    // ノードの大きさとピンの位置はズームに合わせる
    auto& state = *static_cast<GraphEditor::CanvasFrameState*>(opaqueState);
    const float nodeW = GraphEditorDrawingStyle::kBaseNodeWidth * editor.zoom_;
    const float pinR = GraphEditorDrawingStyle::kBasePinRadius * editor.zoom_;
    const float pinPad = GraphEditorDrawingStyle::kBasePinPad * editor.zoom_;
    const float boxPad = GraphEditorDrawingStyle::kNodeBoxPadding * editor.zoom_;

    ImGui::PushID(id.c_str());
    dl->ChannelsSetCurrent(1);

    ImVec2 nodeScreenPos(origin.x + (editor.panOffsetX_ + node.editorX) * editor.zoom_, origin.y + (editor.panOffsetY_ + node.editorY) * editor.zoom_);
    ImGui::SetCursorScreenPos(nodeScreenPos);
    ImGui::BeginGroup();

    const NodeTypeSpec* spec = NodeRegistry::GetInstance()->FindSpec(node.type);

    // タイトル（このボタン自体がドラッグハンドル兼用）
    ImGui::Button(node.type.c_str(), ImVec2(nodeW, 0));
    if (ImGui::IsItemHovered()) {
        state.hoveringNode = true; // キャンバス右クリックのノード追加メニューと衝突しないよう抑制する
        if (spec && !spec->description.empty()) {
            ImGui::SetTooltip("%s", spec->description.c_str());
        }
    }
    // タイトルを掴んだら選択し、ドラッグした分だけノードを動かす（離した時に1回分の履歴にする）
    if (ImGui::IsItemActivated()) {
        editor.selectedNodeId_ = id;
        editor.selectedCommentId_.clear();
    }
    const bool moving = ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left);
    if (moving) {
        const ImVec2 d = ImGui::GetIO().MouseDelta;
        node.editorX += d.x / editor.zoom_;
        node.editorY += d.y / editor.zoom_;
    }
    editor.CaptureItemUndo(moving);
    ImGui::TextDisabled("%s", id.c_str());

    // パラメータの入力欄とデータ入力ピン
    editor.DrawNodeParams(dl, id, node, nodeScreenPos, state);

    // 別グラフを参照するノードは中身をワンクリックで開けるようにする（editor.Open()はループ後に遅延実行）
    if (spec && !spec->openPathParam.empty()) {
        if (ImGui::SmallButton("サブグラフを開く")) {
            auto it = node.params.find(spec->openPathParam);
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
    // ノードの外枠（パラメータまで含む矩形に余白を足したもの）。右クリックの判定と背景・枠の描画で共通に使う
    const ImVec2 interactionMin(nodeMin.x - boxPad, nodeMin.y - boxPad * GraphEditorDrawingStyle::kNodeTopPaddingRatio);
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
    ImU32 bg = (id == editor.selectedNodeId_) ? GraphEditorDrawingStyle::kSelectedBackgroundColor : GraphEditorDrawingStyle::kBackgroundColor;
    dl->AddRectFilled(interactionMin, interactionMax, bg, GraphEditorDrawingStyle::kNodeCornerRadius * editor.zoom_);
    ImU32 border = (id == editor.graph_.startNodeId) ? GraphEditorDrawingStyle::kStartColor : GraphEditorDrawingStyle::kBorderColor;
    float borderThickness = GraphEditorDrawingStyle::kNodeBorderThickness * editor.zoom_;
    if (state.viewingActiveGraph && id == state.activeRunNodeId) {
        border = GraphEditorDrawingStyle::kRunningColor;
        borderThickness = GraphEditorDrawingStyle::kRunningBorderThickness * editor.zoom_; // Run中は太くして一目で分かるようにする
    }
    dl->AddRect(interactionMin, interactionMax, border, GraphEditorDrawingStyle::kNodeCornerRadius * editor.zoom_, 0, borderThickness);

    // 開始ノードは緑枠だけだと気づきにくいため、枠の上にラベルも出す
    if (id == editor.graph_.startNodeId) {
        ImVec2 labelPos(nodeMin.x - boxPad, nodeMin.y - boxPad * GraphEditorDrawingStyle::kNodeTopPaddingRatio - ImGui::GetFontSize() - GraphEditorDrawingStyle::kNodeLabelGap * editor.zoom_);
        dl->AddText(labelPos, GraphEditorDrawingStyle::kStartColor, "開始");
    }

    dl->ChannelsSetCurrent(1);

    // 実行入力ピン（左上。タイトル行の高さに合わせる）
    ImVec2 inPin(nodeMin.x - pinPad, nodeMin.y + GraphEditorDrawingStyle::kPinRowOffset * editor.zoom_);
    dl->AddCircleFilled(inPin, pinR, GraphEditorDrawingStyle::kInputPinColor);
    if (GraphEditorDrawingStyle::IsHoveringCircle(inPin, pinR + GraphEditorDrawingStyle::kExecPinHoverPadding)) {
        state.hoveringAnyPin = true;
        if (editor.linking_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && editor.linkFromNodeId_ != id) {
            auto fromIt = editor.graph_.nodes.find(editor.linkFromNodeId_);
            if (fromIt != editor.graph_.nodes.end()) {
                editor.RecordUndoSnapshotNow();
                GraphNode& fromNode = fromIt->second;
                for (const ExecPinDef& pin : kExecPins) {
                    if (editor.linkFromPin_ == pin.name) {
                        fromNode.*pin.target = id;
                    }
                }
            }
            editor.linking_ = false;
            state.linkCompletedThisFrame = true;
        }
    }

    // 実行出力ピン（右上。Ifのみtrue/falseの2つ、他は1つ）
    auto drawOutputPin = [&](const char* pinKind, ImVec2 pos, ImU32 col) {
        dl->AddCircleFilled(pos, pinR, col);
        if (GraphEditorDrawingStyle::IsHoveringCircle(pos, pinR + GraphEditorDrawingStyle::kExecPinHoverPadding)) {
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

    if (spec && spec->branches) {
        drawOutputPin("true", ImVec2(nodeMax.x + pinPad, nodeMin.y + (nodeMax.y - nodeMin.y) * GraphEditorDrawingStyle::kTruePinHeightRatio), GraphEditorDrawingStyle::kTruePinColor);
        drawOutputPin("false", ImVec2(nodeMax.x + pinPad, nodeMin.y + (nodeMax.y - nodeMin.y) * GraphEditorDrawingStyle::kFalsePinHeightRatio), GraphEditorDrawingStyle::kFalsePinColor);
    } else {
        drawOutputPin("next", ImVec2(nodeMax.x + pinPad, nodeMin.y + GraphEditorDrawingStyle::kPinRowOffset * editor.zoom_), GraphEditorDrawingStyle::kOutputPinColor);
    }

    // データ出力ピン（右下。値を出力するノードだけ）
    if (spec && spec->hasOutput) {
        ImVec2 outPin(nodeMax.x + pinPad, nodeMax.y - GraphEditorDrawingStyle::kPinRowOffset * editor.zoom_);
        editor.dataOutPins_[id] = outPin;

        float dataPinR = pinR * GraphEditorDrawingStyle::kDataPinRadiusRatio;
        dl->AddCircleFilled(outPin, dataPinR, GraphEditorDrawingStyle::ColorForType(spec->outputType));
        if (GraphEditorDrawingStyle::IsHoveringCircle(outPin, dataPinR + GraphEditorDrawingStyle::kPinHoverPadding)) {
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
