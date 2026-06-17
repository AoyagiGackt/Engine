#pragma once
#include "MakeAffine.h"

// カメラ振動エフェクト
// Request() で振動を開始し、Update() を毎フレーム呼んでオフセットを受け取る。
// カメラの最終座標にオフセットを加算する（スムージングの後に適用すること）。
class CameraShaker {
public:
    // intensity: 振幅（ワールド単位）  duration: 持続秒数
    // 既存の揺れより強い要求のみ上書き
    void Request(float intensity, float duration);

    // 毎フレーム呼ぶ。現在フレームの揺れオフセットを返す
    Vector3 Update(float dt);

    bool IsShaking() const { return timer_ > 0.0f; }

private:
    float timer_     = 0.0f;
    float duration_  = 0.0f;
    float intensity_ = 0.0f;
    int   seed_      = 0;
};
