#pragma once
#include <cmath>

// イージング関数ライブラリ
// t は 0.0〜1.0 の正規化済み時間。戻り値も基本 0.0〜1.0 だが Back/Elastic は範囲外になる場合あり。
// 使い方: float v = Easing::Lerp(startVal, endVal, t, Easing::EaseOutBack);
namespace Easing {

    inline float Linear(float t) { return t; }

    // ── Quad ──
    inline float EaseInQuad(float t)    { return t * t; }
    inline float EaseOutQuad(float t)   { return t * (2.0f - t); }
    inline float EaseInOutQuad(float t) {
        return (t < 0.5f) ? 2.0f * t * t : -1.0f + (4.0f - 2.0f * t) * t;
    }

    // ── Cubic ──
    inline float EaseInCubic(float t)  { return t * t * t; }
    inline float EaseOutCubic(float t) { float u = 1.0f - t; return 1.0f - u * u * u; }
    inline float EaseInOutCubic(float t) {
        return (t < 0.5f) ? 4.0f * t * t * t
                          : 1.0f + (t - 1.0f) * (2.0f * t - 2.0f) * (2.0f * t - 2.0f);
    }

    // ── Sine ──
    inline float EaseInSine(float t)    { return 1.0f - std::cos(t * 1.5707963f); }
    inline float EaseOutSine(float t)   { return std::sin(t * 1.5707963f); }
    inline float EaseInOutSine(float t) { return 0.5f * (1.0f - std::cos(t * 3.1415926f)); }

    // ── Expo ──
    inline float EaseInExpo(float t)  { return (t <= 0.0f) ? 0.0f : std::pow(2.0f, 10.0f * t - 10.0f); }
    inline float EaseOutExpo(float t) { return (t >= 1.0f) ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t); }

    // ── Back（少し行き過ぎてから戻る）──
    inline float EaseInBack(float t) {
        constexpr float c1 = 1.70158f, c3 = c1 + 1.0f;
        return c3 * t * t * t - c1 * t * t;
    }
    inline float EaseOutBack(float t) {
        constexpr float c1 = 1.70158f, c3 = c1 + 1.0f;
        float u = t - 1.0f;
        return 1.0f + c3 * u * u * u + c1 * u * u;
    }
    inline float EaseInOutBack(float t) {
        constexpr float c1 = 1.70158f, c2 = c1 * 1.525f;
        return (t < 0.5f)
            ? (std::pow(2.0f * t, 2.0f) * ((c2 + 1.0f) * 2.0f * t - c2)) * 0.5f
            : (std::pow(2.0f * t - 2.0f, 2.0f) * ((c2 + 1.0f) * (2.0f * t - 2.0f) + c2) + 2.0f) * 0.5f;
    }

    // ── Elastic（バネのように弾む）──
    inline float EaseOutElastic(float t) {
        if (t <= 0.0f) { return 0.0f; }
        if (t >= 1.0f) { return 1.0f; }
        constexpr float c4 = 2.0f * 3.1415926f / 3.0f;
        return std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * c4) + 1.0f;
    }
    inline float EaseInElastic(float t) {
        if (t <= 0.0f) { return 0.0f; }
        if (t >= 1.0f) { return 1.0f; }
        constexpr float c4 = 2.0f * 3.1415926f / 3.0f;
        return -std::pow(2.0f, 10.0f * t - 10.0f) * std::sin((t * 10.0f - 10.75f) * c4);
    }

    // ── Bounce（床で弾む）──
    inline float EaseOutBounce(float t) {
        constexpr float n1 = 7.5625f, d1 = 2.75f;
        if (t < 1.0f / d1)        { return n1 * t * t; }
        else if (t < 2.0f / d1)   { t -= 1.5f  / d1; return n1 * t * t + 0.75f; }
        else if (t < 2.5f / d1)   { t -= 2.25f / d1; return n1 * t * t + 0.9375f; }
        else                       { t -= 2.625f/ d1; return n1 * t * t + 0.984375f; }
    }
    inline float EaseInBounce(float t) { return 1.0f - EaseOutBounce(1.0f - t); }

    // ── 汎用補間ヘルパー ──
    // 例: float v = Easing::Lerp(0.0f, 100.0f, t, Easing::EaseOutBounce);
    template <typename Fn>
    inline float Lerp(float a, float b, float t, Fn easingFn) {
        return a + (b - a) * easingFn(t);
    }

} // namespace Easing
