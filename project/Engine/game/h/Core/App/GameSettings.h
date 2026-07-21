/**
 * @file GameSettings.h
 * @brief GameSettingsが公開する型とAPIを定義するファイル
 */
#pragma once
namespace engine {
// ゲーム設定データ（音量など）
struct GameSettings {
    float bgmVolume = 0.7f;
    float seVolume = 1.0f;
};

// 設定の読み込み・保存を管理するシングルトン
// Load() はゲーム起動時、Save() は設定変更時に呼ぶ
class GameSettingsManager {
public:
    static GameSettingsManager* GetInstance();

    void Load(); // save/settings.json から読み込む（ファイルがなければデフォルト値）
    void Save(); // save/settings.json へ書き込む

    GameSettings& Get() { return settings_; }
    const GameSettings& Get() const { return settings_; }

private:
    GameSettingsManager() = default;
    GameSettings settings_;
    static constexpr const char* kFilePath = "save/settings.json";
};

} // namespace engine
