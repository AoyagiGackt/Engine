/**
 * @file EditorUI.cpp
 * @brief EditorUIの描画資源とGPU処理の管理に関する具体的な処理を実装するファイル
 */
#ifdef USE_IMGUI
#include "EditorUI.h"
#include <imgui.h>

namespace {

// 警告文字の色
constexpr float kWarningGreen = 0.82f;
constexpr float kWarningBlue = 0.35f;

// ダイアログと通知のレイアウト
constexpr float kDialogWrapEm = 20.0f;
constexpr float kDialogButtonWidth = 120.0f;
constexpr float kToastInset = 10.0f;
constexpr float kToastOpacity = 0.75f;

}

namespace engine::graphics::EditorUI {

void HelpMarker(const char* desc)
{
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * kDialogWrapEm);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

ConfirmResult ConfirmModal(const char* popupId, const char* message,
    const char* okLabel, const char* cancelLabel)
{
    ConfirmResult result = ConfirmResult::None;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f),
        ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal(popupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(message);
        ImGui::Separator();
        if (ImGui::Button(okLabel, ImVec2(kDialogButtonWidth, 0))) {
            result = ConfirmResult::Ok;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(cancelLabel, ImVec2(kDialogButtonWidth, 0))) {
            result = ConfirmResult::Cancel;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    return result;
}

void ShowHotkeyOverlay(bool nodeEditorOpen, bool stageEditorOpen, const char* extraLine)
{
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + kToastInset, vp->Pos.y + vp->Size.y - kToastInset),
        ImGuiCond_Always, ImVec2(0.0f, 1.0f));
    // 背景を明るい色のブロック上に表示することがあるため、白文字だけでは埋もれる。
    // 背景を濃いめにし、文字も白ではなく視認性の高い暖色(アンバー)にして常に読めるようにする
    ImGui::SetNextWindowBgAlpha(kToastOpacity);

    // NoInputs: ゲーム画面や他エディタへのクリックを一切奪わない表示専用オーバーレイ
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration
        | ImGuiWindowFlags_AlwaysAutoResize
        | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoFocusOnAppearing
        | ImGuiWindowFlags_NoNav
        | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoInputs;

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, kWarningGreen, kWarningBlue, 1.0f));
    if (ImGui::Begin("##EditorHotkeyOverlay", nullptr, flags)) {
        ImGui::Text("F1: ノードエディタ%s", nodeEditorOpen ? " (表示中)" : "");
        ImGui::Text("F2: ステージエディタ%s", stageEditorOpen ? " (表示中)" : "");
        if (extraLine) {
            ImGui::TextUnformatted(extraLine);
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
}

} // namespace engine::graphics::EditorUI

#else
// USE_IMGUI無効ビルドでもこの翻訳単位を空にしないためのダミー
namespace engine::graphics {
}
#endif // USE_IMGUI
