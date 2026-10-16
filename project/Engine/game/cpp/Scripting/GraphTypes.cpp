/**
 * @file GraphTypes.cpp
 * @brief GraphTypesのイベントグラフのデータ、編集、実行に関する具体的な処理を実装するファイル
 */
#include "GraphTypes.h"
#include "JsonHelper.h"
#include "Logger.h"
#include <array>
#include <functional>
using namespace engine::game;
using namespace engine;

float engine::game::AsFloat(const GraphValue& v)
{
    if (std::holds_alternative<float>(v)) {
        return std::get<float>(v);
    }
    if (std::holds_alternative<bool>(v)) {
        return std::get<bool>(v) ? 1.0f : 0.0f;
    }
    try {
        return std::stof(std::get<std::string>(v));
    } catch (...) {
        return 0.0f;
    }
}

bool engine::game::AsBool(const GraphValue& v)
{
    if (std::holds_alternative<bool>(v)) {
        return std::get<bool>(v);
    }
    if (std::holds_alternative<float>(v)) {
        return std::get<float>(v) != 0.0f;
    }
    const std::string& s = std::get<std::string>(v);
    return !s.empty() && s != "false" && s != "0";
}

std::string engine::game::AsString(const GraphValue& v)
{
    if (std::holds_alternative<std::string>(v)) {
        return std::get<std::string>(v);
    }
    if (std::holds_alternative<bool>(v)) {
        return std::get<bool>(v) ? "true" : "false";
    }
    return std::to_string(std::get<float>(v));
}

namespace {
/** @brief 比較演算子1つぶんの定義（記号と、数値・文字列それぞれでの比較） */
struct CompareOpDef {
    const char* symbol;
    bool (*onFloat)(float, float);
    bool (*onString)(const std::string&, const std::string&);
};

template <template <class> class Cmp>
constexpr CompareOpDef MakeCompareOp(const char* symbol)
{
    return {
        symbol,
        [](float a, float b) { return Cmp<float> {}(a, b); },
        [](const std::string& a, const std::string& b) { return Cmp<std::string> {}(a, b); },
    };
}

// CompareOp の並び順
constexpr std::array<CompareOpDef, 6> kCompareOps = {
    MakeCompareOp<std::equal_to>("=="),
    MakeCompareOp<std::not_equal_to>("!="),
    MakeCompareOp<std::less>("<"),
    MakeCompareOp<std::less_equal>("<="),
    MakeCompareOp<std::greater>(">"),
    MakeCompareOp<std::greater_equal>(">="),
};

/** @brief 数値演算子1つぶんの定義 */
struct MathOpDef {
    const char* symbol;
    float (*apply)(float, float);
};

// MathOp の並び順
constexpr std::array<MathOpDef, 4> kMathOps = { {
    { "+", [](float a, float b) { return a + b; } },
    { "-", [](float a, float b) { return a - b; } },
    { "*", [](float a, float b) { return a * b; } },
    { "/", [](float a, float b) { return (b != 0.0f) ? (a / b) : 0.0f; } },
} };

/** @brief 記号から表の添字を引く。見つからなければ -1 */
template <class Def, size_t N>
int FindOpIndex(const std::array<Def, N>& table, const std::string& symbol)
{
    for (size_t i = 0; i < N; ++i) {
        if (symbol == table[i].symbol) {
            return static_cast<int>(i);
        }
    }
    return -1;
}
} // namespace

CompareOp engine::game::ParseCompareOp(const std::string& s)
{
    const int index = FindOpIndex(kCompareOps, s);
    if (index < 0) {
        Logger::LogError("[Graph] unknown compare op '" + s + "', defaulting to '=='");
        return CompareOp::Equal;
    }
    return static_cast<CompareOp>(index);
}

bool engine::game::Compare(CompareOp op, const GraphValue& lhs, const GraphValue& rhs)
{
    const size_t index = static_cast<size_t>(op);
    if (index >= kCompareOps.size()) {
        return false;
    }
    const CompareOpDef& def = kCompareOps[index];
    // どちらかが文字列なら文字列として比較し、それ以外は数値として比較する
    if (std::holds_alternative<std::string>(lhs) || std::holds_alternative<std::string>(rhs)) {
        return def.onString(AsString(lhs), AsString(rhs));
    }
    return def.onFloat(AsFloat(lhs), AsFloat(rhs));
}

