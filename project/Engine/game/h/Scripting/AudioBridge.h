/**
 * @file AudioBridge.h
 * @brief ノードグラフからAudioを鳴らすための橋渡しシングルトン
 * @note GraphRuntimeはSceneへのポインタを持たないため、EnemyRegistryと同じパターンで
 * Scene::Initialize()が現在のAudio*を登録し、ノード実行側はここ経由で呼び出す
 */
#pragma once
#include "Audio.h"
#include <map>
#include <string>
namespace engine::game {

/**
 * @brief AudioBridge に関する型を提供する
 * @details AudioBridge が扱うデータと操作の責務をまとめる
 */
class AudioBridge {
public:
    /**
     * @brief GetInstance の結果を取得する
     * @return 処理結果
     */
    static AudioBridge* GetInstance();

    /** @brief 現在のシーンのAudioを登録する（Scene::Initialize()から呼ぶ） */
    void SetAudio(engine::Audio* audio) { audio_ = audio; }

    /** @brief SEを再生する（未ロードなら初回のみファイルを読み込みキャッシュする） */
    void PlaySE(const std::string& path, float volume);

    /** @brief BGMを再生する（前のBGMは自動停止） */
    void PlayBGM(const std::string& path, bool loop);

    /** @brief BGMを停止する */
    void StopBGM();

private:
    AudioBridge() = default;
    AudioBridge(const AudioBridge&) = delete;
    AudioBridge& operator=(const AudioBridge&) = delete;

    /** @brief pathのSoundDataをキャッシュから探す無ければAudio::LoadAudio()で読み込んでキャッシュする */
    const engine::SoundData* LoadOrGet(const std::string& path);

    engine::Audio* audio_ = nullptr;
    std::map<std::string, engine::SoundData> cache_;
};

} // namespace engine::game
