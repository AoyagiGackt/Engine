/**
 * @file GraphRuntime.h
 * @brief ノードグラフ1個ぶんの実行インスタンス（簡易ステートマシンVM）
 * @note 同じGraphDesc（ノード定義）を複数のGraphRuntimeで共有して動かせる
 * （定義は共有、変数と実行位置はインスタンスごと）
 * 実行ピンを次々辿り、Waitノードに当たったらそこで一旦Updateを抜けて
 * 次回以降のUpdate(dt)で残り時間を減らし、0になったら再開する
 */
#pragma once
#include "GraphTypes.h"
#include <map>
#include <memory>
#include <string>
namespace engine::game {

class GraphRuntime {
public:
    /** @brief startNodeIdから実行を開始する（即座に実行ピンを辿れるところまで進む） */
    void Start(const GraphDesc* graph);

    /** @brief 毎フレーム呼ぶWait中なら残り時間を減らし、0以下になったら実行を再開する */
    void Update(float dt);

    /** @brief 実行中（Wait中を含む、Haltしていない）かどうか */
    bool IsRunning() const { return graph_ != nullptr && !halted_; }

    // ノード実行ロジック（NodeRegistryの各Executorから使う）

    /** @brief 変数を設定する（無ければ新規作成） */
    void SetVariable(const std::string& name, GraphValue value) { variables_[name] = std::move(value); }

    /** @brief 変数を取得する未設定ならnullptr */
    const GraphValue* GetVariable(const std::string& name) const;

    /** @brief 現在保持している全変数（GraphEditorのライブ変数監視パネル用） */
    const std::map<std::string, GraphValue>& GetVariables() const { return variables_; }

    /** @brief 現在実行中（またはWaitで待機中）のノードID（GraphEditorの実行ハイライト用） */
    const std::string& GetCurrentNodeId() const { return currentNodeId_; }

    /**
     * @brief 実行チェーンの最深部（サブグラフ実行中ならその子孫）にある GraphRuntime を返す
     * @param outPath 返した GraphRuntime がサブグラフなら読み込み元パス、トップレベルなら空文字のまま
     * @note GraphEditorが今開いているグラフと実際に実行中のグラフが一致するかを
     * パスで突き合わせ、サブグラフを開いて見ている最中でもハイライトを追従させるために使う
     */
    const GraphRuntime& GetActiveLeaf(std::string& outPath) const
    {
        if (child_) {
            outPath = activeChildPath_;
            return child_->GetActiveLeaf(outPath);
        }
        return *this;
    }

    /**
     * @brief Waitを開始する（Waitノードから呼ぶ）
     * @param seconds 待つ秒数
     * @note 待機完了後に再開するノードIDは、Executorが返す outNextId から
     * RunUntilSuspendOrHalt() が自動的に受け取るここでは指定不要
     */
    void BeginWait(float seconds);

    /**
     * @brief サブグラフの実行を開始する（Subgraphノードから呼ぶ）
     * @return 開始できたら true読み込み失敗やネスト上限超過なら false
     * @note 子グラフがHaltするまで、親はWaitと同じ仕組みで待機する
     */
    bool BeginSubgraph(const std::string& path);

    /** @brief ノードの出力データピンに値を流す（データ配線の供給元になる） */
    void SetNodeOutput(const std::string& nodeId, GraphValue value) { nodeOutputs_[nodeId] = std::move(value); }

    /**
     * @brief パラメータの値を解決する
     * @note データ配線（paramLinks）があれば供給元ノードの出力値を優先し、
     * 供給元が未実行の場合とリンクが無い場合はリテラル値、それも無ければfallbackを返す
     */
    GraphValue ResolveParam(const GraphNode& node, const std::string& key, GraphValue fallback) const;

private:
    /** @brief currentNodeId_ から、SuspendかHaltになるまで実行ピンを辿り続ける */
    void RunUntilSuspendOrHalt();

    const GraphDesc* graph_ = nullptr;
    std::string currentNodeId_;
    std::map<std::string, GraphValue> variables_;
    std::map<std::string, GraphValue> nodeOutputs_; // ノードID → 最後に実行されたときの出力値

    bool halted_ = true;
    bool waiting_ = false;
    float waitTimer_ = 0.0f;
    std::string waitResumeNodeId_;

    // サブグラフ実行（Subgraphノード）
    static constexpr int kMaxSubgraphDepth = 8; // 自己参照などによる無限ネストの安全弁
    int depth_ = 0;
    std::unique_ptr<GraphDesc> childDesc_; // 子グラフ定義（child_ が参照し続けるため所有する）
    std::unique_ptr<GraphRuntime> child_;
    std::string activeChildPath_; // child_ をBeginSubgraph()したときの読み込み元パス（GetActiveLeaf用）
};

} // namespace engine::game
