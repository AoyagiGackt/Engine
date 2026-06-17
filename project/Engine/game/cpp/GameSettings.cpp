#include "GameSettings.h"
#include "JsonHelper.h"
#include <filesystem>
#include <fstream>
#include <sstream>

GameSettingsManager* GameSettingsManager::GetInstance() {
    static GameSettingsManager instance;
    return &instance;
}

void GameSettingsManager::Load() {
    std::ifstream f(kFilePath);
    if (!f) { return; }

    std::stringstream ss;
    ss << f.rdbuf();
    std::string src = ss.str();

    settings_.bgmVolume = JsonHelper::ReadFloat(src, "bgm_volume", settings_.bgmVolume);
    settings_.seVolume  = JsonHelper::ReadFloat(src, "se_volume",  settings_.seVolume);
}

void GameSettingsManager::Save() {
    std::filesystem::create_directories("save");

    std::ofstream f(kFilePath);
    if (!f) { return; }

    f << "{\n";
    f << "  \"bgm_volume\": " << settings_.bgmVolume << ",\n";
    f << "  \"se_volume\": "  << settings_.seVolume  << "\n";
    f << "}\n";
}
