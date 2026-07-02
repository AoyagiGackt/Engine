#pragma once
#include <functional>
#include <new>
#include <utility>
namespace engine {
// 固定サイズのオブジェクトプール（ヘッダーオンリー）
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

} // namespace engine
