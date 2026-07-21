/**
 * @file TimeManager.cpp
 * @brief TimeManagerのエンジン基盤の初期化と状態管理に関する具体的な処理を実装するファイル
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
