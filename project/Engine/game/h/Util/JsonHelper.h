#pragma once
#include "json.hpp"
#include <string>

// JSON ファイルの読み書きユーティリティ
namespace engine {
class JsonHelper {
public:
    // ファイルから JSON を読み込む（ファイルが存在しない・破損の場合は空オブジェクトを返す）
    static nlohmann::json Load(const std::string& path);

    // JSON をファイルへ保存する（親ディレクトリがなければ作成する）
    static void Save(const std::string& path, const nlohmann::json& j, int indent = 2);
};
} // namespace engine
