/**
 * @file EditorUI.cpp
 * @brief エディタ共通の ImGui 部品（説明マーカー・確認ダイアログなど）の実装
 */
#ifdef USE_IMGUI
#include "EditorUI.h"
#include <Windows.h>
#include <algorithm>
#include <commdlg.h>
#include <imgui.h>
#pragma comment(lib, "comdlg32.lib")

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

namespace engine::graphics::editorUI {

/** @brief ファイル選択ダイアログを開き、選んだパスを返す（取り消したら空文字列） */
std::string OpenFileDialog(const char* filter, const char* initialDirectory) {
    char path[MAX_PATH] = { };
    OPENFILENAMEA dialog = { };
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = path;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrInitialDir = initialDirectory;
    // 実在するファイルだけ選べるようにし、作業フォルダは変えない（相対パスの読み込みが狂わないように）
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameA(&dialog) ? std::string(path) : std::string { };
}

/** @brief 絶対パスを Resources/ から始まるプロジェクト内の相対パスへ変換する（Resources/ を含まなければそのまま） */
std::string ToProjectRelativePath(const std::string& absolutePath) {
    std::string path = absolutePath;
    std::replace(path.begin(), path.end(), '\\', '/');
    const size_t resourcesPosition = path.find("Resources/");
    return resourcesPosition == std::string::npos ? path : path.substr(resourcesPosition);
}

/** @brief 直前のウィジェットの右に (?) を表示し、ホバーで説明ツールチップを出す */
void HelpMarker(const char* desc) {
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

/** @brief 確認モーダル（破棄・上書きなど取り返しのつかない操作の前に挟む） */
ConfirmResult ConfirmModal(const char* popupId, const char* message,
    const char* okLabel, const char* cancelLabel) {
    ConfirmResult result = ConfirmResult::kNone;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f),
        ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal(popupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(message);
        ImGui::Separator();
        if (ImGui::Button(okLabel, ImVec2(kDialogButtonWidth, 0))) {
            result = ConfirmResult::kOk;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(cancelLabel, ImVec2(kDialogButtonWidth, 0))) {
            result = ConfirmResult::kCancel;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    return result;
}

/** @brief エディタ起動ホットキーの常時表示オーバーレイ（画面左下、半透明、入力を奪わない） */
void ShowHotkeyOverlay(bool nodeEditorOpen, bool stageEditorOpen, const char* extraLine) {
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
