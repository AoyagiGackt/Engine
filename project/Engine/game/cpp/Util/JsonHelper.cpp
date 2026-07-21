/**
 * @file JsonHelper.cpp
 * @brief JsonHelperが担当する処理を実装するファイル
 */
#include "JsonHelper.h"
#include <filesystem>
#include <fstream>
namespace engine {

nlohmann::json JsonHelper::Load(const std::string& path)
{
    std::ifstream f(path);
    if (!f) {
        return { };
    }
    try {
        return nlohmann::json::parse(f);
    } catch (...) {
        return { };
    }
}

void JsonHelper::Save(const std::string& path, const nlohmann::json& j, int indent)
{
    auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }

    std::ofstream f(path);
    if (!f) {
        return;
    }
    f << j.dump(indent) << '\n';
}

} // namespace engine
