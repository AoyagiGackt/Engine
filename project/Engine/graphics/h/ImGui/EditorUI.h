/**
 * @file EditorUI.h
 * @brief 各エディタ（ステージエディタ/ノードエディタ等）で共通利用するImGui小物ヘルパー
 * @note ツールチップ・確認モーダル・ホットキー一覧オーバーレイなど、
 * エディタごとにコピペされがちなUIイディオムをここに集約する
 */
#pragma once
#ifdef USE_IMGUI

namespace engine::graphics::EditorUI {

/** @brief 直前のウィジェットの右に (?) を表示し、ホバーで説明ツールチップを出す */
void HelpMarker(const char* desc);

enum class ConfirmResult {
    None, ///< モーダル表示中 or 未表示（何も確定していない）
    Ok,
    Cancel,
};

/**
 * @brief 確認モーダル（破棄・上書きなど取り返しのつかない操作の前に挟む）
 * @note 開くのは呼び出し側の ImGui::OpenPopup(popupId)。
 * 同じウィンドウスコープ内で毎フレーム呼び、戻り値がOkのフレームだけ処理を実行する
 */
ConfirmResult ConfirmModal(const char* popupId, const char* message,
    const char* okLabel = "はい", const char* cancelLabel = "キャンセル");

/**
 * @brief エディタ起動ホットキーの常時表示オーバーレイ（画面左下、半透明、入力を奪わない）
 * @param nodeEditorOpen  ノードエディタ(F1)が表示中か（表示中の行にマークを付ける）
 * @param stageEditorOpen ステージエディタ(F2)が表示中か
 * @param extraLine       シーン固有の追加行（不要ならnullptr）
 */
void ShowHotkeyOverlay(bool nodeEditorOpen, bool stageEditorOpen, const char* extraLine = nullptr);

} // namespace engine::graphics::EditorUI

#endif // USE_IMGUI
