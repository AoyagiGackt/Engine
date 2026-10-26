/**
 * @file LevelGraphRunner.cpp
 * @brief LevelGraphRunnerのレベル紐付きグラフの起動・更新を実装するファイル
 */
#include "LevelGraphRunner.h"
#include "GameFlags.h"
#include "Logger.h"
#include <algorithm>

namespace engine::game {
/** @brief レベルデータのグラフ設定を読み込んで実行を開始する */
void LevelGraphRunner::Start(const LevelData& data) {
    Stop();
    mainPath_ = data.graphPath;
    bindings_ = data.flagGraphs;
    for (const FlagGraphBinding& binding : bindings_) {
        lastFlagState_[binding.flag] = GameFlags::GetInstance()->GetFlag(binding.flag);
    }
    if (!mainPath_.empty()) {
        Launch(mainPath_);
    }
}

/** @brief 全グラフを停止して破棄する */
void LevelGraphRunner::Stop() {
    running_.clear();
    bindings_.clear();
    lastFlagState_.clear();
    mainPath_.clear();
}

/** @brief パスのグラフを読み込んで起動する。読み込み失敗や開始ノード未設定なら何もしない */
void LevelGraphRunner::Launch(const std::string& path) {
    RunningGraph graph;
    graph.path = path;
    graph.desc = std::make_unique<GraphDesc>(graphIO::Load(path));
    if (graph.desc->startNodeId.empty()) {
        Logger::LogWarning("[LevelGraph] 開始ノードが無いか読み込めません: " + path);
        return;
    }
    graph.runtime = std::make_unique<GraphRuntime>();
    graph.runtime->Start(graph.desc.get());
    running_.push_back(std::move(graph));
}

/** @brief 毎フレーム呼ぶ。常駐グラフを進め、フラグの立ち上がりで起動グラフを開始する */
void LevelGraphRunner::Update(float dt) {
    for (auto& graph : running_) {
        graph.runtime->Update(dt);
    }
    running_.erase(std::remove_if(running_.begin(), running_.end(),
                       [](const RunningGraph& graph) {
                           return !graph.runtime->IsRunning();
                       }),
        running_.end());

    // フラグの立ち上がり（false→true）を検出して起動グラフを開始する
    for (const FlagGraphBinding& binding : bindings_) {
        if (binding.flag.empty() || binding.graphPath.empty()) {
            continue;
        }
        const bool now = GameFlags::GetInstance()->GetFlag(binding.flag);
        bool& last = lastFlagState_[binding.flag];
        if (now && !last) {
            Launch(binding.graphPath);
        }
        last = now;
    }
}

/** @brief 実行中のグラフ本数（エディタの状態表示用） */
int LevelGraphRunner::GetRunningCount() const {
    return static_cast<int>(running_.size());
}

} // namespace engine::game
