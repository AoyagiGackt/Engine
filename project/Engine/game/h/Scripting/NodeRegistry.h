/**
 * @file NodeRegistry.h
 * @brief ノード種別名（"If"等）と、その実行ロジックを結び付けるレジストリ
 * @note 新しいノード種別を増やしたいときは、ここに1つ登録すればグラフJSON側は
 * コードを書かずに（type名を書くだけで）使えるようになる
 */
#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "GraphTypes.h"
namespace engine::game {

class GraphRuntime;

/// @brief ノード実行の結果次に進めるか（Continue）、フレームをまたいで待つか（Suspend）
enum class NodeResult { Continue, Suspend };

/**
 * @brief 1ノード分の実行ロジック
 * @param rt        実行中のグラフインスタンス（変数の読み書き・Wait開始に使う）
 * @param node      実行するノード本体（params・next系フィールド）
 * @param outNextId [out] 次に進むノードID（空文字列ならそこでHalt）
 * @note Suspend を返す場合も outNextId は「待機完了後に進むノード」を必ず設定すること
 * （Waitノードの実装を参照）
 */
using NodeExecuteFn = std::function<NodeResult(GraphRuntime& rt, const GraphNode& node, std::string& outNextId)>;

/// @brief パラメータ1個ぶんのピン情報（データ配線の型チェック・エディタでのピン色分けに使う）
struct NodeParamSpec {
    std::string    key;
    GraphValueType type = GraphValueType::Any;
};

/// @brief ノード種別1個ぶんのピン情報
struct NodeTypeSpec {
    std::vector<NodeParamSpec> params;                       // 入力データピン（パラメータ）の並び
    bool           hasOutput  = false;                       // 出力データピンを持つか
    GraphValueType outputType = GraphValueType::Any;         // 出力データピンの型
};

class NodeRegistry {
public:
    static NodeRegistry* GetInstance();

    /// @brief 新しいノード種別を登録する（同名で上書きも可）
    void Register(const std::string& type, NodeExecuteFn fn);

    /// @brief ノード種別のピン情報を登録する（データ配線を使わないノードは省略可）
    void RegisterSpec(const std::string& type, NodeTypeSpec spec);

    /// @brief 型名から実行ロジックを引く登録が無ければnullptr
    const NodeExecuteFn* Find(const std::string& type) const;

    /// @brief 型名からピン情報を引く登録が無ければnullptr（エディタはAny扱いにする）
    const NodeTypeSpec* FindSpec(const std::string& type) const;

    /// @brief 登録済みの型名一覧を返す（GraphEditorの「ノード追加」メニュー用）
    std::vector<std::string> GetRegisteredTypes() const;

private:
    NodeRegistry(); // 組み込みノード（SetVariable/If/Wait/EmitEvent等）をここで登録する
    NodeRegistry(const NodeRegistry&) = delete;
    NodeRegistry& operator=(const NodeRegistry&) = delete;

    void RegisterBuiltins();

    std::map<std::string, NodeExecuteFn> executors_;
    std::map<std::string, NodeTypeSpec>  specs_;
};

} // namespace engine::game
