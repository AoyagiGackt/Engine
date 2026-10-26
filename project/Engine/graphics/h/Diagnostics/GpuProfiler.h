/**
 * @file GpuProfiler.h
 * @brief GPUの処理時間を区間ごとに測る処理（GpuProfiler）の宣言
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
 *     BeginScope(Scope::kShadow, cmd)   ... パス開始直前
 *     EndScope(Scope::kShadow, cmd)   ... パス終了直後
 *     Resolve(cmd)              ... Draw() 末尾（コマンドリストのフラッシュ前）
 *   PostDraw() 後（GPU 完了後）に ReadBack() を呼ぶと次フレームの DrawImGui() で表示
 */
/**
 * @brief タイムスタンプクエリヒープとリードバックバッファでGPUパス別コストを計測するシングルトンクラス
 */
class GpuProfiler {
public:
    enum class Scope { kShadow = 0,
        kSSAO,
        kMain3D,
        kCount };

    /**
     * @brief シングルトンインスタンスを取得する
     * @return GpuProfiler のインスタンスへのポインタ
     */
    static GpuProfiler* GetInstance();
    /**
     * @brief タイムスタンプクエリヒープとリードバックバッファを生成する
     * @param dxCommon DirectX共通基盤
     */
    void Initialize(engine::DirectXCommon* dxCommon);
    /**
     * @brief クエリヒープとリードバックバッファを解放する
     */
    void Finalize();

    /**
     * @brief 指定スコープの計測開始タイムスタンプを打つ
     * @param s 計測対象のパス（Shadow/SSAO/Main3D）
     * @param cmd タイムスタンプを積むコマンドリスト
     */
    void BeginScope(Scope s, ID3D12GraphicsCommandList* cmd);
    /**
     * @brief 指定スコープの計測終了タイムスタンプを打つ
     * @param s 計測対象のパス（Shadow/SSAO/Main3D）
     * @param cmd タイムスタンプを積むコマンドリスト
     */
    void EndScope(Scope s, ID3D12GraphicsCommandList* cmd);
    /**
     * @brief 積んだ全タイムスタンプクエリの結果をリードバックバッファへ解決する
     * @param cmd コマンドリスト（Draw() 末尾、フラッシュ前に呼ぶこと）
     */
    void Resolve(ID3D12GraphicsCommandList* cmd);
    /**
     * @brief リードバックバッファをCPU側にマップし、各パスの経過時間(ms)を results_ に反映する
     * @note Resolve() 済みのGPU完了後（PostDraw() 後）に呼ぶこと
     */
    void ReadBack();
    /**
     * @brief CPU/GPUのフレーム時間とパス別コストを表示するImGuiウィンドウを描画する
     */
    void DrawImGui();

    /** @brief 指定した区間のGPU処理時間（ミリ秒）を返す */
    float GetMs(Scope s) const {
        return results_[static_cast<int>(s)];
    }

private:
    /** @brief GPU計測資源が未確保の状態を作る */
    GpuProfiler() = default;
    static constexpr int kTimestampCount = static_cast<int>(Scope::kCount) * 2;

    engine::DirectXCommon* dxCommon_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> queryHeap_;
    Microsoft::WRL::ComPtr<ID3D12Resource> readbackBuf_;
    uint64_t gpuFreq_ = 1;
    bool resolved_ = false; // Resolve() が少なくとも1回実行されたか
    std::array<float, static_cast<size_t>(Scope::kCount)> results_ = { };
};

} // namespace engine::graphics
