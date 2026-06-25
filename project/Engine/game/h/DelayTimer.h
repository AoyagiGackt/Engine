#pragma once
#include <functional>
#include <vector>

// N 秒後にコールバックを実行するタイマー管理クラス
// Game::Update() から Update(dt) を毎フレーム呼ぶこと
//
// 使い方:
//   DelayTimer::GetInstance()->After(2.0f, []{ SceneManager::... });
//   int id = DelayTimer::GetInstance()->AfterCancellable(0.3f, []{ ... });
//   DelayTimer::GetInstance()->Cancel(id); // 途中でキャンセル
//   DelayTimer::GetInstance()->Clear();    // シーン切り替え前に全削除
class DelayTimer {
public:
    static DelayTimer* GetInstance();

    // delay 秒後に callback を1回実行する
    void After(float delay, std::function<void()> callback);

    // キャンセル可能な版。返り値の ID を Cancel() に渡す
    int AfterCancellable(float delay, std::function<void()> callback);
    void Cancel(int id);

    // 毎フレーム更新（実時間 dt = GameConstants::kFrameDeltaTime 推奨）
    void Update(float dt);

    // 全タイマー削除
    void Clear();

private:
    DelayTimer() = default;

    struct Entry {
        int   id;
        float remaining;
        std::function<void()> callback;
        bool  done;
    };

    std::vector<Entry> timers_;
    int nextId_ = 0;
};
