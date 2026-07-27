/**
 * @file TimeManager.cpp
 * @brief 時間倍率とヒットストップの残フレーム管理（TimeManager）の実装
 */
#include "TimeManager.h"
#include <algorithm>
using namespace engine;

TimeManager* TimeManager::GetInstance()
{
    static TimeManager instance;
    return &instance;
}

void TimeManager::Update()
{
    if (hitStopFrames_ > 0) {
        --hitStopFrames_;
    }
}

float TimeManager::GetDeltaTime() const
{
    if (hitStopFrames_ > 0) {
        return 0.0f;
    }
    return kBaseDeltaTime * timeScale_;
}

void TimeManager::SetTimeScale(float scale)
{
    timeScale_ = std::clamp(scale, 0.0f, 10.0f);
}

void TimeManager::RequestHitStop(int frames)
{
    hitStopFrames_ = std::max(hitStopFrames_, frames);
}
