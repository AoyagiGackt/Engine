/**
 * @file TimeManager.cpp
 * @brief 時間倍率とヒットストップの残フレーム管理（TimeManager）の実装
 */
#include "TimeManager.h"
#include <algorithm>

namespace engine {
/** @brief 共有時間管理を返す */
TimeManager* TimeManager::GetInstance()
{
    static TimeManager instance;
    return &instance;
}

/** @brief ヒットストップの残りフレームを更新する */
void TimeManager::Update()
{
    slowSkipFrame_ = false;
    if (hitStopFrames_ > 0)
    {
        --hitStopFrames_;
        return;
    }
    // ヒットストップが明けてからスローを消化する（止め→ゆっくり→元の速さの順になる）
    if (slowSeconds_ > 0.0f)
    {
        slowSeconds_ = (std::max)(slowSeconds_ - kBaseDeltaTime, 0.0f);
        slowAccumulator_ += slowScale_;
        if (slowAccumulator_ >= 1.0f)
        {
            slowAccumulator_ -= 1.0f;
        }
        else
        {
            slowSkipFrame_ = true;
        }
    }
}

/** @brief 時間倍率と停止状態を反映したフレーム間隔を返す */
float TimeManager::GetDeltaTime() const
{
    if (IsHitStopped())
    {
        return 0.0f;
    }
    return kBaseDeltaTime * timeScale_;
}

/** @brief ゲーム更新へ適用する時間倍率を設定する */
void TimeManager::SetTimeScale(float scale)
{
    constexpr float kMaxTimeScale = 10.0f;
    timeScale_ = std::clamp(scale, 0.0f, kMaxTimeScale);
}

/** @brief 指定フレーム数のヒットストップを要求する */
void TimeManager::RequestHitStop(int frames)
{
    hitStopFrames_ = std::max(hitStopFrames_, frames);
}

/** @brief ヒットストップ明けに続くスローを要求する */
void TimeManager::RequestSlowMotion(float seconds, float scale)
{
    constexpr float kMinSlowScale = 0.05f; // 完全に止まるとヒットストップと区別が付かないため下限を設ける
    slowSeconds_ = (std::max)(slowSeconds_, seconds);
    slowScale_ = std::clamp(scale, kMinSlowScale, 1.0f);
    slowAccumulator_ = 0.0f;
}

} // namespace engine
