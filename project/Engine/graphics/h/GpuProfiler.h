#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <array>

class DirectXCommon;

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
class GpuProfiler {
public:
    enum Scope { Shadow = 0, SSAO, Main3D, Count };

    static GpuProfiler* GetInstance();
    void Initialize(DirectXCommon* dxCommon);
    void Finalize();

    void BeginScope(Scope s, ID3D12GraphicsCommandList* cmd);
    void EndScope(Scope s, ID3D12GraphicsCommandList* cmd);
    void Resolve(ID3D12GraphicsCommandList* cmd);
    void ReadBack();
    void DrawImGui();

    float GetMs(Scope s) const { return results_[static_cast<int>(s)]; }

private:
    GpuProfiler() = default;
    static constexpr int kTimestampCount = static_cast<int>(Count) * 2;

    DirectXCommon* dxCommon_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> queryHeap_;
    Microsoft::WRL::ComPtr<ID3D12Resource>  readbackBuf_;
    uint64_t gpuFreq_   = 1;
    bool     resolved_  = false;  // Resolve() が少なくとも1回実行されたか
    std::array<float, Count> results_ = {};
};
