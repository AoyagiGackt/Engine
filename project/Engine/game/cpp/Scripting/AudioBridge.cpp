/**
 * @file AudioBridge.cpp
 * @brief ノードグラフからSE・BGMを鳴らすための橋渡し（AudioBridge）の実装
 */
#include "AudioBridge.h"
#include "Logger.h"

namespace engine::game {
/** @brief 唯一のAudioBridgeインスタンスを取得する（未生成なら生成する） */
AudioBridge* AudioBridge::GetInstance()
{
    static AudioBridge instance;
    return &instance;
}

/** @brief pathのSoundDataをキャッシュから探す無ければAudio::LoadAudio()で読み込んでキャッシュする */
const SoundData* AudioBridge::LoadOrGet(const std::string& path)
{
    auto it = cache_.find(path);
    if (it != cache_.end())
    {
        return &it->second;
    }
    if (!audio_)
    {
        return nullptr;
    }
    auto [inserted, ok] = cache_.emplace(path, audio_->LoadAudio(path));
    return ok ? &inserted->second : nullptr;
}

/** @brief SEを再生する（未ロードなら初回のみファイルを読み込みキャッシュする） */
void AudioBridge::PlaySE(const std::string& path, float volume)
{
    if (!audio_)
    {
        Logger::LogError("[Graph] PlaySE: Audio未登録（AudioBridge::SetAudio()を呼んだシーンで実行してください）");
        return;
    }
    const SoundData* data = LoadOrGet(path);
    if (!data)
    {
        Logger::LogError("[Graph] PlaySE: 読み込み失敗: " + path);
        return;
    }
    audio_->PlaySE(*data, volume);
}

/** @brief BGMを再生する（前のBGMは自動停止） */
void AudioBridge::PlayBGM(const std::string& path, bool loop)
{
    if (!audio_)
    {
        Logger::LogError("[Graph] PlayBGM: Audio未登録（AudioBridge::SetAudio()を呼んだシーンで実行してください）");
        return;
    }
    const SoundData* data = LoadOrGet(path);
    if (!data)
    {
        Logger::LogError("[Graph] PlayBGM: 読み込み失敗: " + path);
        return;
    }
    audio_->PlayBGM(*data, loop);
}

/** @brief BGMを停止する */
void AudioBridge::StopBGM()
{
    if (audio_)
    {
        audio_->StopBGM();
    }
}

} // namespace engine::game
