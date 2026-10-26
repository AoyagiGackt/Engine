/** @file TextBufferCapacity.h
 * @brief 固定長テキスト入力と表示に使うバッファ容量
 */
#pragma once
#include <cstddef>
namespace engine::textBufferCapacity {
inline constexpr std::size_t kCharacter = 2; ///< 1文字と終端
inline constexpr std::size_t kShortcut = 4; ///< 操作キーの短い表示
inline constexpr std::size_t kCodepoint = 16; ///< 文字コードの整形表示
inline constexpr std::size_t kShortText = 32; ///< 数値や時刻の短い表示
inline constexpr std::size_t kSectionTitle = 48; ///< 一覧の節の見出し
inline constexpr std::size_t kIdentifier = 64; ///< 名前や識別子の入力
inline constexpr std::size_t kStatus = 80; ///< 状況を表示する短い行
inline constexpr std::size_t kName = 96; ///< オブジェクト名とフラグ名の入力
inline constexpr std::size_t kLabel = 128; ///< 詳細を含む一覧ラベル
inline constexpr std::size_t kRelativePath = 160; ///< グラフの相対パス入力
inline constexpr std::size_t kPath = 256; ///< アセットやファイルのパス入力
} // namespace engine::TextBufferCapacity
