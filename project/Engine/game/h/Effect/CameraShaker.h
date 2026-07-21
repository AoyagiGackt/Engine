/**
 * @file CameraShaker.h
 * @brief 時間制御されたカメラ振動オフセットを生成するファイル
 */
#pragma once
#include "MakeAffine.h"
namespace engine::game {
/** @brief 振動要求を保持し、フレームごとのカメラ位置オフセットを生成する */
class CameraShaker {
public:
    /** @brief カメラ振動を開始する @param intensity 最大振幅 @param duration 振動時間  単位は秒 */
    void Request(float intensity, float duration);

    /** @brief 振動時間を進めて現在の位置オフセットを返す @param dt フレーム間隔  単位は秒 @return カメラ位置へ加算するオフセット */
    Vector3 Update(float dt);

    /** @brief 振動中か返す @return 残り時間がある場合はtrue */
    bool IsShaking() const { return timer_ > 0.0f; }

private:
    float timer_ = 0.0f;
    float duration_ = 0.0f;
    float intensity_ = 0.0f;
    int seed_ = 0;
};

} // namespace engine::game
