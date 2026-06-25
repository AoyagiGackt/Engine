#pragma once
#include <functional>
#include <new>
#include <utility>

// 固定サイズのオブジェクトプール（ヘッダーオンリー）
// new/delete を避け、オブジェクトをスロットから使い回す
//
// 使い方:
//   ObjectPool<Bullet, 128> pool_;
//
//   // 生成（プール満杯なら nullptr）
//   auto* b = pool_.Spawn(pos, dir, speed);
//   if (b) { ... }
//
//   // 返却
//   pool_.Return(b);
//
//   // 全オブジェクトに処理
//   pool_.ForEach([](Bullet& b){ b.Update(dt); });
//
//   // 返却チェック付き ForEach（Bullet::IsDead() などで自動返却）
//   pool_.ForEachAutoReturn([](Bullet& b){ b.Update(dt); return b.IsDead(); });
template<typename T, int N = 64>
class ObjectPool {
public:
    ObjectPool()  = default;
    ~ObjectPool() { Clear(); }

    // コンストラクタ引数を転送してスポーン
    template<typename... Args>
    T* Spawn(Args&&... args) {
        for (int i = 0; i < N; ++i) {
            if (!active_[i]) {
                active_[i] = true;
                return new(storage_[i]) T(std::forward<Args>(args)...);
            }
        }
        return nullptr; // プール満杯
    }

    // オブジェクトを返却（デストラクタ呼び出し）
    void Return(T* p) {
        for (int i = 0; i < N; ++i) {
            if (active_[i] && ptr(i) == p) {
                p->~T();
                active_[i] = false;
                return;
            }
        }
    }

    // アクティブな全オブジェクトに fn を適用
    template<typename Fn>
    void ForEach(Fn fn) {
        for (int i = 0; i < N; ++i) {
            if (active_[i]) { fn(*ptr(i)); }
        }
    }

    // fn が true を返したオブジェクトを自動返却（Update と Return を同時にできる）
    template<typename Fn>
    void ForEachAutoReturn(Fn fn) {
        for (int i = 0; i < N; ++i) {
            if (!active_[i]) { continue; }
            if (fn(*ptr(i))) {
                ptr(i)->~T();
                active_[i] = false;
            }
        }
    }

    int  ActiveCount() const {
        int n = 0;
        for (int i = 0; i < N; ++i) { if (active_[i]) { ++n; } }
        return n;
    }
    bool IsFull()  const { return ActiveCount() == N; }
    bool IsEmpty() const { return ActiveCount() == 0; }

    // 全オブジェクトを返却
    void Clear() {
        for (int i = 0; i < N; ++i) {
            if (active_[i]) {
                ptr(i)->~T();
                active_[i] = false;
            }
        }
    }

private:
    T* ptr(int i) { return reinterpret_cast<T*>(storage_[i]); }

    alignas(T) char storage_[N][sizeof(T)];
    bool active_[N]{};
};
