/**
 * @file GraphEditorPalette.cpp
 * @brief グラフエディターのノード追加・コメント・変数パネル
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
constexpr float kCommentBorderThickness = 2.0f;
constexpr float kCommentTextPadding = 4.0f;
constexpr float kCommentMinWidth = 80.0f;
constexpr float kCommentMinHeight = 60.0f;

// ASCIIの大文字小文字を無視した部分一致（日本語などマルチバイト部分はバイト列そのまま比較）
bool ContainsCI(const std::string& hay, const std::string& needle)
{
    if (needle.empty())
    {
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

/** @brief 指定した型のノードを、ノード定義の初期パラメータを入れて追加位置に1つ作る */
void GraphEditor::AddNodeOfType(const std::string& type)
{
    std::string id = "node_" + std::to_string(nextNodeSerial_++);
    GraphNode node;
    node.id = id;
    node.type = type;
    node.editorX = pendingAddX_;
    node.editorY = pendingAddY_;

    // 初期パラメータはノード定義（NodeRegistryの表）から入れる（空だと編集の取っ掛かりが無いため）
    if (const NodeTypeSpec* spec = NodeRegistry::GetInstance()->FindSpec(type))
    {
        for (const NodeParamSpec& param : spec->params)
        {
            node.params[param.key] = param.InitialValue();
        }
    }

    RecordUndoSnapshotNow();
    graph_.nodes[id] = std::move(node);
    if (graph_.startNodeId.empty())
    {
        graph_.startNodeId = id;
    }
    selectedNodeId_ = id;
    selectedCommentId_.clear();
}

