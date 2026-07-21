/**
 * @file GpuProfiler.h
 * @brief GpuProfilerの描画資源とGPU処理の管理に関する公開型と操作インターフェースを定義するファイル
 */
#pragma once
#include <array>
#include <d3d12.h>
#include <wrl/client.h>
namespace engine {
class DirectXCommon;
}

namespace engine::graphics {

/**
 * @brief D3D12 タイムスタンプクエリを使い GPU パス別コストを計測するシングルトン
 *
 * 使い方（デバッグビルドのみ推奨）:
 *   Initialize(dxCommon)        ... シーン初期化時に1回
 *   Draw() 内:
 *     BeginScope(Shadow, cmd)   ... パス開始直前
 *     EndScope(Shadow, cmd)     ... パス終了直後
 *     Resolve(cmd)              ... Draw() 末尾（コマンドリストのフラッシュ前）
 *   PostDraw() 後（GPU 完了後）に ReadBack() を呼ぶと次フレームの DrawImGui() で表示
 */
/**
 * @brief GpuProfiler に関する型を提供する
 * @details GpuProfiler が扱うデータと操作の責務をまとめる
 */
class GpuProfiler {
public:
    enum Scope { Shadow = 0,
        SSAO,
        Main3D,
        Count };

    /**
     * @brief GetInstance の結果を取得する
     * @return 処理結果
     */
    static GpuProfiler* GetInstance();
    /**
     * @brief Initialize に対応する処理を開始する
     * @param dxCommon 処理に使用する値
     * @return なし
     */
    void Initialize(engine::DirectXCommon* dxCommon);
    /**
     * @brief Finalize に対応する終了処理を行う
     * @return なし
     */
    void Finalize();

    /**
     * @brief BeginScope に対応する処理を実行する
     * @param s 処理に使用する値
     * @param cmd 処理に使用する値
     * @return なし
     */
    void BeginScope(Scope s, ID3D12GraphicsCommandList* cmd);
    /**
     * @brief EndScope に対応する処理を実行する
     * @param s 処理に使用する値
     * @param cmd 処理に使用する値
     * @return なし
     */
    void EndScope(Scope s, ID3D12GraphicsCommandList* cmd);
    /**
     * @brief Resolve に対応する処理を実行する
     * @param cmd 処理に使用する値
     * @return なし
     */
    void Resolve(ID3D12GraphicsCommandList* cmd);
    /**
     * @brief ReadBack に対応する処理を実行する
     * @return なし
     */
    void ReadBack();
    /**
     * @brief DrawImGui に対応する内容を描画する
     * @return なし
     */
    void DrawImGui();

    float GetMs(Scope s) const { return results_[static_cast<int>(s)]; }

private:
    GpuProfiler() = default;
    static constexpr int kTimestampCount = static_cast<int>(Count) * 2;

    engine::DirectXCommon* dxCommon_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> queryHeap_;
    Microsoft::WRL::ComPtr<ID3D12Resource> readbackBuf_;
    uint64_t gpuFreq_ = 1;
    bool resolved_ = false; // Resolve() が少なくとも1回実行されたか
    std::array<float, Count> results_ = { };
};

} // namespace engine::graphics
