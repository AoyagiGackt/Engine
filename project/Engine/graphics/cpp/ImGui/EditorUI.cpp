/**
 * @file EditorUI.cpp
 * @brief EditorUIの描画資源とGPU処理の管理に関する具体的な処理を実装するファイル
 */
#ifdef USE_IMGUI
#include "EditorUI.h"
#include <imgui.h>

namespace engine::graphics::EditorUI {

void HelpMarker(const char* desc)
{
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 20.0f);
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
        if (ImGui::Button(okLabel, ImVec2(120, 0))) {
            result = ConfirmResult::Ok;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(cancelLabel, ImVec2(120, 0))) {
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
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + 10.0f, vp->Pos.y + vp->Size.y - 10.0f),
        ImGuiCond_Always, ImVec2(0.0f, 1.0f));
    ImGui::SetNextWindowBgAlpha(0.35f);

    // NoInputs: ゲーム画面や他エディタへのクリックを一切奪わない表示専用オーバーレイ
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration
        | ImGuiWindowFlags_AlwaysAutoResize
        | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoFocusOnAppearing
        | ImGuiWindowFlags_NoNav
        | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoInputs;

    if (ImGui::Begin("##EditorHotkeyOverlay", nullptr, flags)) {
        ImGui::Text("F1: ノードエディタ%s", nodeEditorOpen ? " (表示中)" : "");
        ImGui::Text("F2: ステージエディタ%s", stageEditorOpen ? " (表示中)" : "");
        if (extraLine) {
            ImGui::TextUnformatted(extraLine);
        }
    }
    ImGui::End();
}

} // namespace engine::graphics::EditorUI

#else
// USE_IMGUI無効ビルドでもこの翻訳単位を空にしないためのダミー
namespace engine::graphics {
}
#endif // USE_IMGUI
