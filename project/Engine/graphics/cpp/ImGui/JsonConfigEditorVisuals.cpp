/**
 * @file JsonConfigEditorVisuals.cpp
 * @brief 調整値エディタの見た目の補助（行の縞模様・変えた項目の印と戻すボタン・色分けした見出し・ファイル一覧の印）
 */
#ifdef USE_IMGUI
#include "JsonConfigEditor.h"
#include "JsonConfigEditorShared.h"
#include <algorithm>
#include <cfloat>
#include <functional>

namespace engine::graphics {
namespace {
using namespace jsonConfigEditorShared;

constexpr int kHueSteps = 12; ///< 見出しの色の数（グループの名前から選ぶ）
constexpr float kHeaderSaturation = 0.45f; ///< 見出しの色の鮮やかさ
constexpr float kHeaderValue = 0.55f; ///< 見出しの色の明るさ
constexpr float kHeaderAlpha = 0.55f; ///< 見出しの色の濃さ
constexpr float kHeaderHoverBoost = 0.2f; ///< 見出しにカーソルを乗せた時に足す濃さ
constexpr int kHeaderColorCount = 3; ///< 見出しで差し替える色の数（ふだん・乗せた時・押した時）
constexpr float kFileMarkRadius = 4.0f; ///< ファイル一覧の、すぐ反映されるかの丸の半径
constexpr float kFileMarkWidth = 14.0f; ///< 丸のために空ける幅

/** @brief 数値だけの配列か（色やベクトルは1つの値として数える） */
bool IsNumberRow(const nlohmann::json& value) {
    return value.is_array() && !value.empty()
        && std::all_of(value.begin(), value.end(), [](const nlohmann::json& v) { return v.is_number(); });
}

/** @brief 説明用の "_" で始まるキーか */
bool IsHiddenKey(const std::string& key) {
    return key.empty() || key[0] == '_';
}
} // namespace

/** @brief 項目名を左側に出し、続く編集部品を右側に置く準備をする（変えた項目は色と印と戻すボタン） */
bool JsonConfigEditor::BeginRow(const std::string& label, const std::string& path, const nlohmann::json& value, const nlohmann::json* saved) {
    const float labelWidth = ImGui::GetContentRegionAvail().x * kLabelWidthRatio;
    const bool modified = saved == nullptr || *saved != value;
    // 1行おきに薄い背景を敷き、変えた項目は左端に線を引く（部品より先に描いて下に敷く）
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float rowHeight = ImGui::GetFrameHeight();
    const float rowWidth = ImGui::GetContentRegionAvail().x;
    if (rowIndex_++ % 2 == 1) {
        drawList->AddRectFilled(start, { start.x + rowWidth, start.y + rowHeight }, kStripeColor);
    }
    if (modified) {
        drawList->AddRectFilled(start, { start.x + kModifiedBarWidth, start.y + rowHeight }, kModifiedBarColor);
    }
    ImGui::SetCursorScreenPos({ start.x + kModifiedBarWidth + kModifiedBarWidth, start.y });
    ImGui::AlignTextToFramePadding();
    if (modified) {
        ImGui::TextColored(kModifiedColor, "%s", label.c_str());
    } else {
        ImGui::TextUnformatted(label.c_str());
    }
    // 説明・キー・保存時の値を名前の上に出す
    if (ImGui::IsItemHovered()) {
        const std::string help = HelpFor(path);
        const std::string before = modified && saved != nullptr ? "\n保存時の値: " + saved->dump() : std::string();
        ImGui::SetTooltip("%s%sキー: %s%s", help.c_str(), help.empty() ? "" : "\n", path.c_str(), before.c_str());
    }
    ImGui::SameLine(labelWidth);
    // 保存時の値がある変えた項目には、その値だけ戻すボタンを置く
    bool reverted = false;
    if (modified && saved != nullptr) {
        reverted = ImGui::SmallButton("戻す");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("保存時の値に戻す: %s", saved->dump().c_str());
        }
        ImGui::SameLine();
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    return reverted;
}

/** @brief グループの見出しを色分けして描く（中の項目数と、保存時から変えた数を添える） */
bool JsonConfigEditor::DrawSectionHeader(const std::string& label, const std::string& path, const nlohmann::json& value,
    const nlohmann::json* saved, int depth) {
    // 数が変わっても開き具合が変わらないよう、ID は場所だけから作る（### の後ろ）
    const int changes = CountChanges(value, saved);
    std::string caption = label + "  (" + std::to_string(CountValues(value)) + ")";
    if (changes > 0) {
        caption += "   変更 " + std::to_string(changes);
    }
    caption += "###" + path;
    if (depth != 1) {
        return ImGui::TreeNode(caption.c_str());
    }
    // 直下のグループは、名前から決めた色の帯にする（同じグループはいつも同じ色）
    const float hue = static_cast<float>(std::hash<std::string> {}(path) % kHueSteps) / static_cast<float>(kHueSteps);
    ImVec4 color = { 0.0f, 0.0f, 0.0f, kHeaderAlpha };
    ImGui::ColorConvertHSVtoRGB(hue, kHeaderSaturation, kHeaderValue, color.x, color.y, color.z);
    const ImVec4 hovered = { color.x, color.y, color.z, (std::min)(color.w + kHeaderHoverBoost, 1.0f) };
    ImGui::PushStyleColor(ImGuiCol_Header, color);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, hovered);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, hovered);
    const bool opened = ImGui::CollapsingHeader(caption.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::PopStyleColor(kHeaderColorCount);
    return opened;
}

