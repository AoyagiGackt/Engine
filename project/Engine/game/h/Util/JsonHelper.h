/**
 * @file JsonHelper.h
 * @brief JsonHelperのアプリケーション実行基盤の管理に関する公開型と操作インターフェースを定義するファイル
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
     * @brief Load の結果を取得する
     * @param path 処理に使用する値
     * @return 処理結果
     */
    static nlohmann::json Load(const std::string& path);

    // JSON をファイルへ保存する（親ディレクトリがなければ作成する）
    /**
     * @brief Save に対応する処理を実行する
     * @param path 処理に使用する値
     * @param j 処理に使用する値
     * @param indent 処理に使用する値
     * @return なし
     */
    static void Save(const std::string& path, const nlohmann::json& j, int indent = 2);
};
} // namespace engine
