/**
 * @file FrameProfiler.h
 * @brief CPUのフレーム時間とFPSを測る処理（FrameProfiler）
 */
#pragma once
#include <Windows.h>
#include <array>
namespace engine::graphics {
// フレームタイムとFPSをトラッキングするシングルトン
// BeginFrame() / EndFrame() を毎フレーム呼ぶだけで使える
class FrameProfiler {
public:
    /** @brief 共有インスタンスを取得する */
    static FrameProfiler* GetInstance()
    {
        static FrameProfiler inst;
        return &inst;
    }

    /** @brief フレームの計測を始める */
    void BeginFrame()
    {
        // フレーム開始時刻を記録する
        QueryPerformanceCounter(&frameStart_);
    }

    /** @brief フレームの計測を終え、FPSと処理時間を更新する */
    void EndFrame()
    {
        LARGE_INTEGER now, freq;
        // フレーム終了時刻とパフォーマンスカウンタの周波数を取得し、経過時間(ms)を算出する
        QueryPerformanceCounter(&now);
        QueryPerformanceFrequency(&freq);
        float ms = float(now.QuadPart - frameStart_.QuadPart) / float(freq.QuadPart) * kMillisecondsPerSecond;

        samples_[sampleIdx_] = ms;
        sampleIdx_ = (sampleIdx_ + 1) % kSamples;

        float sum = 0.0f;
        for (float s : samples_)
        {
            sum += s;
        }
        avgMs_ = sum / kSamples;
        fps_ = avgMs_ > 0.0f ? kMillisecondsPerSecond / avgMs_ : 0.0f;
    }

    /** @brief FPS を返す */
    float GetFPS() const {
        return fps_;
    }
    /** @brief 1フレームの処理時間（ミリ秒）を返す */
    float GetMs() const {
        return avgMs_;
    }

private:
    static constexpr float kMillisecondsPerSecond = 1000.0f;
    /** @brief 計測前のフレーム統計を生成する */
    FrameProfiler() = default;
    static constexpr int kSamples = 60;
    std::array<float, kSamples> samples_ = { };
    int sampleIdx_ = 0;
    float avgMs_ = 0.0f;
    float fps_ = 0.0f;
    LARGE_INTEGER frameStart_ = { };
};

} // namespace engine::graphics
