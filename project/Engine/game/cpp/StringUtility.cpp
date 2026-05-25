#include "StringUtility.h"
#include <stringapiset.h>

namespace StringUtility {

std::wstring ConvertString(const std::string& str)
{
    if (str.empty()) { return {}; }
    auto n = MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), nullptr, 0);
    if (n == 0) { return {}; }
    std::wstring result(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), result.data(), n);
    return result;
}

std::string ConvertString(const std::wstring& str)
{
    if (str.empty()) { return {}; }
    auto n = WideCharToMultiByte(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), nullptr, 0, nullptr, nullptr);
    if (n == 0) { return {}; }
    std::string result(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), result.data(), n, nullptr, nullptr);
    return result;
}

}
