#pragma once
#include <string>
#include "json.hpp"

// JSON ファイルの読み書きユーティリティ
namespace engine {
namespace JsonHelper {
    // ファイルから JSON を読み込む（ファイルが存在しない・破損の場合は空オブジェクトを返す）
    nlohmann::json Load(const std::string& path);

    // JSON をファイルへ保存する（親ディレクトリがなければ作成する）
    void Save(const std::string& path, const nlohmann::json& j, int indent = 2);
}
} // namespace engine
