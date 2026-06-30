#include "AsyncLoader.h"
using namespace engine;

AsyncLoader* AsyncLoader::GetInstance()
{
    static AsyncLoader inst;
    return &inst;
}

AsyncLoader::AsyncLoader()
{
    worker_ = std::thread(&AsyncLoader::WorkerLoop, this);
}

AsyncLoader::~AsyncLoader()
{
    {
        std::unique_lock<std::mutex> lk(queueMtx_);
        stop_ = true;
    }
    cv_.notify_one();
    if (worker_.joinable()) {
        worker_.join();
    }
}

void AsyncLoader::Submit(std::function<void()> task, std::function<void()> onComplete)
{
    {
        std::unique_lock<std::mutex> lk(queueMtx_);
        queue_.push({ std::move(task), std::move(onComplete) });
        activeCount_++;
    }
    cv_.notify_one();
}

void AsyncLoader::Update()
{
    std::queue<std::function<void()>> local;
    {
        std::unique_lock<std::mutex> lk(doneMtx_);
        std::swap(local, doneCallbacks_);
    }
    while (!local.empty()) {
        auto& cb = local.front();
        if (cb) { cb(); }
        local.pop();
    }
}

bool AsyncLoader::IsIdle() const
{
    return activeCount_.load() == 0;
}

void AsyncLoader::WaitAll()
{
    while (!IsIdle()) {
        std::this_thread::yield();
    }
}

void AsyncLoader::WorkerLoop()
{
    while (true) {
        Job job;
        {
            std::unique_lock<std::mutex> lk(queueMtx_);
            cv_.wait(lk, [this] { return stop_ || !queue_.empty(); });
            if (stop_ && queue_.empty()) { return; }
            job = std::move(queue_.front());
            queue_.pop();
        }

        if (job.task) { job.task(); }

        if (job.onComplete) {
            std::unique_lock<std::mutex> lk(doneMtx_);
            doneCallbacks_.push(std::move(job.onComplete));
        }

        activeCount_--;
    }
}
