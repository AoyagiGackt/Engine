#pragma once
#include <string>

namespace JsonHelper {
    float       ReadFloat (const std::string& src, const std::string& key, float       def);
    int         ReadInt   (const std::string& src, const std::string& key, int         def);
    std::string ReadString(const std::string& src, const std::string& key, const std::string& def);
}
