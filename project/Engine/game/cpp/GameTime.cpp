#include "GameTime.h"
#include "GameConstants.h"

void GameTime::Initialize()
{
    elapsedMinutes_ = 0.0f;
}

void GameTime::Update(float minutesPerSecond)
{
    if (IsCleared()) {
        return;
    }

    float minutesPerFrame = minutesPerSecond / GameConstants::kTargetFps;
    elapsedMinutes_ += minutesPerFrame;

    // 終了時刻を超えたらクランプ
    if (elapsedMinutes_ > kTotalGameMinutes) {
        elapsedMinutes_ = kTotalGameMinutes;
    }
}

int GameTime::GetHour() const
{
    int totalMinutes = kStartMinutes + static_cast<int>(elapsedMinutes_);
    return (totalMinutes / 60) % 24;
}

int GameTime::GetMinute() const
{
    int totalMinutes = kStartMinutes + static_cast<int>(elapsedMinutes_);
    return totalMinutes % 60;
}

void GameTime::SkipMinutes(float minutes)
{
    elapsedMinutes_ += minutes;
    if (elapsedMinutes_ > kTotalGameMinutes) {
        elapsedMinutes_ = kTotalGameMinutes;
    }
}
