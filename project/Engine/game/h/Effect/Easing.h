/** @file Easing.h
 * @brief 正規化した進行度を加速と減速の曲線へ変換する補間関数
 */
#pragma once
#include "GameConstants.h"
#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
namespace engine::Easing {
namespace Detail {
inline constexpr float kHalfCurveScale = 2.0f;
inline constexpr float kCubicHalfScale = 4.0f;
inline constexpr float kBackOvershoot = 1.70158f;
inline constexpr float kBackCubicWeight = kBackOvershoot + 1.0f;
inline constexpr float kBackInOutWeight = kBackOvershoot * 1.525f;
inline constexpr float kExponentialBase = 2.0f;
inline constexpr float kExponentialDecay = 10.0f;
inline constexpr float kElasticAngularFrequency = GameConstants::kTwoPi / 3.0f;
inline constexpr float kElasticOutPhase = 0.75f;
inline constexpr float kElasticInPhase = kExponentialDecay + kElasticOutPhase;
inline constexpr float kSquareExponent = 2.0f;
inline constexpr float kBounceScale = 7.5625f;
inline constexpr float kBounceDivisor = 2.75f;

/** @brief バウンド曲線の区間上限と放物線の中心および最低値 */
struct BounceSegment {
    float end;
    float offset;
    float height;
};
inline constexpr BounceSegment kBounceSegments[] = {
    { 1.0f / kBounceDivisor, 0.0f, 0.0f },
    { 2.0f / kBounceDivisor, 1.5f / kBounceDivisor, 0.75f },
    { 2.5f / kBounceDivisor, 2.25f / kBounceDivisor, 0.9375f },
    { std::numeric_limits<float>::infinity(), 2.625f / kBounceDivisor, 0.984375f },
};

/** @brief 指数減衰と正弦波を組み合わせた弾性変位を求める */
inline float ElasticWave(float exponent, float phase)
{
    return std::pow(kExponentialBase, exponent) * std::sin(phase * kElasticAngularFrequency);
}
} // namespace Detail

/** @brief 加減速を付けずに進行度を返す */
inline float Linear(float t) {
    return t;
}
/** @brief 二次曲線でゆっくり開始する */
inline float EaseInQuad(float t) {
    return t * t;
}
/** @brief 二次曲線でゆっくり停止する */
inline float EaseOutQuad(float t) {
    return t * (Detail::kHalfCurveScale - t);
}
/** @brief 前半を加速し後半を減速する二次曲線を求める */
inline float EaseInOutQuad(float t)
{
    using namespace Detail;
    return t < 0.5f ? kHalfCurveScale * t * t : -1.0f + (kCubicHalfScale - kHalfCurveScale * t) * t;
}
/** @brief 三次曲線でゆっくり開始する */
inline float EaseInCubic(float t) {
    return t * t * t;
}
/** @brief 三次曲線でゆっくり停止する */
inline float EaseOutCubic(float t) {
    const float u = 1.0f - t;
    return 1.0f - u * u * u;
}
/** @brief 前半を加速し後半を減速する三次曲線を求める */
inline float EaseInOutCubic(float t)
{
    using namespace Detail;
    return t < 0.5f ? kCubicHalfScale * t * t * t
        : 1.0f + (t - 1.0f) * (kHalfCurveScale * t - kHalfCurveScale) * (kHalfCurveScale * t - kHalfCurveScale);
}
/** @brief 始端と終端の速度をゼロにする三次曲線を求める */
inline float SmoothStep(float t)
{
    constexpr float kCubicWeight = 3.0f;
    return t * t * (kCubicWeight - Detail::kHalfCurveScale * t);
}
/** @brief 正弦曲線でゆっくり開始する */
inline float EaseInSine(float t) {
    return 1.0f - std::cos(t * GameConstants::kHalfPi);
}
/** @brief 正弦曲線でゆっくり停止する */
inline float EaseOutSine(float t) {
    return std::sin(t * GameConstants::kHalfPi);
}
/** @brief 正弦曲線で滑らかに加速と減速を行う */
inline float EaseInOutSine(float t) {
    return 0.5f * (1.0f - std::cos(t * GameConstants::kPi));
}
/** @brief 指数曲線でゆっくり開始する */
inline float EaseInExpo(float t) {
    return t <= 0.0f ? 0.0f : std::pow(Detail::kExponentialBase, Detail::kExponentialDecay * t - Detail::kExponentialDecay);
}
/** @brief 指数曲線でゆっくり停止する */
inline float EaseOutExpo(float t) {
    return t >= 1.0f ? 1.0f : 1.0f - std::pow(Detail::kExponentialBase, -Detail::kExponentialDecay * t);
}
/** @brief 始端から一度逆へ動いてから加速する */
inline float EaseInBack(float t) {
    return Detail::kBackCubicWeight * t * t * t - Detail::kBackOvershoot * t * t;
}
/** @brief 終端を少し通り過ぎてから戻る */
inline float EaseOutBack(float t)
{
    const float u = t - 1.0f;
    return 1.0f + Detail::kBackCubicWeight * u * u * u + Detail::kBackOvershoot * u * u;
}
/** @brief 始端と終端に行き過ぎを付けた加減速曲線を求める */
inline float EaseInOutBack(float t)
{
    using namespace Detail;
    return t < 0.5f
        ? (std::pow(kHalfCurveScale * t, kSquareExponent) * ((kBackInOutWeight + 1.0f) * kHalfCurveScale * t - kBackInOutWeight)) * 0.5f
        : (std::pow(kHalfCurveScale * t - kHalfCurveScale, kSquareExponent) * ((kBackInOutWeight + 1.0f) * (kHalfCurveScale * t - kHalfCurveScale) + kBackInOutWeight) + kHalfCurveScale) * 0.5f;
}
/** @brief 減衰する弾性振動を付けて終端へ収束する */
inline float EaseOutElastic(float t)
{
    if (t <= 0.0f)
    {
        return 0.0f;
    }
    if (t >= 1.0f)
    {
        return 1.0f;
    }
    return Detail::ElasticWave(-Detail::kExponentialDecay * t, t * Detail::kExponentialDecay - Detail::kElasticOutPhase) + 1.0f;
}
/** @brief 弾性振動から始まり終端へ加速する */
inline float EaseInElastic(float t)
{
    if (t <= 0.0f)
    {
        return 0.0f;
    }
    if (t >= 1.0f)
    {
        return 1.0f;
    }
    return -Detail::ElasticWave(Detail::kExponentialDecay * t - Detail::kExponentialDecay, t * Detail::kExponentialDecay - Detail::kElasticInPhase);
}
/** @brief 区間定義表に従い小さくなるバウンドを再現する */
inline float EaseOutBounce(float t)
{
    const Detail::BounceSegment* segment = &Detail::kBounceSegments[std::size(Detail::kBounceSegments) - 1];
    for (const Detail::BounceSegment& candidate : Detail::kBounceSegments)
    {
        if (t < candidate.end)
        {
            segment = &candidate;
            break;
        }
    }
    t -= segment->offset;
    return Detail::kBounceScale * t * t + segment->height;
}
/** @brief バウンド曲線を反転して跳ねながら開始する */
inline float EaseInBounce(float t) {
    return 1.0f - EaseOutBounce(1.0f - t);
}
/** @brief 0〜duration の区間のうち、時刻 previous から current までの1フレームで進んだ割合を返す（区間の後ろは数えない） */
inline float ProgressDuring(float previous, float current, float duration) {
    return (std::min)(current / duration, 1.0f) - previous / duration;
}
/** @brief 指定した加減速曲線を使ってスカラー値を補間する */
template <typename Fn>
inline float Lerp(float a, float b, float t, const Fn& easingFn) {
    return a + (b - a) * easingFn(t);
}
} // namespace engine::Easing
