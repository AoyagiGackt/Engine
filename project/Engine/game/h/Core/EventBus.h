#pragma once
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
namespace engine {
// イベントバス（シングルトン）
class EventBus {
public:
    static EventBus* GetInstance();

    // イベント購読。返り値の ID を Off() に渡すと解除できる
    int On(const std::string& event, std::function<void()> callback);

    // 購読解除
    void Off(int id);

    // イベント発火（登録済みコールバックを即座に全て呼ぶ）
    void Emit(const std::string& event);

    // 全購読解除（シーン切り替え時に必ず呼ぶ）
    void Clear();

private:
    EventBus() = default;

    struct Subscription {
        int id;
        std::function<void()> callback;
        bool removed = false;
    };

    std::unordered_map<std::string, std::vector<Subscription>> subs_;
    int nextId_ = 0;
};

} // namespace engine
