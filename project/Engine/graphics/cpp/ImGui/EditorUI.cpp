/**
 * @file EditorUI.cpp
 * @brief エディタ共通の ImGui 部品（説明マーカー・確認ダイアログなど）の実装
 */
#ifdef USE_IMGUI
#include "EditorUI.h"
#include <Windows.h>
#include <algorithm>
#include <cctype>
#include <cmath>
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

// 未保存の印の色
constexpr ImVec4 kUnsavedColor = { 1.0f, 0.75f, 0.2f, 1.0f };

// 値に合わせたドラッグ感度
constexpr float kDragSpeedRatio = 0.01f; // ドラッグ1ピクセルで値の何割動かすか
constexpr float kMinDragSpeed = 0.001f; // 値が小さい時の感度の下限
constexpr float kZeroValueDragSpeed = 0.01f; // 値が0の時は桁が分からないので少し大きめに動かす
constexpr float kMinIntDragSpeed = 0.2f;

// 掴んでいるドラッグ項目と、掴んだ時点で決めた感度
ImGuiID sFrozenDragItem = 0;
float sFrozenDragSpeed = 0.0f;

/** @brief 掴んでいる項目なら固定した感度を、それ以外は渡された感度を返す */
float ResolveDragSpeed(ImGuiID item, float freshSpeed) {
    return (item == sFrozenDragItem && ImGui::IsAnyItemActive()) ? sFrozenDragSpeed : freshSpeed;
}

/** @brief 直前の項目を掴んだ瞬間の感度を覚える（途中で値が0に近づいても感度が落ちないように） */
void RememberDragSpeed(ImGuiID item, float speed) {
    if (ImGui::IsItemActivated()) {
        sFrozenDragItem = item;
        sFrozenDragSpeed = speed;
    }
}

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

/** @brief ボタンを押すと確認モーダルを挟み、はいを選んだフレームだけ true を返す */
bool ConfirmButton(const char* label, const char* message, const char* okLabel) {
    // モーダルの ID はボタンごとに分ける
    const std::string popupId = std::string("確認##") + label;
    if (ImGui::Button(label)) {
        ImGui::OpenPopup(popupId.c_str());
    }
    return ConfirmModal(popupId.c_str(), message, okLabel) == ConfirmResult::kOk;
}

/** @brief 直前の項目の右に未保存の印を出す */
void DrawUnsavedMark() {
    ImGui::SameLine();
    ImGui::TextColored(kUnsavedColor, "未保存");
}

/** @brief 英字の大文字小文字を無視して部分一致を調べる（検索語が空なら常に一致） */
bool ContainsIgnoreCase(const std::string& text, const std::string& query) {
    const auto sameLetter = [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    };
    return query.empty() || std::search(text.begin(), text.end(), query.begin(), query.end(), sameLetter) != text.end();
}

/** @brief コンボの先頭に置く絞り込み欄を出す（開いた直後にフォーカスし、前回の文字を消す） */
void ComboFilterInput(char* buffer, size_t bufferSize) {
    if (ImGui::IsWindowAppearing()) {
        buffer[0] = '\0';
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##comboFilter", "絞り込み", buffer, bufferSize);
    ImGui::Separator();
}

/** @brief 今のウィンドウにフォーカスがある時の Ctrl+キーを判定する */
bool CtrlShortcut(ImGuiKey key, bool allowWhileTyping) {
    const ImGuiIO& io = ImGui::GetIO();
    if (!io.KeyCtrl || (io.WantTextInput && !allowWhileTyping)) {
        return false;
    }
    // 押しっぱなしで連続して実行しないようリピートを切る
    return ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(key, false);
}

/** @brief 値の大きさに合わせた感度の小数ドラッグを出す（感度は掴んだ時点の値で固定する） */
bool AutoDragFloat(const char* label, float* values, int components, const char* format) {
    // いちばん大きい成分を基準に感度を決める
    float largest = 0.0f;
    for (int i = 0; i < components; ++i) {
        largest = (std::max)(largest, std::fabs(values[i]));
    }
    const float freshSpeed = largest > 0.0f ? (std::max)(largest * kDragSpeedRatio, kMinDragSpeed) : kZeroValueDragSpeed;
    const ImGuiID item = ImGui::GetID(label);
    const float speed = ResolveDragSpeed(item, freshSpeed);
    const bool changed = ImGui::DragScalarN(label, ImGuiDataType_Float, values, components, speed, nullptr, nullptr, format);
    RememberDragSpeed(item, speed);
    return changed;
}

/** @brief 値の大きさに合わせた感度の整数ドラッグを出す（感度は掴んだ時点の値で固定する） */
bool AutoDragInt(const char* label, int* value) {
    const float freshSpeed = (std::max)(std::fabs(static_cast<float>(*value)) * kDragSpeedRatio, kMinIntDragSpeed);
    const ImGuiID item = ImGui::GetID(label);
    const float speed = ResolveDragSpeed(item, freshSpeed);
    const bool changed = ImGui::DragInt(label, value, speed);
    RememberDragSpeed(item, speed);
    return changed;
}

/** @brief 直前の一覧の行をドラッグした時の並べ替え方向を返す */
int ListReorderStep(int index, int count) {
    // 掴んだまま行の外へ出た時だけ動かす（クリックしただけでは動かない）
    if (!ImGui::IsItemActive() || ImGui::IsItemHovered()) {
        return 0;
    }
    const float dragY = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left).y;
    if (dragY == 0.0f) {
        return 0;
    }
    const int step = dragY < 0.0f ? -1 : 1;
    const int next = index + step;
    if (next < 0 || next >= count) {
        return 0;
    }
    // 次の行へ移ったら移動量を数え直す
    ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
    return step;
}

/** @brief 一覧の行の左に上下の並べ替えボタンを出す（端の行は外へ動かせない） */
int ListMoveButtons(int index, int count) {
    int step = 0;
    const auto moveButton = [&step](const char* id, ImGuiDir direction, bool disabled, int offset) {
        ImGui::BeginDisabled(disabled);
        if (ImGui::ArrowButton(id, direction)) {
            step = offset;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
    };
    moveButton("##up", ImGuiDir_Up, index <= 0, -1);
    moveButton("##down", ImGuiDir_Down, index + 1 >= count, 1);
    return step;
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