/** @brief ノードを追加するメニューを出す */
void GraphEditor::DrawAddNodeMenu()
{
    if (ImGui::BeginPopup("AddNodePopup"))
    {
        // メニューを開いた直後は検索欄へフォーカスし、そのままタイプして絞り込めるようにする
        if (ImGui::IsWindowAppearing())
        {
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(kNodeSearchWidth);
        ImGui::InputTextWithHint("##nodeSearch", "検索...", nodeSearchBuf_, sizeof(nodeSearchBuf_));
        ImGui::Separator();

        // メニュー項目を出し、マウスを乗せたら説明、押したらそのノードを追加する
        auto drawItem = [&](const std::string& type) {
            bool clicked = ImGui::MenuItem(type.c_str());
            if (ImGui::IsItemHovered())
            {
                const NodeTypeSpec* spec = NodeRegistry::GetInstance()->FindSpec(type);
                if (spec && !spec->description.empty())
                {
                    ImGui::SetTooltip("%s", spec->description.c_str());
                }
            }
            if (clicked)
            {
                AddNodeOfType(type);
            }
        };

        if (nodeSearchBuf_[0] != '\0')
        {
            // 検索中はカテゴリ分けをやめ、名前・ジャンル・説明のどれかに一致した物をフラットに並べる
            const std::string query = nodeSearchBuf_;
            bool anyMatch = false;
            for (const std::string& type : NodeRegistry::GetInstance()->GetRegisteredTypes())
            {
                const NodeTypeSpec* spec = NodeRegistry::GetInstance()->FindSpec(type);
                bool match = ContainsCI(type, query)
                    || (spec && (ContainsCI(spec->category, query) || ContainsCI(spec->description, query)));
                if (match)
                {
                    drawItem(type);
                    anyMatch = true;
                }
            }
            if (!anyMatch)
            {
                ImGui::TextDisabled("該当なし");
            }
            ImGui::EndPopup();
            return;
        }

        // カテゴリごとにグループ化する（よく使う分類は直接、他はサブメニューにまとめる）
        std::map<std::string, std::vector<std::string>> byCategory;
        for (const std::string& type : NodeRegistry::GetInstance()->GetRegisteredTypes())
        {
            const NodeTypeSpec* spec = NodeRegistry::GetInstance()->FindSpec(type);
            std::string cat = (spec && !spec->category.empty()) ? spec->category : "その他";
            byCategory[cat].push_back(type);
        }

        auto itFreq = byCategory.find("よく使う");
        if (itFreq != byCategory.end())
        {
            for (const std::string& type : itFreq->second)
            {
                drawItem(type);
            }
            byCategory.erase(itFreq);
            ImGui::Separator();
        }

        for (auto& [cat, types] : byCategory)
        {
            if (ImGui::BeginMenu(cat.c_str()))
            {
                for (const std::string& type : types)
                {
                    drawItem(type);
                }
                ImGui::EndMenu();
            }
        }

        ImGui::Separator();
        // 右クリックした位置にコメント枠を作って選択する
        if (ImGui::MenuItem("コメント枠"))
        {
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

/** @brief コメント（注釈用の色付き矩形）の描画・ドラッグ移動・リサイズ・テキスト編集 */
void GraphEditor::DrawComments(ImDrawList* dl, const ImVec2& origin)
{
    constexpr float kHeaderH = 20.0f;
    constexpr float kHandleSize = 10.0f;

    for (auto& [id, c] : graph_.comments)
    {
        ImGui::PushID(id.c_str());

        ImVec2 screenMin(origin.x + (panOffsetX_ + c.x) * zoom_, origin.y + (panOffsetY_ + c.y) * zoom_);
        ImVec2 screenMax(screenMin.x + c.w * zoom_, screenMin.y + c.h * zoom_);

        // 枠と塗りはコメントの色を使い、透明度だけを変える（選択中は白い枠）
        auto commentColor = [&c](int alpha) {
            return IM_COL32(static_cast<int>(c.colorR * kColorChannelMax), static_cast<int>(c.colorG * kColorChannelMax),
                static_cast<int>(c.colorB * kColorChannelMax), alpha);
        };
        const ImU32 baseCol = commentColor(kCommentBorderAlpha);
        const ImU32 fillCol = commentColor(kCommentFillAlpha);
        constexpr ImU32 kSelectedBorderColor = IM_COL32(255, 255, 255, 255); // 選択中のコメントは白い枠
        const ImU32 borderCol = (id == selectedCommentId_) ? kSelectedBorderColor : baseCol;

        dl->AddRectFilled(screenMin, screenMax, fillCol, GraphEditorDrawingStyle::kNodeCornerRadius * zoom_);
        dl->AddRect(screenMin, screenMax, borderCol, GraphEditorDrawingStyle::kNodeCornerRadius * zoom_, 0, kCommentBorderThickness * zoom_);

        // ヘッダー（ドラッグハンドル兼選択領域）。掴んだら選択し、ドラッグした分だけ動かす
        ImGui::SetCursorScreenPos(screenMin);
        ImGui::InvisibleButton("##commentDrag", ImVec2(c.w * zoom_, kHeaderH * zoom_));
        if (ImGui::IsItemActivated())
        {
            selectedCommentId_ = id;
            selectedNodeId_.clear();
        }
        const bool moving = ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left);
        if (moving)
        {
            const ImVec2 d = ImGui::GetIO().MouseDelta;
            c.x += d.x / zoom_;
            c.y += d.y / zoom_;
        }
        CaptureItemUndo(moving);

        // テキスト編集欄（ヘッダーの下、ボックス幅いっぱい）
        ImGui::SetCursorScreenPos(ImVec2(screenMin.x + kCommentTextPadding, screenMin.y + kHeaderH * zoom_));
        char buf[TextBufferCapacity::kPath];
        strncpy_s(buf, c.text.c_str(), _TRUNCATE);
        ImGui::SetNextItemWidth(c.w * zoom_ - kCommentTextPadding * 2.0f);
        if (CaptureItemUndo(ImGui::InputText("##commentText", buf, sizeof(buf))))
        {
            c.text = buf;
        }

        // リサイズハンドル（右下角）。最小の大きさより小さくはしない
        ImVec2 handleMin(screenMax.x - kHandleSize * zoom_, screenMax.y - kHandleSize * zoom_);
        ImGui::SetCursorScreenPos(handleMin);
        ImGui::InvisibleButton("##commentResize", ImVec2(kHandleSize * zoom_, kHandleSize * zoom_));
        const bool resizing = ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left);
        if (resizing)
        {
            const ImVec2 d = ImGui::GetIO().MouseDelta;
            c.w = (std::max)(kCommentMinWidth, c.w + d.x / zoom_);
            c.h = (std::max)(kCommentMinHeight, c.h + d.y / zoom_);
        }
        CaptureItemUndo(resizing);
        dl->AddRectFilled(handleMin, screenMax, borderCol);

        ImGui::PopID();
    }
}

/** @brief Run中のグラフ変数を一覧表示し、その場で上書きテストできるパネル */
void GraphEditor::DrawVariablesPanel()
{
    if (!testRuntime_.IsRunning())
    {
        return;
    }

    ImGui::SetNextWindowPos(kVariablesWindowPos, ImGuiCond_Once);
    ImGui::SetNextWindowSize(kVariablesWindowSize, ImGuiCond_FirstUseEver);
    ImGui::Begin("グラフ変数（実行中）");
    ImGui::TextDisabled("実行中の変数一覧値を直接書き換えて上書きテストできます");
    ImGui::Separator();

    testRuntime_.VisitVariables([this](const std::string& name, const GraphValue& value)
    {
        ImGui::PushID(name.c_str());
        GraphValueEditResult edit = DrawGraphValueInput(name.c_str(), value);
        if (edit.changed)
        {
            testRuntime_.SetVariable(name, std::move(edit.value));
        }
        ImGui::PopID();
    });
    ImGui::End();
}

} // namespace engine::game
#endif
