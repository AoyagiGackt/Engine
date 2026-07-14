#pragma once
#include "Easing.h"
#include <functional>
#include <vector>
namespace engine {
// float 値をイージングでアニメーションするクラス（シングルトン）
class Tweener {
public:
    using EasingFn = float (*)(float);

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
        int id;
        float* ptr;
        float from, to, duration, elapsed;
        EasingFn fn;
        std::function<void()> onComplete;
        bool done;
    };

    std::vector<Tween> tweens_;
    int nextId_ = 0;
};

} // namespace engine
