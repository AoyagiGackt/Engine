/**
 * @file TAAEffect.h
 * @brief Temporal Anti-Aliasing（時間的アンチエイリアシング）エフェクトを管理するクラス
 */
#pragma once
#include "DirectXCommon.h"
#include "MakeAffine.h"
#include "SrvManager.h"
#include <wrl/client.h>
namespace engine::graphics {

/**
 * @brief TAA（時間的AA）を実装するシングルトンクラス
 * @note 前フレームの描画結果（ヒストリ）と現フレームをブレンドすることでジャギーを低減する。
 *       毎フレーム BeginFrame() でジッター量を取得し、描画後に Apply() を呼ぶことで効果が適用される。
 */
class TAAEffect {
public:
    static TAAEffect* GetInstance();

    /**
     * @brief 初期化。レンダーターゲットと PSO を作成する
     * @param dxCommon    DirectX共通基盤
     * @param srvManager  SRVディスクリプタヒープ管理
     */
    void Initialize(engine::DirectXCommon* dxCommon, SrvManager* srvManager);
    void Finalize();

    /**
     * @brief フレーム開始時に呼ぶ。投影行列に加算するジッターオフセットを返す
     * @return Vector2 今フレームのサブピクセルジッター量（Halton 数列ベース）
     */
    Vector2 BeginFrame();

    /**
     * @brief TAA ブレンドを適用する。メイン描画完了後に呼ぶ
     * @param dxCommon        DirectX共通基盤
     * @param currentSrvIndex 現フレームのシーン SRV インデックス
     */
    void Apply(engine::DirectXCommon* dxCommon, uint32_t currentSrvIndex);

    /** @brief ヒストリバッファの SRV インデックスを返す（影等のサンプリングに使用） */
    uint32_t GetHistorySrvIndex() const { return historySrvIndex_; }
    bool IsEnabled()  const { return enabled_; }
    void SetEnabled(bool e) { enabled_ = e; }
    void Reset()            { frameIdx_ = 0; }

private:
    TAAEffect() = default;
    ~TAAEffect() = default;
    TAAEffect(const TAAEffect&) = delete;
    TAAEffect& operator=(const TAAEffect&) = delete;

    void Barrier(ID3D12GraphicsCommandList* cmd, ID3D12Resource* res,
                 D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) const;

    // history render target (R16G16B16A16_FLOAT)
    Microsoft::WRL::ComPtr<ID3D12Resource>       historyRT_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> historyRtvHeap_;
    D3D12_CPU_DESCRIPTOR_HANDLE                  historyRtvHandle_ = {};
    uint32_t                                     historySrvIndex_  = UINT32_MAX;

    // accumulation RT — blended output before copying to history and backbuffer
    Microsoft::WRL::ComPtr<ID3D12Resource>       accumRT_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> accumRtvHeap_;
    D3D12_CPU_DESCRIPTOR_HANDLE                  accumRtvHandle_   = {};
    uint32_t                                     accumSrvIndex_    = UINT32_MAX;

    struct CBLayout {
        float jitterX, jitterY;
        float blendAlpha;
        float _pad;
    };
    Microsoft::WRL::ComPtr<ID3D12Resource> cb_;
    CBLayout*                              cbData_ = nullptr;

    // Pass 2 用パススルー CB（blendAlpha=1, jitter=0 固定。毎フレーム save/restore 不要）
    Microsoft::WRL::ComPtr<ID3D12Resource> passthroughCb_;
    CBLayout*                              passthroughCbData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rs_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pso_;       // SRGB8 output (→ backbuffer)
    Microsoft::WRL::ComPtr<ID3D12PipelineState> psoFloat_;  // FLOAT16 output (→ accumRT_)

    SrvManager*    srvManager_ = nullptr;
    engine::DirectXCommon* dxCommon_   = nullptr;

    uint32_t frameIdx_ = 0;
    bool     enabled_  = true;
};

} // namespace engine::graphics
