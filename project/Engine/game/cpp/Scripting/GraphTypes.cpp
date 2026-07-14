#include "GraphTypes.h"
#include "JsonHelper.h"
#include "Logger.h"
using namespace engine::game;
using namespace engine;

float engine::game::AsFloat(const GraphValue& v)
{
    if (std::holds_alternative<float>(v)) { return std::get<float>(v); }
    if (std::holds_alternative<bool>(v))  { return std::get<bool>(v) ? 1.0f : 0.0f; }
    try { return std::stof(std::get<std::string>(v)); } catch (...) { return 0.0f; }
}

bool engine::game::AsBool(const GraphValue& v)
{
    if (std::holds_alternative<bool>(v))  { return std::get<bool>(v); }
    if (std::holds_alternative<float>(v)) { return std::get<float>(v) != 0.0f; }
    const std::string& s = std::get<std::string>(v);
    return !s.empty() && s != "false" && s != "0";
}

std::string engine::game::AsString(const GraphValue& v)
{
    if (std::holds_alternative<std::string>(v)) { return std::get<std::string>(v); }
    if (std::holds_alternative<bool>(v))         { return std::get<bool>(v) ? "true" : "false"; }
    return std::to_string(std::get<float>(v));
}

CompareOp engine::game::ParseCompareOp(const std::string& s)
{
    if (s == "==") { return CompareOp::Equal; }
    if (s == "!=") { return CompareOp::NotEqual; }
    if (s == "<")  { return CompareOp::Less; }
    if (s == "<=") { return CompareOp::LessEqual; }
    if (s == ">")  { return CompareOp::Greater; }
    if (s == ">=") { return CompareOp::GreaterEqual; }
    Logger::LogError("[Graph] unknown compare op '" + s + "', defaulting to '=='");
    return CompareOp::Equal;
}

bool engine::game::Compare(CompareOp op, const GraphValue& lhs, const GraphValue& rhs)
{
    // どちらかが文字列なら文字列として比較し、それ以外は数値として比較する
    if (std::holds_alternative<std::string>(lhs) || std::holds_alternative<std::string>(rhs)) {
        const std::string a = AsString(lhs);
        const std::string b = AsString(rhs);
        switch (op) {
        case CompareOp::Equal:        return a == b;
        case CompareOp::NotEqual:     return a != b;
        case CompareOp::Less:         return a <  b;
        case CompareOp::LessEqual:    return a <= b;
        case CompareOp::Greater:      return a >  b;
        case CompareOp::GreaterEqual: return a >= b;
        }
        return false;
    }

    const float a = AsFloat(lhs);
    const float b = AsFloat(rhs);
    switch (op) {
    case CompareOp::Equal:        return a == b;
    case CompareOp::NotEqual:     return a != b;
    case CompareOp::Less:         return a <  b;
    case CompareOp::LessEqual:    return a <= b;
    case CompareOp::Greater:      return a >  b;
    case CompareOp::GreaterEqual: return a >= b;
    }
    return false;
}

const GraphNode* GraphDesc::FindNode(const std::string& id) const
{
    auto it = nodes.find(id);
    return (it != nodes.end()) ? &it->second : nullptr;
}

namespace {
GraphValue ParamFromJson(const nlohmann::json& j)
{
    if (j.is_boolean()) { return GraphValue{ j.get<bool>() }; }
    if (j.is_number())  { return GraphValue{ j.get<float>() }; }
    return GraphValue{ j.get<std::string>() };
}

nlohmann::json ParamToJson(const GraphValue& v)
{
    if (std::holds_alternative<float>(v)) { return std::get<float>(v); }
    if (std::holds_alternative<bool>(v))  { return std::get<bool>(v); }
    return std::get<std::string>(v);
}
} // namespace

GraphDesc engine::game::GraphIO::Load(const std::string& path)
{
    GraphDesc graph;
    nlohmann::json j = JsonHelper::Load(path);
    if (j.empty()) {
        Logger::LogError("[Graph] failed to load or empty: " + path);
        return graph;
    }

    graph.startNodeId = j.value("start", "");

    if (j.contains("nodes") && j["nodes"].is_object()) {
        for (auto& [nodeId, nj] : j["nodes"].items()) {
            GraphNode node;
            node.id        = nodeId;
            node.type      = nj.value("type", "");
            node.next      = nj.value("next", "");
            node.nextTrue  = nj.value("nextTrue", "");
            node.nextFalse = nj.value("nextFalse", "");
            node.editorX   = nj.value("editorX", 0.0f);
            node.editorY   = nj.value("editorY", 0.0f);

            if (nj.contains("params") && nj["params"].is_object()) {
                for (auto& [key, pv] : nj["params"].items()) {
                    node.params[key] = ParamFromJson(pv);
                }
            }
            if (nj.contains("paramLinks") && nj["paramLinks"].is_object()) {
                for (auto& [key, src] : nj["paramLinks"].items()) {
                    node.paramLinks[key] = src.get<std::string>();
                }
            }
            graph.nodes[nodeId] = std::move(node);
        }
    }

    if (j.contains("comments") && j["comments"].is_object()) {
        for (auto& [commentId, cj] : j["comments"].items()) {
            GraphComment c;
            c.id     = commentId;
            c.text   = cj.value("text", "Comment");
            c.x      = cj.value("x", 0.0f);
            c.y      = cj.value("y", 0.0f);
            c.w      = cj.value("w", 220.0f);
            c.h      = cj.value("h", 120.0f);
            c.colorR = cj.value("colorR", 0.9f);
            c.colorG = cj.value("colorG", 0.85f);
            c.colorB = cj.value("colorB", 0.3f);
            graph.comments[commentId] = std::move(c);
        }
    }

    return graph;
}

void engine::game::GraphIO::Save(const std::string& path, const GraphDesc& graph)
{
    nlohmann::json j;
    j["start"] = graph.startNodeId;

    nlohmann::json nodesJson = nlohmann::json::object();
    for (const auto& [id, node] : graph.nodes) {
        nlohmann::json nj;
        nj["type"]      = node.type;
        nj["next"]      = node.next;
        nj["nextTrue"]  = node.nextTrue;
        nj["nextFalse"] = node.nextFalse;
        nj["editorX"]   = node.editorX;
        nj["editorY"]   = node.editorY;

        nlohmann::json paramsJson = nlohmann::json::object();
        for (const auto& [key, val] : node.params) {
            paramsJson[key] = ParamToJson(val);
        }
        nj["params"] = paramsJson;

        if (!node.paramLinks.empty()) {
            nlohmann::json linksJson = nlohmann::json::object();
            for (const auto& [key, src] : node.paramLinks) { linksJson[key] = src; }
            nj["paramLinks"] = linksJson;
        }

        nodesJson[id] = nj;
    }
    j["nodes"] = nodesJson;

    nlohmann::json commentsJson = nlohmann::json::object();
    for (const auto& [id, c] : graph.comments) {
        nlohmann::json cj;
        cj["text"]   = c.text;
        cj["x"]      = c.x;
        cj["y"]      = c.y;
        cj["w"]      = c.w;
        cj["h"]      = c.h;
        cj["colorR"] = c.colorR;
        cj["colorG"] = c.colorG;
        cj["colorB"] = c.colorB;
        commentsJson[id] = cj;
    }
    j["comments"] = commentsJson;

    JsonHelper::Save(path, j);
}
