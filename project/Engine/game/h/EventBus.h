#pragma once
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

// 軽量なイベントバス（シングルトン）
// オブジェクト同士を直接参照せずに通知し合う仕組み。
// Player が Emit → Scene が On で受け取る、という使い方が基本。
//
// 使い方:
//   auto* eb = EventBus::GetInstance();
//
//   // 購読（Initialize 時）
//   int id = eb->On("player_jumped", []{ /* パーティクル発生など */ });
//
//   // 発火（Player::Update 内）
//   eb->Emit("player_jumped");
//
//   // 個別解除 or シーン終了時に一括解除
//   eb->Off(id);
//   eb->Clear();   // Finalize 時に必ず呼ぶこと
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
