/**
 * @file TimeManager.h
 * @brief 時間倍率とヒットストップを管理するファイル
 */
#pragma once
namespace engine {
/** @brief ゲーム全体の時間倍率とヒットストップ残量を一元管理する */
class TimeManager {
public:
    /** @brief 共有時間管理を返す @return 時間管理のポインタ */
    static TimeManager* GetInstance();

    /** @brief ヒットストップの残りフレームを更新する */
    void Update();

    /** @brief 時間倍率と停止状態を反映したフレーム間隔を返す @return ゲーム更新用の秒数 */
    float GetDeltaTime() const;
    /** @brief 現在の時間倍率を返す @return 時間倍率 */
    float GetTimeScale() const { return timeScale_; }
    /** @brief ヒットストップ中か返す @return 停止中の場合はtrue */
    bool IsHitStopped() const { return hitStopFrames_ > 0; }

    /** @brief ゲーム更新へ適用する時間倍率を設定する @param scale 0で停止、1で通常速度となる倍率 */
    void SetTimeScale(float scale);

    /** @brief 指定フレーム数のヒットストップを要求する @param frames 停止するフレーム数 */
    void RequestHitStop(int frames);

    static constexpr float kBaseDeltaTime = 1.0f / 60.0f;

private:
    TimeManager() = default;
    float timeScale_ = 1.0f;
    int hitStopFrames_ = 0;
};

} // namespace engine
