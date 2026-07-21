/**
 * @file DelayTimer.h
 * @brief 実時間に基づく遅延コールバックを管理するファイル
 */
#pragma once
#include <functional>
#include <vector>
namespace engine {
/** @brief 遅延コールバックの登録、取消、実行時刻を一元管理する */
class DelayTimer {
public:
    /** @brief 共有タイマー管理を返す @return タイマー管理のポインタ */
    static DelayTimer* GetInstance();

    /** @brief 指定秒数後に処理を1回実行する @param delay 実行までの秒数 @param callback 実行する処理 */
    void After(float delay, std::function<void()> callback);

    /** @brief 取消可能な遅延処理を登録する @param delay 実行までの秒数 @param callback 実行する処理 @return 取消に使用する識別子 */
    int AfterCancellable(float delay, std::function<void()> callback);
    /** @brief 登録済み処理を取り消す @param id 登録時に返された識別子 */
    void Cancel(int id);

    /** @brief 経過時間を反映して期限に達した処理を実行する @param dt 実時間のフレーム間隔  単位は秒 */
    void Update(float dt);

    /** @brief 登録済みの全処理を破棄する */
    void Clear();

private:
    DelayTimer() = default;

    struct Entry {
        int id;
        float remaining;
        std::function<void()> callback;
        bool done;
    };

    std::vector<Entry> timers_;
    int nextId_ = 0;
};

} // namespace engine
