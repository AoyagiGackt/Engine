/**
 * @file GameSettings.cpp
 * @brief BGM/SE音量設定の読み込み・保存（GameSettingsManager）の実装
 */
#include "GameSettings.h"
#include "JsonHelper.h"

namespace engine {
/** @brief 唯一のGameSettingsManagerインスタンスを取得する（未生成なら生成する） */
GameSettingsManager* GameSettingsManager::GetInstance() {
    static GameSettingsManager sInstance;
    return &sInstance;
}

/** @brief 保存済みの設定を読み込む（項目が無い設定は既定値のまま残す） */
void GameSettingsManager::Load() {
    auto j = JsonHelper::Load(kFilePath);
    if (j.is_object()) {
        settings_.bgmVolume = j.value("bgm_volume", settings_.bgmVolume);
        settings_.seVolume = j.value("se_volume", settings_.seVolume);
        settings_.autoCombo = j.value("auto_combo", settings_.autoCombo);
    } else {
        // ログ出力やデフォルト維持
    }
}

/** @brief 現在の設定を設定ファイルへ保存する */
void GameSettingsManager::Save() {
    nlohmann::json j;
    j["bgm_volume"] = settings_.bgmVolume;
    j["se_volume"] = settings_.seVolume;
    j["auto_combo"] = settings_.autoCombo;
    JsonHelper::Save(kFilePath, j);
}

} // namespace engine
