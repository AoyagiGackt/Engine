#pragma once
#include <Windows.h>
#include <array>

// フレームタイムとFPSをトラッキングするシングルトン
// BeginFrame() / EndFrame() を毎フレーム呼ぶだけで使える
class DebugProfiler {
public:
    static DebugProfiler* GetInstance() {
        static DebugProfiler inst;
        return &inst;
    }

    void BeginFrame() {
        QueryPerformanceCounter(&frameStart_);
    }

    void EndFrame() {
        LARGE_INTEGER now, freq;
        QueryPerformanceCounter(&now);
        QueryPerformanceFrequency(&freq);
        float ms = float(now.QuadPart - frameStart_.QuadPart) / float(freq.QuadPart) * 1000.0f;

        samples_[sampleIdx_] = ms;
        sampleIdx_ = (sampleIdx_ + 1) % kSamples;

        float sum = 0.0f;
        for (float s : samples_) sum += s;
        avgMs_ = sum / kSamples;
        fps_   = avgMs_ > 0.0f ? 1000.0f / avgMs_ : 0.0f;
    }

    float GetFPS()  const { return fps_; }
    float GetMs()   const { return avgMs_; }

private:
    DebugProfiler() = default;
    static constexpr int kSamples = 60;
    std::array<float, kSamples> samples_ = {};
    int    sampleIdx_ = 0;
    float  avgMs_     = 0.0f;
    float  fps_       = 0.0f;
    LARGE_INTEGER frameStart_ = {};
};