MathOp engine::game::ParseMathOp(const std::string& s)
{
    const int index = FindOpIndex(kMathOps, s);
    if (index < 0) {
        Logger::LogError("[Graph] unknown math op '" + s + "', defaulting to '+'");
        return MathOp::Add;
    }
    return static_cast<MathOp>(index);
}

float engine::game::ApplyMath(MathOp op, float a, float b)
{
    const size_t index = static_cast<size_t>(op);
    return index < kMathOps.size() ? kMathOps[index].apply(a, b) : 0.0f;
}

const GraphNode* GraphDesc::FindNode(const std::string& id) const
{
    auto it = nodes.find(id);
    return (it != nodes.end()) ? &it->second : nullptr;
}

namespace {
GraphValue ParamFromJson(const nlohmann::json& j)
{
    if (j.is_boolean()) {
        return GraphValue { j.get<bool>() };
    }
    if (j.is_number()) {
        return GraphValue { j.get<float>() };
    }
    if (j.is_string()) {
        return GraphValue { j.get<std::string>() };
    }
    // 想定外の型（null/配列/オブジェクト等、手編集や壊れたJSON由来）は空文字にフォールバックする
    return GraphValue { std::string() };
}

nlohmann::json ParamToJson(const GraphValue& v)
{
    if (std::holds_alternative<float>(v)) {
        return std::get<float>(v);
    }
    if (std::holds_alternative<bool>(v)) {
        return std::get<bool>(v);
    }
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
            node.id = nodeId;
            node.type = nj.value("type", "");
            node.next = nj.value("next", "");
            node.nextTrue = nj.value("nextTrue", "");
            node.nextFalse = nj.value("nextFalse", "");
            node.editorX = nj.value("editorX", 0.0f);
            node.editorY = nj.value("editorY", 0.0f);

            if (nj.contains("params") && nj["params"].is_object()) {
                for (auto& [key, pv] : nj["params"].items()) {
                    node.params[key] = ParamFromJson(pv);
                }
            }
            if (nj.contains("paramLinks") && nj["paramLinks"].is_object()) {
                for (auto& [key, src] : nj["paramLinks"].items()) {
                    if (src.is_string()) {
                        node.paramLinks[key] = src.get<std::string>();
                    }
                }
            }
            graph.nodes[nodeId] = std::move(node);
        }
    }

    if (j.contains("comments") && j["comments"].is_object()) {
        for (auto& [commentId, cj] : j["comments"].items()) {
            GraphComment c;
            c.id = commentId;
            c.text = cj.value("text", "Comment");
            c.x = cj.value("x", 0.0f);
            c.y = cj.value("y", 0.0f);
            c.w = cj.value("w", c.w); // 省略時は構造体の既定値のまま
            c.h = cj.value("h", c.h);
            c.colorR = cj.value("colorR", c.colorR);
            c.colorG = cj.value("colorG", c.colorG);
            c.colorB = cj.value("colorB", c.colorB);
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
        nj["type"] = node.type;
        nj["next"] = node.next;
        nj["nextTrue"] = node.nextTrue;
        nj["nextFalse"] = node.nextFalse;
        nj["editorX"] = node.editorX;
        nj["editorY"] = node.editorY;

        nlohmann::json paramsJson = nlohmann::json::object();
        for (const auto& [key, val] : node.params) {
            paramsJson[key] = ParamToJson(val);
        }
        nj["params"] = paramsJson;

        if (!node.paramLinks.empty()) {
            nlohmann::json linksJson = nlohmann::json::object();
            for (const auto& [key, src] : node.paramLinks) {
                linksJson[key] = src;
            }
            nj["paramLinks"] = linksJson;
        }

        nodesJson[id] = nj;
    }
    j["nodes"] = nodesJson;

    nlohmann::json commentsJson = nlohmann::json::object();
    for (const auto& [id, c] : graph.comments) {
        nlohmann::json cj;
        cj["text"] = c.text;
        cj["x"] = c.x;
        cj["y"] = c.y;
        cj["w"] = c.w;
        cj["h"] = c.h;
        cj["colorR"] = c.colorR;
        cj["colorG"] = c.colorG;
        cj["colorB"] = c.colorB;
        commentsJson[id] = cj;
    }
    j["comments"] = commentsJson;

    JsonHelper::Save(path, j);
}
