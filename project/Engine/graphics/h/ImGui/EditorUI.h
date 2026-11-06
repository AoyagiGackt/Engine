/**
 * @file EditorUI.h
 * @brief 各エディタ（ステージエディタ/ノードエディタ等）で共通利用するImGui小物ヘルパー
 * @note ツールチップ・確認モーダル・ホットキー一覧オーバーレイなど、
 * エディタごとにコピペされがちなUIイディオムをここに集約する
 */
#pragma once
#ifdef USE_IMGUI
#include <cstddef>
#include <imgui.h>
#include <string>

namespace engine::graphics::editorUI {

/**
 * @brief ファイル選択ダイアログを開き、選んだパスを返す（取り消したら空文字列）
 * @param filter 表示するファイルの種類（例: "PNG Files\0*.png\0\0"）
 * @param initialDirectory ダイアログを開いた時に最初に表示するフォルダ
 */
std::string OpenFileDialog(const char* filter, const char* initialDirectory);

/** @brief 絶対パスを Resources/ から始まるプロジェクト内の相対パスへ変換する（Resources/ を含まなければそのまま） */
std::string ToProjectRelativePath(const std::string& absolutePath);

/** @brief 直前のウィジェットの右に (?) を表示し、ホバーで説明ツールチップを出す */
void HelpMarker(const char* desc);

enum class ConfirmResult {
    kNone, ///< モーダル表示中 or 未表示（何も確定していない）
    kOk,
    kCancel,
};

/**
 * @brief 確認モーダル（破棄・上書きなど取り返しのつかない操作の前に挟む）
 * @note 開くのは呼び出し側の ImGui::OpenPopup(popupId)。
 * 同じウィンドウスコープ内で毎フレーム呼び、戻り値がOkのフレームだけ処理を実行する
 */
ConfirmResult ConfirmModal(const char* popupId, const char* message,
    const char* okLabel = "はい", const char* cancelLabel = "キャンセル");

/**
 * @brief ボタンを押すと確認モーダルを挟み、はいを選んだフレームだけ true を返す
 * @param label ボタンの文字（同じIDスコープ内で重複させない）
 * @param message 確認モーダルの本文
 * @param okLabel 実行側のボタンの文字
 * @note モーダルをボタンと同じ場所で開閉するので、確認中も毎フレーム呼び続ける
 */
bool ConfirmButton(const char* label, const char* message, const char* okLabel = "削除する");

/** @brief 直前の項目の右に未保存の印を出す */
void DrawUnsavedMark();

/** @brief 英字の大文字小文字を無視して部分一致を調べる（検索語が空なら常に一致） */
bool ContainsIgnoreCase(const std::string& text, const std::string& query);

/** @brief コンボの先頭に置く絞り込み欄を出す（開いた直後にフォーカスし、前回の文字を消す） */
void ComboFilterInput(char* buffer, size_t bufferSize);

/**
 * @brief 今のウィンドウにフォーカスがある時の Ctrl+キーを判定する
 * @param key 判定するキー
 * @param allowWhileTyping 文字入力中も反応させるか（保存など入力欄の編集と競合しない操作用）
 */
bool CtrlShortcut(ImGuiKey key, bool allowWhileTyping = false);

/**
 * @brief 値の大きさに合わせた感度の小数ドラッグを出す（感度は掴んだ時点の値で固定する）
 * @param label 項目のラベル
 * @param values 編集する数値の先頭
 * @param components 数値の個数（ベクトルなら3など）
 * @param format 表示の書式
 * @return 値が変わったら true
 */
bool AutoDragFloat(const char* label, float* values, int components = 1, const char* format = "%.4g");

/** @brief 値の大きさに合わせた感度の整数ドラッグを出す（感度は掴んだ時点の値で固定する） */
bool AutoDragInt(const char* label, int* value);

/**
 * @brief 直前の一覧の行をドラッグした時の並べ替え方向を返す
 * @param index この行の番号
 * @param count 一覧の行数
 * @return 上へ動かすなら -1、下へ動かすなら 1、動かさないなら 0
 */
int ListReorderStep(int index, int count);

/**
 * @brief 一覧の行の左に上下の並べ替えボタンを出す（端の行は外へ動かせない）
 * @param index この行の番号
 * @param count 一覧の行数
 * @return 上を押したら -1、下を押したら 1、押されなければ 0
 */
int ListMoveButtons(int index, int count);

/**
 * @brief エディタ起動ホットキーの常時表示オーバーレイ（画面左下、半透明、入力を奪わない）
 * @param nodeEditorOpen  ノードエディタ(F1)が表示中か（表示中の行にマークを付ける）
 * @param stageEditorOpen ステージエディタ(F2)が表示中か
 * @param extraLine       シーン固有の追加行（不要ならnullptr）
 */
void ShowHotkeyOverlay(bool nodeEditorOpen, bool stageEditorOpen, const char* extraLine = nullptr);

} // namespace engine::graphics::EditorUI

#endif // USE_IMGUI
