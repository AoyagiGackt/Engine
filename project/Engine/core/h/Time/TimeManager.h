/**
 * @file TimeManager.h
 * @brief 時間倍率とヒットストップを管理するファイル
 */
#pragma once
#include "EngineConstants.h"
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
    float GetTimeScale() const {
        return timeScale_;
    }
    /**
     * @brief ヒットストップ中か返す @return 停止中の場合はtrue
     * @note スロー中に間引かれたフレームも停止扱いになる（固定dtで動く処理をまとめて遅くするため）
     */
    bool IsHitStopped() const {
        return hitStopFrames_ > 0 || slowSkipFrame_;
    }

    /** @brief ゲーム更新へ適用する時間倍率を設定する @param scale 0で停止、1で通常速度となる倍率 */
    void SetTimeScale(float scale);

    /** @brief 指定フレーム数のヒットストップを要求する @param frames 停止するフレーム数 */
    void RequestHitStop(int frames);

    /**
     * @brief ヒットストップ明けに続くスローを要求する
     * @param seconds スローを続ける実時間（秒、ヒットストップ中は減らない）
     * @param scale   ゲームを進める割合（0.5なら2フレームに1回だけ進める）
     * @note 各処理が固定dtで動くため、フレームを間引くことで全体を遅くする
     */
    void RequestSlowMotion(float seconds, float scale);
    /** @brief スロー中か返す @return スローの残り時間がある場合はtrue */
    bool IsSlowMotion() const {
        return slowSeconds_ > 0.0f;
    }

    static constexpr float kBaseDeltaTime = engineConstants::kFrameDeltaTime;

private:
    /** @brief 時間倍率と停止要求を既定状態にする */
    TimeManager() = default;
    float timeScale_ = 1.0f;
    int hitStopFrames_ = 0;
    float slowSeconds_ = 0.0f; ///< スローの残り実時間
    float slowScale_ = 1.0f; ///< スロー中にゲームを進める割合
    float slowAccumulator_ = 0.0f; ///< 進めたフレームの端数（1を超えたらそのフレームを進める）
    bool slowSkipFrame_ = false; ///< このフレームはスローで間引かれたか
};

} // namespace engine
