/**
 * @file JsonHelper.cpp
 * @brief JSONファイルの読み込み・保存ユーティリティ（JsonHelper）の実装
 */
#include "JsonHelper.h"
#include "AssetPack.h"
#include <filesystem>
#include <fstream>
namespace engine {

nlohmann::json JsonHelper::Load(const std::string& path)
{
    // 配布時はResourcesが無いので、pakへそのまま入れた元ファイルから読む
    std::vector<uint8_t> packed;
    if (AssetPack::GetInstance()->ReadRawIfMissing(path, packed)) {
        try {
            return nlohmann::json::parse(packed.begin(), packed.end());
        } catch (...) {
            return { };
        }
    }

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
