/**
 * @file JsonHelper.cpp
 * @brief JSONファイルの読み込み・保存ユーティリティ（JsonHelper）の実装
 */
#include "JsonHelper.h"
#include "AssetPack.h"
#include "Logger.h"
#include <Windows.h>
#include <stdexcept>
#include <filesystem>
#include <fstream>
namespace engine {

/** @brief JSONファイルを読み込んでパースする */
nlohmann::json JsonHelper::Load(const std::string& path) {
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

/** @brief JSONをファイルへ書き出す（親ディレクトリが無ければ作成する） */
bool JsonHelper::Save(const std::string& path, const nlohmann::json& j, int indent) {
    const std::filesystem::path destination(path);
    auto temporary = destination;
    temporary += ".tmp";
    try {
        // 既存データには触れず、同じフォルダーへ全内容を書き出す。
        const auto parent = destination.parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent);
        }
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        file << j.dump(indent) << '\n';
        file.flush();
        if (!file) {
            throw std::runtime_error("JSONの書き込みに失敗しました");
        }
        file.close();
        if (!file) {
            throw std::runtime_error("JSONファイルを閉じられません");
        }
        // Windowsでは既存ファイルの置換を明示して、保存済みデータを保護する。
        if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            throw std::runtime_error("JSONファイルを置換できません");
        }
        return true;
    } catch (const std::exception& error) {
        std::error_code cleanupError;
        std::filesystem::remove(temporary, cleanupError);
        Logger::LogError("JSONの保存に失敗しました: " + path + " / " + error.what());
        return false;
    }
}

} // namespace engine
