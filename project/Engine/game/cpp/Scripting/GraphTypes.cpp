/**
 * @file GraphTypes.cpp
 * @brief グラフの値の変換・比較・演算と、グラフファイルの読み書きの実装
 */
#include "GraphTypes.h"
#include "JsonFieldTable.h"
#include "JsonHelper.h"
#include "Logger.h"
#include <array>
#include <functional>

namespace engine::game {
/** @brief グラフの値を数値へ変換する */
float engine::game::AsFloat(const GraphValue& v)
{
    if (std::holds_alternative<float>(v))
    {
        return std::get<float>(v);
    }
    if (std::holds_alternative<bool>(v))
    {
        return std::get<bool>(v) ? 1.0f : 0.0f;
    }
    try
    {
        return std::stof(std::get<std::string>(v));
    }
    catch (...)
    {
        return 0.0f;
    }
}

/** @brief グラフの値を真偽値へ変換する */
bool engine::game::AsBool(const GraphValue& v)
{
    if (std::holds_alternative<bool>(v))
    {
        return std::get<bool>(v);
    }
    if (std::holds_alternative<float>(v))
    {
        return std::get<float>(v) != 0.0f;
    }
    const std::string& s = std::get<std::string>(v);
    return !s.empty() && s != "false" && s != "0";
}

/** @brief グラフの値を文字列へ変換する */
std::string engine::game::AsString(const GraphValue& v)
{
    if (std::holds_alternative<std::string>(v))
    {
        return std::get<std::string>(v);
    }
    if (std::holds_alternative<bool>(v))
    {
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

/** @brief 記号から比較演算の定義を作る */
template <template <class> class Cmp>
constexpr CompareOpDef MakeCompareOp(const char* symbol)
{
    return {
        symbol,
        [](float a, float b) {
            return Cmp<float> {}(a, b);
        },
        [](const std::string& a, const std::string& b) {
            return Cmp<std::string> {}(a, b);
        },
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
    {
        "+", [](float a, float b) {
            return a + b;
        }
    },
    {
        "-", [](float a, float b) {
            return a - b;
        }
    },
    {
        "*", [](float a, float b) {
            return a * b;
        }
    },
    {
        "/", [](float a, float b) {
            return (b != 0.0f) ? (a / b) : 0.0f;
        }
    },
} };

/** @brief 記号から表の添字を引く。見つからなければ -1 */
/** @brief 演算の表から記号に一致する番号を探す（無ければ -1） */
template <class Def, size_t N>
int FindOpIndex(const std::array<Def, N>& table, const std::string& symbol)
{
    for (size_t i = 0; i < N; ++i)
    {
        if (symbol == table[i].symbol)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}
} // namespace

/** @brief 記号を比較演算の種類へ変換する */
CompareOp engine::game::ParseCompareOp(const std::string& s)
{
    const int index = FindOpIndex(kCompareOps, s);
    if (index < 0)
    {
        Logger::LogError("[Graph] unknown compare op '" + s + "', defaulting to '=='");
        return CompareOp::Equal;
    }
    return static_cast<CompareOp>(index);
}

/** @brief 2つのグラフの値を指定の比較演算で比べる */
bool engine::game::Compare(CompareOp op, const GraphValue& lhs, const GraphValue& rhs)
{
    const size_t index = static_cast<size_t>(op);
    if (index >= kCompareOps.size())
    {
        return false;
    }
    const CompareOpDef& def = kCompareOps[index];
    // どちらかが文字列なら文字列として比較し、それ以外は数値として比較する
    if (std::holds_alternative<std::string>(lhs) || std::holds_alternative<std::string>(rhs))
    {
        return def.onString(AsString(lhs), AsString(rhs));
    }
    return def.onFloat(AsFloat(lhs), AsFloat(rhs));
}

/** @brief 記号を四則演算などの種類へ変換する */
MathOp engine::game::ParseMathOp(const std::string& s)
{
    const int index = FindOpIndex(kMathOps, s);
    if (index < 0)
    {
        Logger::LogError("[Graph] unknown math op '" + s + "', defaulting to '+'");
        return MathOp::Add;
    }
    return static_cast<MathOp>(index);
}

/** @brief 2つの数値に指定の演算を行う */
float engine::game::ApplyMath(MathOp op, float a, float b)
{
    const size_t index = static_cast<size_t>(op);
    return index < kMathOps.size() ? kMathOps[index].apply(a, b) : 0.0f;
}

/** @brief ID からノードを探す（無ければ nullptr） */
const GraphNode* GraphDesc::FindNode(const std::string& id) const
{
    auto it = nodes.find(id);
    return (it != nodes.end()) ? &it->second : nullptr;
}

namespace {
/** @brief JSONのノードのパラメーターをグラフの値へ変換する */
GraphValue ParamFromJson(const nlohmann::json& j)
{
    if (j.is_boolean())
    {
        return GraphValue { j.get<bool>() };
    }
    if (j.is_number())
    {
        return GraphValue { j.get<float>() };
    }
    if (j.is_string())
    {
        return GraphValue { j.get<std::string>() };
    }
    // 想定外の型（null/配列/オブジェクト等、手編集や壊れたJSON由来）は空文字にフォールバックする
    return GraphValue { std::string() };
}

/** @brief グラフの値をJSONのノードのパラメーターへ変換する */
nlohmann::json ParamToJson(const GraphValue& v)
{
    if (std::holds_alternative<float>(v))
    {
        return std::get<float>(v);
    }
    if (std::holds_alternative<bool>(v))
    {
        return std::get<bool>(v);
    }
    return std::get<std::string>(v);
}

/** @brief ノードのうち値1つで済む項目と JSON のキーの対応表（パラメータとデータ配線は別に読み書きする） */
constexpr JsonField<GraphNode> kNodeFields[] = {
    { nullptr, "type", &GraphNode::type },
    { nullptr, "next", &GraphNode::next },
    { nullptr, "nextTrue", &GraphNode::nextTrue },
    { nullptr, "nextFalse", &GraphNode::nextFalse },
    { nullptr, "editorX", &GraphNode::editorX },
    { nullptr, "editorY", &GraphNode::editorY },
};

/** @brief コメント枠の項目と JSON のキーの対応表 */
constexpr JsonField<GraphComment> kCommentFields[] = {
    { nullptr, "text", &GraphComment::text },
    { nullptr, "x", &GraphComment::x },
    { nullptr, "y", &GraphComment::y },
    { nullptr, "w", &GraphComment::w },
    { nullptr, "h", &GraphComment::h },
    { nullptr, "colorR", &GraphComment::colorR },
    { nullptr, "colorG", &GraphComment::colorG },
    { nullptr, "colorB", &GraphComment::colorB },
};
} // namespace

/** @brief グラフのファイルを読み込む */
GraphDesc engine::game::GraphIO::Load(const std::string& path)
{
    // 読めなかった時は空のグラフを返し、エラーを記録する
    GraphDesc graph;
    nlohmann::json j = JsonHelper::Load(path);
    if (j.empty())
    {
        Logger::LogError("[Graph] failed to load or empty: " + path);
        return graph;
    }

    // 実行を始めるノード
    graph.startNodeId = j.value("start", "");

    // ノード: 型・行き先・エディタ上の位置は対応表で読み、パラメータとデータ配線は名前ごとに読む
    if (j.contains("nodes") && j["nodes"].is_object())
    {
        for (auto& [nodeId, nj] : j["nodes"].items())
        {
            GraphNode node;
            node.id = nodeId;
            ReadJsonFields(nj, kNodeFields, node);

            if (nj.contains("params") && nj["params"].is_object())
            {
                for (auto& [key, pv] : nj["params"].items())
                {
                    node.params[key] = ParamFromJson(pv);
                }
            }
            if (nj.contains("paramLinks") && nj["paramLinks"].is_object())
            {
                for (auto& [key, src] : nj["paramLinks"].items())
                {
                    if (src.is_string())
                    {
                        node.paramLinks[key] = src.get<std::string>();
                    }
                }
            }
            graph.nodes[nodeId] = std::move(node);
        }
    }

    // コメント枠: 書かれていない項目は構造体の既定値のまま
    if (j.contains("comments") && j["comments"].is_object())
    {
        for (auto& [commentId, cj] : j["comments"].items())
        {
            GraphComment c;
            c.id = commentId;
            ReadJsonFields(cj, kCommentFields, c);
            graph.comments[commentId] = std::move(c);
        }
    }

    return graph;
}

/** @brief グラフをファイルへ保存する */
void engine::game::GraphIO::Save(const std::string& path, const GraphDesc& graph)
{
    nlohmann::json j;
    j["start"] = graph.startNodeId;

    // ノード: 対応表の項目に、パラメータと（あれば）データ配線を足して書く
    nlohmann::json nodesJson = nlohmann::json::object();
    for (const auto& [id, node] : graph.nodes)
    {
        nlohmann::json nj = nlohmann::json::object();
        WriteJsonFields(nj, kNodeFields, node);

        nlohmann::json paramsJson = nlohmann::json::object();
        for (const auto& [key, val] : node.params)
        {
            paramsJson[key] = ParamToJson(val);
        }
        nj["params"] = paramsJson;

        if (!node.paramLinks.empty())
        {
            nlohmann::json linksJson = nlohmann::json::object();
            for (const auto& [key, src] : node.paramLinks)
            {
                linksJson[key] = src;
            }
            nj["paramLinks"] = linksJson;
        }

        nodesJson[id] = nj;
    }
    j["nodes"] = nodesJson;

    // コメント枠
    nlohmann::json commentsJson = nlohmann::json::object();
    for (const auto& [id, c] : graph.comments)
    {
        nlohmann::json cj = nlohmann::json::object();
        WriteJsonFields(cj, kCommentFields, c);
        commentsJson[id] = cj;
    }
    j["comments"] = commentsJson;

    JsonHelper::Save(path, j);
}

} // namespace engine::game
