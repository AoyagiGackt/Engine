/**
 * @file RenderPassGraph.h
 * @brief 名前付き描画パスを登録順に実行し、GPU診断境界を記録する軽量Render Graph
 */
#pragma once
#include "DirectXCommon.h"
#include <functional>
#include <string>
#include <vector>

namespace engine::graphics {

/**
 * @brief 1フレーム内の描画パス順序と診断名をまとめて管理する
 * @note リソース自動割り当ては行わず、既存描画コードを段階移行するための実行グラフとして使う
 */
class RenderPassGraph {
public:
    /** @brief グラフの実行に使うDirectX基盤を設定する */
    explicit RenderPassGraph(engine::DirectXCommon* dxCommon)
        : dxCommon_(dxCommon)
    {
    }

    /**
     * @brief 名前付き描画パスを末尾へ追加する
     * @param name GPU診断とDREDへ記録するパス名
     * @param execute 描画コマンドを記録する処理
     * @param enabled falseの場合はパスを登録するが実行しない
     */
    void AddPass(std::string name, std::function<void()> execute, bool enabled = true)
    {
        passes_.push_back({ std::move(name), std::move(execute), enabled });
    }

    /** @brief 有効な描画パスを登録順に実行する */
    void Execute()
    {
        for (const Pass& pass : passes_) {
            if (!pass.enabled) {
                continue;
            }
            // D3D12のBeginEventはPIX形式のメタデータが必要になる環境があるため使わない
            // パス名はDirectXCommonの診断コンテキストへ保存してエラーログへ付加する
            dxCommon_->SetDiagnosticContext(pass.name);
            pass.execute();
        }
    }

private:
    /**
     * @brief Pass に関する型を提供する
     * @details Pass が扱うデータと操作の責務をまとめる
     */
    struct Pass {
        std::string name;
        std::function<void()> execute;
        bool enabled = true;
    };

    engine::DirectXCommon* dxCommon_ = nullptr;
    std::vector<Pass> passes_;
};

} // namespace engine::graphics
