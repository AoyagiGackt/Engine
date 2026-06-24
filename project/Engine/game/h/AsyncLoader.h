#pragma once
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>

class AsyncLoader {
public:
    static AsyncLoader* GetInstance();

    void Submit(std::function<void()> task, std::function<void()> onComplete = nullptr);
    void Update();
    bool IsIdle() const;
    void WaitAll();

private:
    AsyncLoader();
    ~AsyncLoader();
    AsyncLoader(const AsyncLoader&) = delete;
    AsyncLoader& operator=(const AsyncLoader&) = delete;

    struct Job {
        std::function<void()> task;
        std::function<void()> onComplete;
    };

    void WorkerLoop();

    std::thread             worker_;
    std::mutex              queueMtx_;
    std::condition_variable cv_;
    std::queue<Job>         queue_;
    std::mutex              doneMtx_;
    std::queue<std::function<void()>> doneCallbacks_;
    bool                    stop_ = false;
    std::atomic<int>        activeCount_{ 0 };
};
