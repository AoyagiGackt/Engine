#include "AudioBridge.h"
#include "Logger.h"
using namespace engine::game;
using namespace engine;

AudioBridge* AudioBridge::GetInstance()
{
    static AudioBridge instance;
    return &instance;
}

const SoundData* AudioBridge::LoadOrGet(const std::string& path)
{
    auto it = cache_.find(path);
    if (it != cache_.end()) {
        return &it->second;
    }
    if (!audio_) {
        return nullptr;
    }
    auto [inserted, ok] = cache_.emplace(path, audio_->LoadAudio(path));
    return ok ? &inserted->second : nullptr;
}

void AudioBridge::PlaySE(const std::string& path, float volume)
{
    if (!audio_) {
        Logger::LogError("[Graph] PlaySE: Audio未登録（AudioBridge::SetAudio()を呼んだシーンで実行してください）");
        return;
    }
    const SoundData* data = LoadOrGet(path);
    if (!data) {
        Logger::LogError("[Graph] PlaySE: 読み込み失敗: " + path);
        return;
    }
    audio_->PlaySE(*data, volume);
}

void AudioBridge::PlayBGM(const std::string& path, bool loop)
{
    if (!audio_) {
        Logger::LogError("[Graph] PlayBGM: Audio未登録（AudioBridge::SetAudio()を呼んだシーンで実行してください）");
        return;
    }
    const SoundData* data = LoadOrGet(path);
    if (!data) {
        Logger::LogError("[Graph] PlayBGM: 読み込み失敗: " + path);
        return;
    }
    audio_->PlayBGM(*data, loop);
}

void AudioBridge::StopBGM()
{
    if (audio_) {
        audio_->StopBGM();
    }
}
