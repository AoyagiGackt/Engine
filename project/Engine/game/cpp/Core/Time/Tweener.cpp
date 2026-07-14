#include "Tweener.h"
#include <algorithm>
using namespace engine;

Tweener* Tweener::GetInstance()
{
    static Tweener instance;
    return &instance;
}

int Tweener::To(float* ptr, float to, float duration, EasingFn fn, std::function<void()> onComplete)
{
    return FromTo(ptr, *ptr, to, duration, fn, std::move(onComplete));
}

int Tweener::FromTo(float* ptr, float from, float to, float duration,
    EasingFn fn, std::function<void()> onComplete)
{
    CancelAll(ptr); // 同一 float への既存トゥイーンを停止
    int id = nextId_++;
    *ptr = from;
    EasingFn safeFn = fn ? fn : Easing::Linear;
    tweens_.push_back({ id, ptr, from, to, duration, 0.0f, safeFn, std::move(onComplete), false });
    return id;
}

void Tweener::Cancel(int id)
{
    for (auto& t : tweens_) {
        if (t.id == id) {
            t.done = true;
        }
    }
}

void Tweener::CancelAll(float* ptr)
{
    for (auto& t : tweens_) {
        if (t.ptr == ptr) {
            t.done = true;
        }
    }
}

void Tweener::Clear()
{
    tweens_.clear();
}

void Tweener::Update(float dt)
{
    for (auto& t : tweens_) {
        if (t.done) {
            continue;
        }
        t.elapsed += dt;
        float progress = (t.duration > 0.0f) ? (t.elapsed / t.duration) : 1.0f;
        if (progress >= 1.0f) {
            *t.ptr = t.to;
            t.done = true;
            if (t.onComplete) {
                t.onComplete();
            }
        } else {
            *t.ptr = Easing::Lerp(t.from, t.to, progress, t.fn);
        }
    }
    tweens_.erase(
        std::remove_if(tweens_.begin(), tweens_.end(),
            [](const Tween& t) { return t.done; }),
        tweens_.end());
}
