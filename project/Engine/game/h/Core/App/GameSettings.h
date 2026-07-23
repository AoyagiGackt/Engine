/**
 * @file GameSettings.h
 * @brief BGM/SE音量などのゲーム設定データと、その読み込み・保存を行うシングルトンを定義するファイル
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
/**
 * @brief GameSettingsをファイル（save/settings.json）から読み込み・保存するシングルトン
 */
class GameSettingsManager {
public:
    /**
     * @brief 唯一のGameSettingsManagerインスタンスを取得する（未生成なら生成する）
     * @return GameSettingsManagerのインスタンス
     */
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
