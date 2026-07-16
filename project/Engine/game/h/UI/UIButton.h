/**
 * @file UIButton.h
 * @brief UIMenu が並べる1項目分のデータを定義するファイル
 */
#pragma once
#include <string>
namespace engine::game {

/** @brief UIMenu の選択項目1つ分のデータ */
struct UIButton {
    std::string label; ///< 表示するラベル文字列
    bool enabled = true; ///< false の場合は選択不可（グレーアウト表示）
};

} // namespace engine::game
