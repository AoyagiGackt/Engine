/**
 * @file JsonHelper.h
 * @brief nlohmann::jsonを使ったJSONファイルの読み込み・保存ユーティリティ
 */
#pragma once
#include "json.hpp"
#include <string>

// JSON ファイルの読み書きユーティリティ
namespace engine {
class JsonHelper {
public:
    // ファイルから JSON を読み込む（ファイルが存在しない・破損の場合は空オブジェクトを返す）
    /**
     * @brief JSONファイルを読み込んでパースする
     * @param path 読み込むファイルのパス
     * @return パース結果ファイルが無い・パースに失敗した場合は空オブジェクト
     */
    static nlohmann::json Load(const std::string& path);

    // JSON をファイルへ保存する（親ディレクトリがなければ作成する）
    /**
     * @brief JSONをファイルへ書き出す（親ディレクトリが無ければ作成する）
     * @param path 書き出し先のファイルパス
     * @param j 書き出すJSONデータ
     * @param indent インデントの空白数（0以下ならnlohmann::jsonのdump()の仕様に従う）
     */
    static void Save(const std::string& path, const nlohmann::json& j, int indent = 2);
};
} // namespace engine
