#pragma once
#include "Easing.h"
#include <functional>
#include <vector>

// float 値を Easing で時間をかけてアニメーションするクラス
// Game::Update() から Update(dt) を毎フレーム呼ぶこと
//
// 使い方:
//   // HPバーを 0.3秒かけて目標幅に補間
//   Tweener::GetInstance()->To(&hpBarW_, targetW, 0.3f, Easing::EaseOutQuad);
//
//   // 完了コールバック付き
//   Tweener::GetInstance()->To(&alpha_, 0.0f, 0.5f, Easing::EaseInSine,
//                              []{ sceneManager->ChangeScene("TITLE"); });
class Tweener {
public:
    using EasingFn = float(*)(float);

    static Tweener* GetInstance();

    // *ptr の現在値から to まで duration 秒でアニメーション
    int To(float* ptr, float to, float duration,
           EasingFn fn = Easing::Linear,
           std::function<void()> onComplete = nullptr);

    // from を明示する版（*ptr を即座に from に設定してからアニメーション開始）
    int FromTo(float* ptr, float from, float to, float duration,
               EasingFn fn = Easing::Linear,
               std::function<void()> onComplete = nullptr);

    void Cancel(int id);
    void CancelAll(float* ptr); // 同じ float への全トゥイーンを停止
    void Clear();

    // TimeManager::GetDeltaTime() を渡すとヒットストップ中も自動で止まる
    void Update(float dt);

private:
    Tweener() = default;

    struct Tween {
        int    id;
        float* ptr;
        float  from, to, duration, elapsed;
        EasingFn fn;
        std::function<void()> onComplete;
        bool   done;
    };

    std::vector<Tween> tweens_;
    int nextId_ = 0;
};
