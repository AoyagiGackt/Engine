/**
 * @file GraphRuntime.cpp
 * @brief ノードグラフを実行する仮想マシン（GraphRuntime）の実装
 */
#include "GraphRuntime.h"
#include "Logger.h"
#include "NodeRegistry.h"

namespace engine::game {
/** @brief startNodeIdから実行を開始する（即座に実行ピンを辿れるところまで進む） */
void GraphRuntime::Start(const GraphDesc* graph)
{
    graph_ = graph;
    variables_.clear();
    nodeOutputs_.clear();
    waiting_ = false;
    waitTimer_ = 0.0f;
    waitResumeNodeId_.clear();
    child_.reset();
    childDesc_.reset();
    activeChildPath_.clear();

    halted_ = (graph_ == nullptr || graph_->startNodeId.empty());
    if (!halted_)
    {
        currentNodeId_ = graph_->startNodeId;
        RunUntilSuspendOrHalt();
    }
}

/** @brief 毎フレーム呼ぶWait中なら残り時間を減らし、0以下になったら実行を再開する */
void GraphRuntime::Update(float dt)
{
    // サブグラフ実行中は子を進め、子がHaltしたら親を再開する（Waitと同じresume機構を使う）
    if (child_)
    {
        child_->Update(dt);
        if (child_->IsRunning())
        {
            return;
        }
        child_.reset();
        childDesc_.reset();
        activeChildPath_.clear();
        ResumeFromWait();
        return;
    }

    if (!waiting_)
    {
        return;
    }

    waitTimer_ -= dt;
    if (waitTimer_ > 0.0f)
    {
        return;
    }

    waiting_ = false;
    ResumeFromWait();
}

/** @brief 子グラフとWaitの完了後に、共通の再開先へ処理を戻す */
void GraphRuntime::ResumeFromWait()
{
    if (waitResumeNodeId_.empty())
    {
        halted_ = true;
        return;
    }
    currentNodeId_ = waitResumeNodeId_;
    RunUntilSuspendOrHalt();
}

/** @brief 変数を取得する未設定ならnullptr */
const GraphValue* GraphRuntime::GetVariable(const std::string& name) const
{
    auto it = variables_.find(name);
    return (it != variables_.end()) ? &it->second : nullptr;
}

/** @brief Waitを開始する（Waitノードから呼ぶ） */
void GraphRuntime::BeginWait(float seconds)
{
    waiting_ = true;
    waitTimer_ = seconds;
}

/** @brief サブグラフの実行を開始する（Subgraphノードから呼ぶ） */
bool GraphRuntime::BeginSubgraph(const std::string& path)
{
    if (depth_ >= kMaxSubgraphDepth)
    {
        Logger::LogError("[Graph] subgraph nest depth exceeded " + std::to_string(kMaxSubgraphDepth)
            + " (possible self-reference): " + path);
        return false;
    }

    auto desc = std::make_unique<GraphDesc>(GraphIO::Load(path));
    if (desc->startNodeId.empty())
    {
        return false; // 読み込み失敗 or 開始ノード未設定
    }

    childDesc_ = std::move(desc);
    child_ = std::make_unique<GraphRuntime>();
    child_->depth_ = depth_ + 1;
    child_->Start(childDesc_.get());
    activeChildPath_ = path;
    return true;
}

/** @brief パラメータの値を解決する */
GraphValue GraphRuntime::ResolveParam(const GraphNode& node, const std::string& key, GraphValue fallback) const
{
    auto linkIt = node.paramLinks.find(key);
    if (linkIt != node.paramLinks.end())
    {
        auto outIt = nodeOutputs_.find(linkIt->second);
        if (outIt != nodeOutputs_.end())
        {
            return outIt->second;
        }
        // 供給元ノードが未実行で出力が無い場合はリテラル値へフォールバックする
    }
    auto it = node.params.find(key);
    return (it != node.params.end()) ? it->second : std::move(fallback);
}

/** @brief currentNodeId_ から、SuspendかHaltになるまで実行ピンを辿り続ける */
void GraphRuntime::RunUntilSuspendOrHalt()
{
    // Wait を挟まないノードだけで環状につながっていると無限ループでフレームが固まるため、
    // 安全弁として1回のUpdate内で辿れるノード数に上限を設ける
    constexpr int kMaxStepsPerRun = 10000;
    int steps = 0;

    while (!halted_ && !waiting_)
    {
        if (++steps > kMaxStepsPerRun)
        {
            Logger::LogError("[Graph] exceeded " + std::to_string(kMaxStepsPerRun)
                + " steps without Wait/Halt — possible cycle, aborting at node: " + currentNodeId_);
            halted_ = true;
            break;
        }

        // 今のノードを探し、型に対応する実行関数で1つ進める（見つからなければ止める）
        const GraphNode* node = graph_->FindNode(currentNodeId_);
        if (!node)
        {
            Logger::LogError("[Graph] node not found: " + currentNodeId_);
            halted_ = true;
            break;
        }

        const NodeExecuteFn* fn = NodeRegistry::GetInstance()->Find(node->type);
        if (!fn)
        {
            Logger::LogError("[Graph] unknown node type: " + node->type);
            halted_ = true;
            break;
        }

        std::string nextId;
        NodeResult result = (*fn)(*this, *node, nextId);
        if (result == NodeResult::Suspend)
        {
            // BeginWait() で waiting_ はセット済み。再開先は outNextId をそのまま使う
            waitResumeNodeId_ = nextId;
            break;
        }

        // 行き先が無ければグラフの終わり
        if (nextId.empty())
        {
            halted_ = true;
            break;
        }
        currentNodeId_ = nextId;
    }
}

} // namespace engine::game
