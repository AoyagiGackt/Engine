#include "NodeRegistry.h"
#include "EventBus.h"
#include "GameFlags.h"
#include "GraphRuntime.h"
#include "Logger.h"
using namespace engine::game;
using namespace engine;

namespace {

// パラメータ解決はGraphRuntime::ResolveParamに委譲する（データ配線があれば供給元ノードの出力を優先）

NodeResult ExecSetVariable(GraphRuntime& rt, const GraphNode& node, std::string& outNextId)
{
    std::string name = AsString(rt.ResolveParam(node, "name", std::string{}));
    if (name.empty()) {
        Logger::LogError("[Graph] SetVariable node '" + node.id + "' is missing 'name' param");
    } else {
        GraphValue value = rt.ResolveParam(node, "value", GraphValue{ 0.0f });
        rt.SetVariable(name, value);
        rt.SetNodeOutput(node.id, std::move(value)); // 設定した値をそのまま出力ピンにも流す
    }
    outNextId = node.next;
    return NodeResult::Continue;
}

NodeResult ExecIf(GraphRuntime& rt, const GraphNode& node, std::string& outNextId)
{
    std::string varName = AsString(rt.ResolveParam(node, "var", std::string{}));
    CompareOp   op       = ParseCompareOp(AsString(rt.ResolveParam(node, "op", std::string("=="))));
    GraphValue  rhs      = rt.ResolveParam(node, "value", GraphValue{ 0.0f });

    const GraphValue* lhs = rt.GetVariable(varName);
    bool result = lhs ? Compare(op, *lhs, rhs) : false;
    if (!lhs) {
        Logger::LogError("[Graph] If node '" + node.id + "' references undefined variable '" + varName + "'");
    }

    outNextId = result ? node.nextTrue : node.nextFalse;
    return NodeResult::Continue;
}

NodeResult ExecWait(GraphRuntime& rt, const GraphNode& node, std::string& outNextId)
{
    float seconds = AsFloat(rt.ResolveParam(node, "seconds", GraphValue{ 0.0f }));
    rt.BeginWait(seconds);
    outNextId = node.next; // 待機完了後にRunUntilSuspendOrHalt()側がこれをresume先として使う
    return NodeResult::Suspend;
}

NodeResult ExecEmitEvent(GraphRuntime& rt, const GraphNode& node, std::string& outNextId)
{
    std::string eventName = AsString(rt.ResolveParam(node, "event", std::string{}));
    if (eventName.empty()) {
        Logger::LogError("[Graph] EmitEvent node '" + node.id + "' is missing 'event' param");
    } else {
        EventBus::GetInstance()->Emit(eventName);
    }
    outNextId = node.next;
    return NodeResult::Continue;
}

// ステージエディタで置いたトリガー等が立てたフラグを、グラフ側のロジックで参照できるようにする
// GameFlags本体はグラフインスタンス間・ステージ間で共有のグローバルストア（GraphRuntimeのローカル変数とは別物）
NodeResult ExecSetFlag(GraphRuntime& rt, const GraphNode& node, std::string& outNextId)
{
    std::string flag = AsString(rt.ResolveParam(node, "flag", std::string{}));
    if (flag.empty()) {
        Logger::LogError("[Graph] SetFlag node '" + node.id + "' is missing 'flag' param");
    } else {
        bool value = AsBool(rt.ResolveParam(node, "value", GraphValue{ false }));
        GameFlags::GetInstance()->SetFlag(flag, value);
        rt.SetNodeOutput(node.id, GraphValue{ value });
    }
    outNextId = node.next;
    return NodeResult::Continue;
}

// フラグの値をグラフのローカル変数へコピーする（既存のIfノードで分岐に使うため）
// あわせて出力データピンにも流すので、他ノードのboolパラメータへ直接配線もできる
NodeResult ExecGetFlag(GraphRuntime& rt, const GraphNode& node, std::string& outNextId)
{
    std::string flag = AsString(rt.ResolveParam(node, "flag", std::string{}));
    std::string into = AsString(rt.ResolveParam(node, "into", std::string{}));
    if (flag.empty()) {
        Logger::LogError("[Graph] GetFlag node '" + node.id + "' is missing 'flag' param");
    } else {
        bool value = GameFlags::GetInstance()->GetFlag(flag);
        if (!into.empty()) { rt.SetVariable(into, GraphValue{ value }); }
        rt.SetNodeOutput(node.id, GraphValue{ value });
    }
    outNextId = node.next;
    return NodeResult::Continue;
}

// 別ファイルのグラフを呼び出して再利用する（カプセル化）子グラフがHaltするまで親は待機する
NodeResult ExecSubgraph(GraphRuntime& rt, const GraphNode& node, std::string& outNextId)
{
    std::string path = AsString(rt.ResolveParam(node, "path", std::string{}));
    outNextId = node.next;
    if (path.empty() || !rt.BeginSubgraph(path)) {
        Logger::LogError("[Graph] Subgraph node '" + node.id + "' failed to start: " + path);
        return NodeResult::Continue;
    }
    return NodeResult::Suspend;
}

} // namespace

NodeRegistry* NodeRegistry::GetInstance()
{
    static NodeRegistry instance;
    return &instance;
}

NodeRegistry::NodeRegistry()
{
    RegisterBuiltins();
}

void NodeRegistry::RegisterBuiltins()
{
    using VT = GraphValueType;

    Register("SetVariable", ExecSetVariable);
    RegisterSpec("SetVariable", { { { "name", VT::String }, { "value", VT::Any } }, true, VT::Any });

    Register("If", ExecIf);
    RegisterSpec("If", { { { "var", VT::String }, { "op", VT::String }, { "value", VT::Any } }, false, VT::Any });

    Register("Wait", ExecWait);
    RegisterSpec("Wait", { { { "seconds", VT::Float } }, false, VT::Any });

    Register("EmitEvent", ExecEmitEvent);
    RegisterSpec("EmitEvent", { { { "event", VT::String } }, false, VT::Any });

    Register("SetFlag", ExecSetFlag);
    RegisterSpec("SetFlag", { { { "flag", VT::String }, { "value", VT::Bool } }, true, VT::Bool });

    Register("GetFlag", ExecGetFlag);
    RegisterSpec("GetFlag", { { { "flag", VT::String }, { "into", VT::String } }, true, VT::Bool });

    Register("Subgraph", ExecSubgraph);
    RegisterSpec("Subgraph", { { { "path", VT::String } }, false, VT::Any });
}

void NodeRegistry::Register(const std::string& type, NodeExecuteFn fn)
{
    executors_[type] = std::move(fn);
}

void NodeRegistry::RegisterSpec(const std::string& type, NodeTypeSpec spec)
{
    specs_[type] = std::move(spec);
}

const NodeExecuteFn* NodeRegistry::Find(const std::string& type) const
{
    auto it = executors_.find(type);
    return (it != executors_.end()) ? &it->second : nullptr;
}

const NodeTypeSpec* NodeRegistry::FindSpec(const std::string& type) const
{
    auto it = specs_.find(type);
    return (it != specs_.end()) ? &it->second : nullptr;
}

std::vector<std::string> NodeRegistry::GetRegisteredTypes() const
{
    std::vector<std::string> types;
    types.reserve(executors_.size());
    for (const auto& [type, fn] : executors_) { types.push_back(type); }
    return types;
}
