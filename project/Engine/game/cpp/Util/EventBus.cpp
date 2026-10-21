/**
 * @file EventBus.cpp
 * @brief 文字列イベント名によるPublish/Subscribe（EventBus）の実装
 */
#include "EventBus.h"
#include <algorithm>

namespace engine::game {
/** @brief 唯一のEventBusインスタンスを取得する（未生成なら生成する） */
EventBus* EventBus::GetInstance()
{
    static EventBus instance;
    return &instance;
}

/** @brief イベントを購読する */
int EventBus::Subscribe(const std::string& eventName, Callback callback)
{
    int handle = nextHandle_++;
    subscribers_[eventName].push_back({ handle, std::move(callback) });
    return handle;
}

/** @brief 購読を解除する */
void EventBus::Unsubscribe(const std::string& eventName, int handle)
{
    auto it = subscribers_.find(eventName);
    if (it == subscribers_.end())
    {
        return;
    }

    auto& entries = it->second;
    entries.erase(
        std::remove_if(entries.begin(), entries.end(),
            [handle](const Entry& e) {
                return e.handle == handle;
            }),
        entries.end());
}

/** @brief イベントを発火し、購読者のコールバックを同期的に全て呼ぶ */
void EventBus::Emit(const std::string& eventName)
{
    auto it = subscribers_.find(eventName);
    if (it == subscribers_.end())
    {
        return;
    }

    // コールバック内でSubscribe/Unsubscribeされてもいいようにコピーを回す
    auto entries = it->second;
    for (auto& e : entries)
    {
        e.callback();
    }
}

} // namespace engine::game
