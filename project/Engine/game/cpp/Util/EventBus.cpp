/**
 * @file EventBus.cpp
 * @brief EventBusが担当する処理を実装するファイル
 */
#include "EventBus.h"
#include <algorithm>
using namespace engine::game;

EventBus* EventBus::GetInstance()
{
    static EventBus instance;
    return &instance;
}

int EventBus::Subscribe(const std::string& eventName, Callback callback)
{
    int handle = nextHandle_++;
    subscribers_[eventName].push_back({ handle, std::move(callback) });
    return handle;
}

void EventBus::Unsubscribe(const std::string& eventName, int handle)
{
    auto it = subscribers_.find(eventName);
    if (it == subscribers_.end()) {
        return;
    }

    auto& entries = it->second;
    entries.erase(
        std::remove_if(entries.begin(), entries.end(),
            [handle](const Entry& e) { return e.handle == handle; }),
        entries.end());
}

void EventBus::Emit(const std::string& eventName)
{
    auto it = subscribers_.find(eventName);
    if (it == subscribers_.end()) {
        return;
    }

    // コールバック内でSubscribe/Unsubscribeされてもいいようにコピーを回す
    auto entries = it->second;
    for (auto& e : entries) {
        e.callback();
    }
}
