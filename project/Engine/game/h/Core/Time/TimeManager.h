#pragma once
namespace engine {
// タイムスケール制御 & ヒットストップ管理
// - SetTimeScale(0.3f) でスローモーション
// - RequestHitStop(4)  で 4 フレーム一時停止（連続リクエストは最大値を採用）
// - GetDeltaTime()     はヒットストップ中 0、スロー中は比例値を返す
class TimeManager {
public:
    static TimeManager* GetInstance();

    // 毎フレーム Game::Update() の先頭で呼ぶ
    void Update();

    // timers や物理に渡す dt（ヒットストップ中=0, 通常=1/60, スロー=比例値）
    float GetDeltaTime() const;
    float GetTimeScale() const { return timeScale_; }
    bool IsHitStopped() const { return hitStopFrames_ > 0; }

    // 0.0=完全停止 / 0.5=スロー / 1.0=通常
    void SetTimeScale(float scale);

    // N フレーム時間を止める（既存残量との最大値を採用）
    void RequestHitStop(int frames);

    static constexpr float kBaseDeltaTime = 1.0f / 60.0f;

private:
    TimeManager() = default;
    float timeScale_ = 1.0f;
    int hitStopFrames_ = 0;
};

} // namespace engine
