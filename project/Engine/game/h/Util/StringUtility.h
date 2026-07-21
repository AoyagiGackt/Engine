/**
 * @file StringUtility.h
 * @brief 文字列の変換ユーティリティ（std::string ⇔ std::wstring）
 *
 * Windows の API やファイルパスは wstring（ワイド文字列）を要求することが多い
 * 一方、C++ の標準的な文字列は string（UTF-8 マルチバイト）
 * この2つを相互変換する関数を提供する
 *
 * 例: "Resources/audio.wav"（string）→ L"Resources/audio.wav"（wstring）
 *     → Windows API に渡せる形式になる
 */
#pragma once
#include <string>

namespace engine {
/**
 * @brief StringUtility に関する型を提供する
 * @details StringUtility が扱うデータと操作の責務をまとめる
 */
class StringUtility {
public:
    // UTF-8 の string（日本語も扱える文字列）を wstring（ワイド文字列）に変換する
    // 用途  ファイルパスを Windows API（MFCreateSourceReaderFromURL など）に渡すとき
    /**
     * @brief ConvertString に対応する処理を実行する
     * @param str 処理に使用する値
     * @return 処理結果
     */
    static std::wstring ConvertString(const std::string& str);

    // wstring（ワイド文字列）を UTF-8 の string に変換する
    // 用途  Windows API から返ってきた wstring をログ出力やデバッグ表示に使うとき
    /**
     * @brief ConvertString に対応する処理を実行する
     * @param str 処理に使用する値
     * @return 処理結果
     */
    static std::string ConvertString(const std::wstring& str);
};
} // namespace engine
