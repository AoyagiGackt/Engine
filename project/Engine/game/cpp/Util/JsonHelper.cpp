#include "JsonHelper.h"
#include <filesystem>
#include <fstream>
namespace engine {
namespace JsonHelper {

nlohmann::json Load(const std::string& path)
{
    std::ifstream f(path);
    if (!f) { return {}; }
    try { return nlohmann::json::parse(f); }
    catch (...) { return {}; }
}

void Save(const std::string& path, const nlohmann::json& j, int indent)
{
    auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) { std::filesystem::create_directories(parent); }

    std::ofstream f(path);
    if (!f) { return; }
    f << j.dump(indent) << '\n';
}

}
} // namespace engine
