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
#include "JsonHelper.h"
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

constexpr const char* kNodeLabelsPath = "Resources/Config/UI/node_labels.json";

/** @brief 読み込み済みの表示名の表（最初に引いた時に読む） */
nlohmann::json& NodeLabelTable() {
    static nlohmann::json sTable = JsonHelper::Load(kNodeLabelsPath);
    return sTable;
}

/** @brief ノード種別の項目を返す（無ければ null） */
const nlohmann::json* FindTypeLabels(const std::string& type) {
    const nlohmann::json& table = NodeLabelTable();
    if (!table.is_object() || !table.contains("types") || !table["types"].is_object()) {
        return nullptr;
    }
    const auto entry = table["types"].find(type);
    return entry != table["types"].end() && entry->is_object() ? &*entry : nullptr;
}
} // namespace

/** @brief ノード種別の表示名を返す（表に無ければ type 名のまま） */
std::string GraphNodeLabels::TypeName(const std::string& type) {
    const nlohmann::json* labels = FindTypeLabels(type);
    if (labels == nullptr) {
        return type;
    }
    const auto name = labels->find("name");
    return name != labels->end() && name->is_string() ? name->get<std::string>() : type;
}

/** @brief ノードのパラメータの表示名を返す（表に無ければパラメータ名のまま） */
std::string GraphNodeLabels::ParamName(const std::string& type, const std::string& param) {
    const nlohmann::json* labels = FindTypeLabels(type);
    if (labels == nullptr) {
        return param;
    }
    const auto params = labels->find("params");
    if (params == labels->end() || !params->is_object()) {
        return param;
    }
    const auto name = params->find(param);
    return name != params->end() && name->is_string() ? name->get<std::string>() : param;
}

/** @brief 表を読み直す */
void GraphNodeLabels::Reload() {
    NodeLabelTable() = JsonHelper::Load(kNodeLabelsPath);
}

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

