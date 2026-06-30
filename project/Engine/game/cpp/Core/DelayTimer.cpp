#include "DelayTimer.h"
#include <algorithm>
using namespace engine;

DelayTimer* DelayTimer::GetInstance() {
    static DelayTimer instance;
    return &instance;
}

void DelayTimer::After(float delay, std::function<void()> callback) {
    timers_.push_back({ -1, delay, std::move(callback), false });
}

int DelayTimer::AfterCancellable(float delay, std::function<void()> callback) {
    int id = nextId_++;
    timers_.push_back({ id, delay, std::move(callback), false });
    return id;
}

void DelayTimer::Cancel(int id) {
    for (auto& e : timers_) {
        if (e.id == id) { e.done = true; }
    }
}

void DelayTimer::Update(float dt) {
    for (auto& e : timers_) {
        if (e.done) { continue; }
        e.remaining -= dt;
        if (e.remaining <= 0.0f) {
            e.callback();
            e.done = true;
        }
    }
    timers_.erase(
        std::remove_if(timers_.begin(), timers_.end(),
                       [](const Entry& e) { return e.done; }),
        timers_.end());
}

void DelayTimer::Clear() {
    timers_.clear();
}
