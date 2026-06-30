#include "JsonHelper.h"
#include <filesystem>
#include <fstream>
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

// ---- 後方互換実装 ----

float ReadFloat(const std::string& src, const std::string& key, float def)
{
    std::string needle = "\"" + key + "\": ";
    auto pos = src.find(needle);
    if (pos == std::string::npos) { return def; }
    pos += needle.size();
    try { return std::stof(src.substr(pos)); } catch (...) { return def; }
}

int ReadInt(const std::string& src, const std::string& key, int def)
{
    std::string needle = "\"" + key + "\": ";
    auto pos = src.find(needle);
    if (pos == std::string::npos) { return def; }
    pos += needle.size();
    try { return std::stoi(src.substr(pos)); } catch (...) { return def; }
}

std::string ReadString(const std::string& src, const std::string& key, const std::string& def)
{
    std::string needle = "\"" + key + "\":";
    auto pos = src.find(needle);
    if (pos == std::string::npos) { return def; }
    pos += needle.size();
    while (pos < src.size() && (src[pos] == ' ' || src[pos] == '\t')) { ++pos; }
    if (pos >= src.size() || src[pos] != '"') { return def; }
    ++pos;
    auto end = src.find('"', pos);
    if (end == std::string::npos) { return def; }
    return src.substr(pos, end - pos);
}

}
