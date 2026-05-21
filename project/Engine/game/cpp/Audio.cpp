#include "Audio.h"
#include "Logger.h"
#include "StringUtlity.h"
#include <algorithm>
#include <cassert>

using namespace Microsoft::WRL;

// =====================================================
// 初期化 / 終了
// =====================================================

void Audio::Initialize()
{
    HRESULT hr;

    hr = MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);
    assert(SUCCEEDED(hr));

    hr = XAudio2Create(&xAudio2_, 0, XAUDIO2_DEFAULT_PROCESSOR);
    assert(SUCCEEDED(hr));

    hr = xAudio2_->CreateMasteringVoice(&masteringVoice_);
    assert(SUCCEEDED(hr));

    Logger::Log("Audio System Initialized.\n");
}

void Audio::Finalize()
{
    if (!xAudio2_) {
        return;
    }

    StopBGM();
    StopAllSE();

    if (masteringVoice_) {
        masteringVoice_->DestroyVoice();
        masteringVoice_ = nullptr;
    }

    xAudio2_.Reset();
    MFShutdown();
}

// =====================================================
// ロード
// =====================================================

SoundData Audio::LoadAudio(const std::string& filename)
{
    HRESULT hr;
    SoundData soundData = {};

    std::wstring wFilename = StringUtility::ConvertString(filename);
    ComPtr<IMFSourceReader> pSourceReader;
    hr = MFCreateSourceReaderFromURL(wFilename.c_str(), nullptr, &pSourceReader);
    
    if (FAILED(hr)) {
        Logger::Log("Error: Failed to open audio file: " + filename + "\n");
        assert(false);
        return soundData;
    }

    ComPtr<IMFMediaType> pMediaType;
    MFCreateMediaType(&pMediaType);
    pMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    pMediaType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    hr = pSourceReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, pMediaType.Get());
    
    if (FAILED(hr)) {
        Logger::Log("Error: Failed to set media type for: " + filename + "\n");
        assert(false);
        return soundData;
    }

    ComPtr<IMFMediaType> pOutputMediaType;
    hr = pSourceReader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, &pOutputMediaType);
    assert(SUCCEEDED(hr));

    WAVEFORMATEX* wfex = &soundData.wfex;
    wfex->wFormatTag = WAVE_FORMAT_PCM;

    UINT32 temp = 0;
    pOutputMediaType->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &temp);
    wfex->nChannels = (WORD)temp;
    pOutputMediaType->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &temp);
    wfex->nSamplesPerSec = temp;
    pOutputMediaType->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &temp);
    wfex->wBitsPerSample = (WORD)temp;
    pOutputMediaType->GetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, &temp);
    wfex->nBlockAlign = (WORD)temp;
    pOutputMediaType->GetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, &temp);
    wfex->nAvgBytesPerSec = temp;
    wfex->cbSize = 0;

    std::vector<byte> rawData;
    while (true) {
        DWORD flags = 0;
        ComPtr<IMFSample> pSample;
        hr = pSourceReader->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &flags, nullptr, &pSample);
        
        if (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM)) {
            break;
        }

        if (!pSample) {
            continue;
        }

        ComPtr<IMFMediaBuffer> pBuffer;
        pSample->ConvertToContiguousBuffer(&pBuffer);
        BYTE* pBufferPtr = nullptr;
        DWORD currentLength = 0;
        pBuffer->Lock(&pBufferPtr, nullptr, &currentLength);
        size_t oldSize = rawData.size();
        rawData.resize(oldSize + currentLength);
        memcpy(rawData.data() + oldSize, pBufferPtr, currentLength);
        pBuffer->Unlock();
    }

    soundData.pBuffer = std::move(rawData);
    soundData.bufferSize = static_cast<unsigned int>(soundData.pBuffer.size());
    return soundData;
}

// =====================================================
// ヘルパー
// =====================================================

IXAudio2SourceVoice* Audio::CreateSourceVoice(const SoundData& soundData)
{
    IXAudio2SourceVoice* voice = nullptr;
    HRESULT hr = xAudio2_->CreateSourceVoice(&voice, &soundData.wfex);
    assert(SUCCEEDED(hr));
    return voice;
}

void Audio::CleanupFinishedSE()
{
    seVoices_.erase(
        std::remove_if(seVoices_.begin(), seVoices_.end(), [](IXAudio2SourceVoice* v) {
            XAUDIO2_VOICE_STATE state;
            v->GetState(&state);
            
            if (state.BuffersQueued == 0) {
                v->DestroyVoice();
                return true;
            }

            return false;
        }),
        seVoices_.end());
}

// =====================================================
// BGM
// =====================================================

void Audio::PlayBGM(const SoundData& soundData, bool loop)
{
    // 前の BGM を停止してから新しいものを再生
    StopBGM();

    bgmVoice_ = CreateSourceVoice(soundData);

    XAUDIO2_BUFFER buffer = {};
    buffer.pAudioData = soundData.pBuffer.data();
    buffer.AudioBytes = soundData.bufferSize;
    buffer.Flags = XAUDIO2_END_OF_STREAM;
    buffer.LoopCount = loop ? XAUDIO2_LOOP_INFINITE : 0;

    HRESULT hr = bgmVoice_->SubmitSourceBuffer(&buffer);
    assert(SUCCEEDED(hr));
    bgmVoice_->Start(0);
}

void Audio::StopBGM()
{
    if (bgmVoice_) {
        bgmVoice_->Stop();
        bgmVoice_->DestroyVoice();
        bgmVoice_ = nullptr;
    }
}

void Audio::SetBGMVolume(float volume)
{
    if (bgmVoice_) {
        bgmVoice_->SetVolume(volume);
    }
}

void Audio::SetBGMSpeed(float speed)
{
    if (bgmVoice_) {
        bgmVoice_->SetFrequencyRatio(speed);
    }
}

// =====================================================
// SE
// =====================================================

void Audio::PlaySE(const SoundData& soundData, float volume)
{
    // 再生終了済みの SE を先にクリーンアップ
    CleanupFinishedSE();

    IXAudio2SourceVoice* voice = CreateSourceVoice(soundData);
    voice->SetVolume(volume);

    XAUDIO2_BUFFER buffer = {};
    buffer.pAudioData = soundData.pBuffer.data();
    buffer.AudioBytes = soundData.bufferSize;
    buffer.Flags = XAUDIO2_END_OF_STREAM;
    buffer.LoopCount = 0; // SE はループなし

    HRESULT hr = voice->SubmitSourceBuffer(&buffer);
    assert(SUCCEEDED(hr));
    voice->Start(0);

    seVoices_.push_back(voice);
}

void Audio::StopAllSE()
{
    for (auto* v : seVoices_) {
        v->Stop();
        v->DestroyVoice();
    }
    seVoices_.clear();
}
