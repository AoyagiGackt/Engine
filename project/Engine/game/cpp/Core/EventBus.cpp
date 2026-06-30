#include "EventBus.h"
#include <algorithm>
using namespace engine;

EventBus* EventBus::GetInstance() {
    static EventBus instance;
    return &instance;
}

int EventBus::On(const std::string& event, std::function<void()> callback) {
    int id = nextId_++;
    subs_[event].push_back({ id, std::move(callback) });
    return id;
}

void EventBus::Off(int id) {
    for (auto& [_, list] : subs_) {
        for (auto& s : list) {
            if (s.id == id) { s.removed = true; return; }
        }
    }
}

void EventBus::Emit(const std::string& event) {
    auto it = subs_.find(event);
    if (it == subs_.end()) { return; }

    // イテレーション中に Off() されてもクラッシュしないようコピーして呼ぶ
    auto list = it->second;
    for (auto& s : list) {
        if (!s.removed) { s.callback(); }
    }

    // removed フラグが立ったものをまとめて削除
    auto& orig = subs_[event];
    orig.erase(std::remove_if(orig.begin(), orig.end(),
        [](const Subscription& s) { return s.removed; }), orig.end());
}

void EventBus::Clear() {
    subs_.clear();
    nextId_ = 0;
}
