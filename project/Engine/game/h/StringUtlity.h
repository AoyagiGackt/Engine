/**
 * @file StringUtility.h
 * @brief 文字コードの変換（std::string ⇔ std::wstring）に関するユーティリティ関数を定義するファイル
 */
#pragma once
#include <string>

/**
 * @namespace StringUtility
 * @brief 文字列操作に関連する便利な関数をまとめた名前空間
 */
namespace StringUtility {

/**
 * @brief マルチバイト文字列(string)をワイド文字列(wstring)に変換する
 * @param str 変換したいマルチバイト文字列（UTF-8など）
 * @return std::wstring 変換後のワイド文字列（UTF-16）
 * @note Windows API や DXGI 関連の関数に文字列を渡す際に使用します
 */
std::wstring ConvertString(const std::string& str);

/**
 * @brief ワイド文字列(wstring)をマルチバイト文字列(string)に変換する
 * @param str 変換したいワイド文字列（UTF-16）
 * @return std::string 変換後のマルチバイト文字列（UTF-8など）
 * @note ログ出力やファイルパスの標準的な扱いにおいて、wstringをstringに戻したい場合に使用します
 */
std::string ConvertString(const std::wstring& str);
};