#include "GameSettings.h"
#include "JsonHelper.h"

GameSettingsManager* GameSettingsManager::GetInstance() {
    static GameSettingsManager instance;
    return &instance;
}

void GameSettingsManager::Load()
{
    auto j = JsonHelper::Load(kFilePath);
    if (j.is_object()) {
        settings_.bgmVolume = j.value("bgm_volume", settings_.bgmVolume);
        settings_.seVolume = j.value("se_volume", settings_.seVolume);
    } else {
        // ログ出力やデフォルト維持
    }
}

void GameSettingsManager::Save() {
    nlohmann::json j;
    j["bgm_volume"] = settings_.bgmVolume;
    j["se_volume"]  = settings_.seVolume;
    JsonHelper::Save(kFilePath, j);
}
