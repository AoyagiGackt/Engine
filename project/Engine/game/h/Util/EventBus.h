/**
 * @file EventBus.h
 * @brief 文字列イベント名によるPublish/Subscribeを提供するシングルトン
 * @note システム間を直接参照させずに疎結合で通知したい場合に使う
 * （ビジュアルスクリプティングのEmitEventノードの実体でもある）
 */
#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>
namespace engine::game {

/**
 * @brief EventBus に関する型を提供する
 * @details EventBus が扱うデータと操作の責務をまとめる
 */
class EventBus {
public:
    /**
     * @brief GetInstance の結果を取得する
     * @return 処理結果
     */
    static EventBus* GetInstance();

    using Callback = std::function<void()>;

    /**
     * @brief イベントを購読する
     * @return int Unsubscribe() で解除するためのハンドルID
     */
    int Subscribe(const std::string& eventName, Callback callback);

    /** @brief 購読を解除する */
    void Unsubscribe(const std::string& eventName, int handle);

    /** @brief イベントを発火し、購読者のコールバックを同期的に全て呼ぶ */
    void Emit(const std::string& eventName);

private:
    EventBus() = default;
    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;

    struct Entry {
        int handle;
        Callback callback;
    };

    std::map<std::string, std::vector<Entry>> subscribers_;
    int nextHandle_ = 0;
};

} // namespace engine::game
