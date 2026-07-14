/**
 * @file GraphTypes.h
 * @brief ビジュアルスクリプティング（ノードグラフ）のデータモデル
 * @note V2: パラメータはリテラル値に加えて、他ノードの出力値をデータピンで接続して渡せる
 * （paramLinks参照）接続の型チェックは NodeRegistry のスペック情報を使ってエディタ側で行う
 */
#pragma once
#include <map>
#include <string>
#include <variant>
namespace engine::game {

/// @brief ノードのパラメータ／変数が取りうる値
using GraphValue = std::variant<float, bool, std::string>;

/// @brief データピンの型（エディタでのピン色分け・接続可否の判定に使う）
/// Any は何にでも接続できるワイルドカード（SetVariableのvalue等、実行時に型が決まるもの）
enum class GraphValueType { Float,
    Bool,
    String,
    Any };

float AsFloat(const GraphValue& v);
bool AsBool(const GraphValue& v);
std::string AsString(const GraphValue& v);

/// @brief Ifノードの比較演算子
enum class CompareOp { Equal,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual };
CompareOp ParseCompareOp(const std::string& s); // "==" "!=" "<" "<=" ">" ">="
bool Compare(CompareOp op, const GraphValue& lhs, const GraphValue& rhs);

/// @brief グラフ中の1ノード
struct GraphNode {
    std::string id;
    std::string type; // "SetVariable" | "If" | "Wait" | "EmitEvent"（NodeRegistryに登録された型名）
    std::map<std::string, GraphValue> params;

    // ---- データ配線（パラメータ名 → 値の供給元ノードID） ----
    // 接続されているパラメータは、リテラル値の代わりに供給元ノードの出力値を実行時に使う
    // （供給元が未実行で出力が無い場合はリテラル値にフォールバックする）
    std::map<std::string, std::string> paramLinks;

    // ---- 実行フロー ----
    std::string next; // 通常ノードの次（空文字列ならそこでHalt）
    std::string nextTrue; // Ifノード専用：条件が真のときの次
    std::string nextFalse; // Ifノード専用：条件が偽のときの次

    // ---- エディタ上のレイアウト（実行には無関係、GraphEditor用） ----
    float editorX = 0.0f;
    float editorY = 0.0f;
};

/// @brief グラフ上に置く注釈用の色付き矩形実行には一切関与しない、GraphEditor上でのドキュメント化専用
struct GraphComment {
    std::string id;
    std::string text = "Comment";
    float x = 0.0f, y = 0.0f;
    float w = 220.0f, h = 120.0f;
    float colorR = 0.9f, colorG = 0.85f, colorB = 0.3f;
};

/// @brief 1本のグラフ（コードを書かずに組んだロジック1個ぶん）
struct GraphDesc {
    std::string startNodeId;
    std::map<std::string, GraphNode> nodes;
    std::map<std::string, GraphComment> comments;

    const GraphNode* FindNode(const std::string& id) const;
};

namespace GraphIO {
    /// @brief JSONファイルからグラフを読み込む（読み込み失敗時は空のGraphDescを返す）
    GraphDesc Load(const std::string& path);

    /// @brief グラフをJSONファイルへ保存する（GraphEditorの保存機能から使う）
    void Save(const std::string& path, const GraphDesc& graph);
} // namespace GraphIO

} // namespace engine::game
