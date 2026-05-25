#include "JsonHelper.h"

namespace JsonHelper {

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
