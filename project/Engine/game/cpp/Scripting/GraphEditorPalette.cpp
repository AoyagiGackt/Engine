/**
 * @file GraphEditorPalette.cpp
 * @brief グラフエディターのノード追加・コメント・変数パネル
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

namespace {
constexpr float kBaseLinkCtrl = 60.0f; // ベジエ曲線の制御点オフセット
constexpr ImU32 kColLink = IM_COL32(220, 220, 220, 200);
constexpr float kNodeSearchWidth = 200.0f;
constexpr ImVec2 kVariablesWindowPos = { 20.0f, 470.0f };
constexpr ImVec2 kVariablesWindowSize = { 300.0f, 220.0f };

// コメント枠
constexpr float kColorChannelMax = 255.0f;
constexpr int kCommentBorderAlpha = 255;
constexpr int kCommentFillAlpha = 45;
constexpr float kCommentBorderThickness = 2.0f;
constexpr float kCommentTextPadding = 4.0f;
constexpr float kCommentMinWidth = 80.0f;
constexpr float kCommentMinHeight = 60.0f;

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

} // namespace
// ══════════════════════════════════════════════════════
// キャンバスとノード描画
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
        node.params["a"] = GraphNodeDefaults::kFlashAlpha;
        node.params["duration"] = GraphNodeDefaults::kFlashDuration;
    } else if (type == "HitStop") {
        node.params["frames"] = GraphNodeDefaults::kHitStopFrames;
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
    } else if (type == "SetObjectVisible") {
        node.params["target"] = std::string("obj_0");
        node.params["visible"] = true;
    } else if (type == "SetObjectEnabled") {
        node.params["target"] = std::string("obj_0");
        node.params["enabled"] = true;
    } else if (type == "TeleportObject" || type == "MoveObject") {
        node.params["target"] = std::string("obj_0");
        node.params["x"] = 0.0f;
        node.params["y"] = 0.0f;
        node.params["z"] = 0.0f;
        if (type == "MoveObject") {
            node.params["seconds"] = 1.0f;
        }
    } else if (type == "ChangeScene") {
        node.params["scene"] = std::string("MAP");
        node.params["fadeOut"] = GraphNodeDefaults::kSceneFadeSeconds;
        node.params["fadeIn"] = GraphNodeDefaults::kSceneFadeSeconds;
    } else if (type == "SpawnEnemy") {
        node.params["target"] = std::string("spawn_0");
    } else if (type == "ShakeCamera") {
        node.params["amount"] = GraphNodeDefaults::kShakeAmount;
        node.params["seconds"] = GraphNodeDefaults::kShakeSeconds;
    } else if (type == "EmitRing") {
        node.params["group"] = std::string("hit_ring");
        node.params["x"] = 0.0f;
        node.params["y"] = 0.0f;
        node.params["z"] = 0.0f;
        node.params["radius"] = GraphNodeDefaults::kRingRadius;
        node.params["r"] = 1.0f;
        node.params["g"] = 1.0f;
        node.params["b"] = 1.0f;
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
        ImGui::SetNextItemWidth(kNodeSearchWidth);
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

void GraphEditor::DrawComments(ImDrawList* dl, const ImVec2& origin)
{
    constexpr float kHeaderH = 20.0f;
    constexpr float kHandleSize = 10.0f;

    for (auto& [id, c] : graph_.comments) {
        ImGui::PushID(id.c_str());

        ImVec2 screenMin(origin.x + (panOffsetX_ + c.x) * zoom_, origin.y + (panOffsetY_ + c.y) * zoom_);
        ImVec2 screenMax(screenMin.x + c.w * zoom_, screenMin.y + c.h * zoom_);

        ImU32 baseCol = IM_COL32(
            static_cast<int>(c.colorR * kColorChannelMax), static_cast<int>(c.colorG * kColorChannelMax),
            static_cast<int>(c.colorB * kColorChannelMax), kCommentBorderAlpha);
        ImU32 fillCol = IM_COL32(static_cast<int>(c.colorR * kColorChannelMax), static_cast<int>(c.colorG * kColorChannelMax),
            static_cast<int>(c.colorB * kColorChannelMax), kCommentFillAlpha);
        ImU32 borderCol = (id == selectedCommentId_) ? IM_COL32(255, 255, 255, 255) : baseCol;

        dl->AddRectFilled(screenMin, screenMax, fillCol, GraphEditorDrawingStyle::kNodeCornerRadius * zoom_);
        dl->AddRect(screenMin, screenMax, borderCol, GraphEditorDrawingStyle::kNodeCornerRadius * zoom_, 0, kCommentBorderThickness * zoom_);

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
        ImGui::SetCursorScreenPos(ImVec2(screenMin.x + kCommentTextPadding, screenMin.y + kHeaderH * zoom_));
        char buf[256];
        strncpy_s(buf, c.text.c_str(), _TRUNCATE);
        ImGui::SetNextItemWidth(c.w * zoom_ - kCommentTextPadding * 2.0f);
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
            c.w = (std::max)(kCommentMinWidth, c.w + d.x / zoom_);
            c.h = (std::max)(kCommentMinHeight, c.h + d.y / zoom_);
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

    ImGui::SetNextWindowPos(kVariablesWindowPos, ImGuiCond_Once);
    ImGui::SetNextWindowSize(kVariablesWindowSize, ImGuiCond_FirstUseEver);
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
#endif