/** @brief 保存時から変えた値の数（入れ子の中まで数える。保存時に無かった値も1つと数える） */
int JsonConfigEditor::CountChanges(const nlohmann::json& value, const nlohmann::json* saved) {
    if (saved == nullptr) {
        return CountValues(value);
    }
    if (*saved == value) {
        return 0;
    }
    // 中身を持つ物は子ごとに比べる（色やベクトルは1つの値）
    int count = 0;
    if (value.is_object()) {
        for (auto it = value.begin(); it != value.end(); ++it) {
            if (!IsHiddenKey(it.key())) {
                count += CountChanges(it.value(), SavedChild(saved, it.key()));
            }
        }
        return count;
    }
    if (value.is_array() && !IsNumberRow(value)) {
        for (size_t i = 0; i < value.size(); ++i) {
            count += CountChanges(value[i], SavedElement(saved, i));
        }
        return count;
    }
    return 1;
}

/** @brief 入れ子の中まで数えた編集できる値の数 */
int JsonConfigEditor::CountValues(const nlohmann::json& value) {
    int count = 0;
    if (value.is_object()) {
        for (auto it = value.begin(); it != value.end(); ++it) {
            if (!IsHiddenKey(it.key())) {
                count += CountValues(it.value());
            }
        }
        return count;
    }
    if (value.is_array() && !IsNumberRow(value)) {
        for (const nlohmann::json& element : value) {
            count += CountValues(element);
        }
        return count;
    }
    return 1;
}

/** @brief 左側のファイル一覧の1行（すぐ反映されるかの丸・未保存の色） */
void JsonConfigEditor::DrawFileListRow(int index) {
    const FileEntry& entry = files_[index];
    const bool live = reloadCallbacks_.count(entry.fileName) > 0;
    // 行の左に丸を描く（塗った丸=保存するとすぐ反映、輪だけ=次回起動時に反映）
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const ImVec2 center = { start.x + kFileMarkWidth * 0.5f, start.y + ImGui::GetTextLineHeight() * 0.5f };
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImU32 markColor = ImGui::GetColorU32(live ? kLiveColor : kNextBootColor);
    if (live) {
        drawList->AddCircleFilled(center, kFileMarkRadius, markColor);
    } else {
        drawList->AddCircle(center, kFileMarkRadius, markColor);
    }
    ImGui::SetCursorScreenPos({ start.x + kFileMarkWidth, start.y });
    // 未保存のファイルは名前の色を変える
    if (entry.dirty) {
        ImGui::PushStyleColor(ImGuiCol_Text, kModifiedColor);
    }
    const std::string caption = (entry.dirty ? "* " : "") + entry.title + "##" + entry.fileName;
    if (ImGui::Selectable(caption.c_str(), selected_ == index)) {
        selected_ = index;
    }
    if (entry.dirty) {
        ImGui::PopStyleColor();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s\n%s", entry.fileName.c_str(), live ? "保存するとすぐ反映" : "保存すると次回起動時に反映");
    }
}

} // namespace engine::graphics

#endif // USE_IMGUI
