#pragma once
#include <functional>
#include <vector>
namespace engine {
// コルーチン風シーケンサー（シングルトン）
class Sequencer {
public:
    static Sequencer* GetInstance();

    // ---- ビルダー ----
    class Builder {
    public:
        // アクションを即時実行するステップ
        Builder& Do(std::function<void()> action);
        // 指定秒数だけ待つ（dt に従うのでヒットストップ中は停止する）
        Builder& WaitSeconds(float seconds);
        // 指定フレーム数だけ待つ（dt に依存しない）
        Builder& WaitFrames(int frames);
        // condition が true になるまで毎フレーム待つ
        Builder& WaitUntil(std::function<bool()> condition);
        // シーケンスを登録して開始返り値の ID で Cancel できる
        int Run();

    private:
        friend class Sequencer;
        explicit Builder(Sequencer* owner);

        struct Step {
            enum class Type { Action,
                WaitSeconds,
                WaitFrames,
                WaitUntil };
            Type type = Type::Action;
            std::function<void()> action;
            float seconds = 0.0f;
            int frames = 0;
            std::function<bool()> condition;
        };

        Sequencer* owner_;
        std::vector<Step> steps_;
    };

    Builder Begin();

    void Cancel(int id);
    void Clear();

    // Game::Update から毎フレーム呼ぶ
    // TimeManager::GetDeltaTime() を渡すとヒットストップ中に自動停止する
    void Update(float dt);

private:
    Sequencer() = default;

    struct Sequence {
        int id;
        std::vector<Builder::Step> steps;
        int stepIdx = 0;
        float waitTimer = 0.0f;
        int waitFrames = 0;
        bool done = false;
    };

    int nextId_ = 0;
    std::vector<Sequence> sequences_;
};

} // namespace engine
