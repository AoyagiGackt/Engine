/**
 * @file Audio.h
 * @brief XAudio2を使用した音声ファイルの読み込み・再生を管理するファイル
 *
 * 使い方:
 *   audio->PlayBGM(bgmData);          // BGM再生（ループ）
 *   audio->PlaySE(seData);             // SE再生（複数同時可）
 *   audio->StopBGM();                  // BGM停止
 *   audio->SetBGMVolume(0.5f);         // BGM音量変更
 */
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <wrl/client.h>
#include <xaudio2.h>

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#pragma comment(lib, "Mfplat.lib")
#pragma comment(lib, "Mfreadwrite.lib")
#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "xaudio2.lib")
#pragma comment(lib, "mfuuid.lib")
namespace engine {
/**
 * @brief 再生する音声データを保持する構造体
 */
struct SoundData {
    WAVEFORMATEX wfex; ///< 波形フォーマット
    std::vector<byte> pBuffer; ///< 音声データ本体
    unsigned int bufferSize; ///< バッファのサイズ（バイト）
};

/**
 * @brief オーディオ再生を管理するクラス
 *
 * BGM チャンネル（1本・ループ）と SE チャンネル（複数同時再生）を独立管理します
 */
class Audio {
public:
    /**
     * @brief オーディオエンジンの初期化
     */
    void Initialize();

    /**
     * @brief オーディオエンジンの終了処理
     */
    void Finalize();

    /**
     * @brief 音声ファイルをロードする
     * @param filename ファイルパス（WAV / MP3 等）
     * @return ロード済みの SoundData
     */
    SoundData LoadAudio(const std::string& filename);


    // BGM（1チャンネル、ループ再生）


    /**
     * @brief BGM を再生する（前の BGM は自動停止）
     * @param soundData 再生する音声データ
     * @param loop true でループ再生（デフォルト）
     */
    void PlayBGM(const SoundData& soundData, bool loop = true);

    /**
     * @brief BGM を停止する
     */
    void StopBGM();

    /**
     * @brief BGM の音量を設定する
     * @param volume 0.0f（無音）〜 1.0f（最大）
     */
    void SetBGMVolume(float volume);

    /** @brief 全SEへ適用するカテゴリ音量を設定する */
    void SetSEVolume(float volume);

    /** @brief 現在のBGMカテゴリ音量を返す */
    float GetBGMVolume() const { return bgmCurrentVolume_; }

    /** @brief 現在のSEカテゴリ音量を返す */
    float GetSEVolume() const { return seVolume_; }

    /**
     * @brief BGM の再生速度（ピッチ）を変更する
     * @param speed 1.0f で通常速度
     */
    void SetBGMSpeed(float speed);


    // SE（複数同時再生、自動クリーンアップ）


    /**
     * @brief SE を再生する（複数同時再生可）
     * @param soundData 再生する音声データ
     * @param volume 音量（0.0f〜1.0f、デフォルト 1.0f）
     */
    void PlaySE(const SoundData& soundData, float volume = 1.0f);

    /**
     * @brief 再生中の SE をすべて停止する
     */
    void StopAllSE();


    // BGM フェード


    /**
     * @brief BGM の音量を duration 秒かけて targetVolume へフェードする
     * @param targetVolume 目標音量（0.0f〜1.0f）
     * @param duration     フェードにかかる秒数
     */
    void FadeVolumeTo(float targetVolume, float duration);


    // 毎フレーム更新（Framework::Update から呼ぶ）


    /** @brief フェードアニメーションを進めるdt = 経過秒数（1/60 など）*/
    void Update(float dt);


    // 後方互換 API（PlayWave / StopWave）


    /** @brief BGM として再生する（PlayBGM の別名） */
    void PlayWave(const SoundData& soundData) { PlayBGM(soundData, true); }

    /** @brief BGM を停止する（StopBGM の別名） */
    void StopWave() { StopBGM(); }

    /** @brief BGM の速度を変更する（SetBGMSpeed の別名） */
    void SetPlaybackSpeed(float speed) { SetBGMSpeed(speed); }

private:
    /** @brief SourceVoice を生成するヘルパー */
    IXAudio2SourceVoice* CreateSourceVoice(const SoundData& soundData);

    /** @brief 終了済みの SE Voice を削除する */
    void CleanupFinishedSE();

    Microsoft::WRL::ComPtr<IXAudio2> xAudio2_;
    IXAudio2MasteringVoice* masteringVoice_ = nullptr;

    /** @brief BGM 専用チャンネル */
    IXAudio2SourceVoice* bgmVoice_ = nullptr;

    /** @brief SE チャンネル（複数同時再生） */
    std::vector<IXAudio2SourceVoice*> seVoices_;

    // BGM フェード状態
    float bgmCurrentVolume_ = 1.0f;
    float bgmTargetVolume_ = 1.0f;
    float bgmFadeStartVolume_ = 1.0f;
    float bgmFadeDuration_ = 0.0f;
    float bgmFadeTimer_ = 0.0f;
    float seVolume_ = 1.0f; ///< 個別SE音量へ乗算するカテゴリ音量
};

} // namespace engine