/** @brief F1 でエディタの表示を切り替え、開いている間はゲームの時間を止める（表示中は true） */
bool GraphEditorInteraction::PrepareFrame(GraphEditor& editor, engine::Input* input, float& realDeltaTime) {

    bool wasVisible = editor.visible_;
    if (input && input->TriggerKey(DIK_F1)) {
        editor.visible_ = !editor.visible_;
    }
    if (editor.visible_ != wasVisible) {
        if (editor.visible_) {
            // 開くたびに日本語の表示名を読み直す（node_labels.json の書き換えを再起動せずに見られる）
            GraphNodeLabels::Reload();
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

    // 保存は入力欄の編集と競合しないので、入力中でも受け付ける
    if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        editor.Save();
    }

    // Ctrl系ショートカット（テキスト入力欄にフォーカスがある間はImGui自身の入力欄内編集に譲る）
    if (!ImGui::GetIO().WantTextInput) {
        /** @brief 修飾キー付きのショートカットと実行操作の対応 */
        struct CtrlShortcut {
            ImGuiKey key;
            void (*action)(GraphEditor&);
            bool repeats; ///< 押しっぱなしで繰り返すか
        };
        static constexpr CtrlShortcut kCtrlShortcuts[] = {
            {
                ImGuiKey_Z, [](GraphEditor& e) {
                    e.Undo();
                }, true
            },
            {
                ImGuiKey_Y, [](GraphEditor& e) {
                    e.Redo();
                }, true
            },
            // editor.DrawCanvas()のノード走査ループの外なので、ここでは直接複製してよい
            {
                ImGuiKey_D, [](GraphEditor& e) {
                    e.DuplicateSelectedNodes();
                }, false
            },
            {
                ImGuiKey_A, [](GraphEditor& e) {
                    e.SelectAllNodes();
                }, false
            },
        };
        if (ImGui::GetIO().KeyCtrl) {
            for (const CtrlShortcut& shortcut : kCtrlShortcuts) {
                if (ImGui::IsKeyPressed(shortcut.key, shortcut.repeats)) {
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
    // 折りたためる使い方一覧
    if (ImGui::CollapsingHeader("使い方")) {
        // ノードの追加と探し方
        ImGui::BulletText("何もない所を右クリック → ノードやコメント枠を追加（よく使う物は直接、他はジャンル別サブメニューに分類）");
        ImGui::BulletText("追加メニュー上部の検索欄に入力すると、名前・ジャンル・説明で絞り込める");
        ImGui::BulletText("ノードにマウスを乗せると、何ができるノードか説明がツールチップで出る");
        ImGui::BulletText("Ctrl+Z: 元に戻す  Ctrl+Y: やり直す  Ctrl+S: 保存  Ctrl+D: 選択ノードを複製  Ctrl+A: 全選択");
        ImGui::BulletText("F: 選択中のノード（無ければ全体）が収まるように表示");
        ImGui::BulletText("ホイール: カーソル位置を中心にズーム  中ドラッグかスペース+左ドラッグ: パン");
        // 実行の線と値の線のつなぎ方
        ImGui::BulletText("実行の流れ: ノード右上の黄ピンを、次のノードへドラッグして接続（ノードのどこに落としてもよい）");
        ImGui::BulletText("配線中は接続できるピンに輪が付き、近づくと吸着する");
        ImGui::BulletText("Ifノードは緑ピン=条件が真のとき、赤ピン=偽のときの行き先");
        ImGui::BulletText("値の受け渡し: ノード右下の出力ピンを、パラメータ左の同じ色のピンへドラッグ");
        ImGui::BulletText("　ピンの色 = 型: 緑=数値  赤=ON/OFF  紫=文字列  白=なんでも接続可");
        ImGui::BulletText("線かピンを右クリック → 配線を解除");
        ImGui::BulletText("つながったパラメータ左のピンをドラッグ → 線を抜いて別の場所へつなぎ替え（何もない所で離すと切断）");
        // ノードの移動・削除と、テスト実行・ほかの機能との連携
        ImGui::BulletText("タイトルかノードの余白をドラッグ: 選択中のノードをまとめて移動（Ctrlで格子に吸着）");
        ImGui::BulletText("Delete: 選択中のノードを削除");
        ImGui::BulletText("何もない所をドラッグ: 範囲選択（Ctrl/Shiftで追加）  クリック: 選択解除");
        ImGui::BulletText("Ctrl/Shift+クリック: ノードを選択に追加、もう一度で外す");
        ImGui::BulletText("コメント枠は上の帯をドラッグで移動、右下の角をドラッグで大きさ変更");
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
            ImGui::Text("%s (%s)  %s", GraphNodeLabels::TypeName(selected->type).c_str(), selected->type.c_str(),
                selectedSpec->category.c_str());
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
                { "text", "画面に出す文字" },
                { "scale", "ゲームを進める速さの倍率  1で通常" },
                { "count", "くり返す回数  0ならずっと" },
                { "interval", "次の回までの間隔  単位は秒" },
                { "weapon", "武器の種類名  Sword Spear Hammer Dagger Ball Greatsword Scythe Axe" },
                { "locked", "ONなら操作を止め、OFFなら戻す" },
                { "width", "範囲の横幅  ワールド座標の単位" },
                { "height", "範囲の高さ  ワールド座標の単位" },
                { "condition", "真偽を決める入力  CompareやAndの出力をつなぐ" },
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
                ImGui::BulletText("%s (%s)  %s", GraphNodeLabels::ParamName(selected->type, param.key).c_str(),
                    param.key.c_str(), parameterHelp(param.key));
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
        graphics::editorUI::DrawUnsavedMark();
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
    if (ImGui::Button("全体表示 (F)")) {
        editor.FrameNodes(false);
    }
    ImGui::SameLine();
    ImGui::Checkbox("説明", &editor.showHelpPanel_);

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

    // 説明の開閉でキャンバスが上下に動かないよう、説明は右側のパネルへ分ける
    DrawToolbar(editor);
    ImGui::TextDisabled("F1: 表示/非表示    右クリック: 追加/メニュー/配線解除    ホイール: ズーム    中ドラッグ: パン    左ドラッグ: 範囲選択    F: 全体表示");
    ImGui::Separator();
    editor.DrawCanvas();
    if (editor.showHelpPanel_) {
        ImGui::SameLine();
        ImGui::BeginChild("GraphHelpPanel", ImVec2(0.0f, 0.0f), true);
        DrawGuidance(editor);
        ImGui::EndChild();
    }
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
    dl->ChannelsSetCurrent(GraphEditorDrawingStyle::kNodeFrontChannel);

    ImVec2 nodeScreenPos(origin.x + (editor.panOffsetX_ + node.editorX) * editor.zoom_, origin.y + (editor.panOffsetY_ + node.editorY) * editor.zoom_);
    ImGui::SetCursorScreenPos(nodeScreenPos);
    ImGui::BeginGroup();

    const NodeTypeSpec* spec = NodeRegistry::GetInstance()->FindSpec(node.type);

    // タイトル（このボタン自体がドラッグハンドル兼用）
    // 表示は日本語名、マウスを乗せるとグラフJSONに書く type 名と説明を出す
    const std::string title = GraphNodeLabels::TypeName(node.type) + "##title";
    ImGui::Button(title.c_str(), ImVec2(nodeW, 0));
    if (ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        if (spec && !spec->description.empty()) {
            ImGui::SetTooltip("%s\n%s", node.type.c_str(), spec->description.c_str());
        } else {
            ImGui::SetTooltip("%s", node.type.c_str());
        }
    }
    // タイトルを掴んだら選択して移動の準備をする（移動は全ノードを描いた後に UpdateNodeDrag で進める）
    if (ImGui::IsItemActivated() && !editor.panning_) {
        editor.BeginNodePress(id);
        state.nodePressedThisFrame = true;
    }
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

    // ノードの外枠（パラメータまで含む矩形に余白を足したもの）
    const ImVec2 interactionMin(nodeMin.x - boxPad, nodeMin.y - boxPad * GraphEditorDrawingStyle::kNodeTopPaddingRatio);
    const ImVec2 interactionMax(nodeMax.x + boxPad, nodeMax.y + boxPad);

    // 背景（枠・塗り、開始ノードは緑枠で強調）
    dl->ChannelsSetCurrent(GraphEditorDrawingStyle::kNodeBackChannel);
    ImU32 bg = editor.IsNodeSelected(id) ? GraphEditorDrawingStyle::kSelectedBackgroundColor : GraphEditorDrawingStyle::kBackgroundColor;
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

    dl->ChannelsSetCurrent(GraphEditorDrawingStyle::kNodeFrontChannel);

    // ピンは描いて記録するだけにする（掴む・つなぐ判定は全ピンが出そろった後に近い物を選ぶ）
    // 実行入力ピン（左上。タイトル行の高さに合わせる）
    ImVec2 inPin(nodeMin.x - pinPad, nodeMin.y + GraphEditorDrawingStyle::kPinRowOffset * editor.zoom_);
    dl->AddCircleFilled(inPin, pinR, GraphEditorDrawingStyle::kInputPinColor);
    state.pins.push_back({ GraphPinKind::kExecIn, id, GraphEditorDrawingStyle::kExecInPinName, inPin, pinR, GraphValueType::kAny });

    // 実行出力ピン（右上。Ifのみtrue/falseの2つ、他は1つ）
    auto drawOutputPin = [&](const char* pinKind, ImVec2 pos, ImU32 col) {
        dl->AddCircleFilled(pos, pinR, col);
        state.pins.push_back({ GraphPinKind::kExecOut, id, pinKind, pos, pinR, GraphValueType::kAny });
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

        float dataPinR = pinR * GraphEditorDrawingStyle::kDataPinRadiusRatio;
        dl->AddCircleFilled(outPin, dataPinR, GraphEditorDrawingStyle::ColorForType(spec->outputType));
        state.pins.push_back({ GraphPinKind::kDataOut, id, "", outPin, dataPinR, spec->outputType });
    }

    ImGui::PopID();
}
} // namespace engine::game
#endif
