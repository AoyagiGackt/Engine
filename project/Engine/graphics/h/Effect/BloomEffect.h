#pragma once
#include "DirectXCommon.h"
#include "SrvManager.h"
#include "WinApp.h"
#include <wrl/client.h>
namespace engine::graphics {

/**
 * @brief Bloom（発光）ポストプロセスエフェクト
 *
 * ■ 処理の流れ（4パス）
 *   1. BrightPass  : シーン → bloomTex_[0]（輝度が threshold を超えたピクセル抽出）
 *   2. BlurH       : bloomTex_[0] → bloomTex_[1]（水平ガウシアンブラー）
 *   3. BlurV       : bloomTex_[1] → bloomTex_[0]（垂直ガウシアンブラー）
 *   4. Combine     : シーン + bloomTex_[0] → バックバッファ（加算合成）
 *
 * ■ 使い方
 *   // 初期化（フレームワーク起動時）
 *   BloomEffect::GetInstance()->Initialize(dxCommon, srvManager);
 *
 *   // 毎フレーム（Bloom 有効時）
 *   BloomEffect::GetInstance()->BeginScene();    // シーン描画開始前
 *   // ... 3D/Sprite 描画 ...
 *   BloomEffect::GetInstance()->EndScene();      // シーン描画終了後
 *   BloomEffect::GetInstance()->Apply(srvManager); // バックバッファへ合成
 *
 *   // 無効時はバックバッファへ直接描画するため BeginScene/EndScene/Apply は呼ばない
 *   // （GrayscaleEffect と同じパターン）
 */
/**
 * @brief シーンを輝度抽出→水平/垂直ブラー→加算合成の4パスで処理するブルーム効果のシングルトンクラス
 */
class BloomEffect {
public:
    static BloomEffect* GetInstance()
    {
        static BloomEffect instance;
        return &instance;
    }

    /**
     * @brief シーンキャプチャ用RT・ブルームピンポンRT・各パスのPSO/定数バッファを生成する
     * @param dxCommon DirectX共通基盤
     * @param srvManager SRVディスクリプタヒープ管理
     */
    void Initialize(engine::DirectXCommon* dxCommon, SrvManager* srvManager);
    /**
     * @brief 生成した全RT・PSO・定数バッファを解放する
     */
    void Finalize();

    // シーン描画の前後に呼んでオフスクリーンテクスチャへ切り替える
    /**
     * @brief シーンキャプチャ用RTをレンダーターゲットに設定する
     * @note 3D/Sprite描画の前に呼ぶ
     */
    void BeginScene();
    /**
     * @brief シーンキャプチャ用テクスチャをSRV状態へ遷移させる
     * @note 3D/Sprite描画の後、Apply() の前に呼ぶ
     */
    void EndScene();

    // 4パスのブルームをかけてバックバッファへ合成するEndScene() 後に呼ぶ
    /**
     * @brief 輝度抽出→水平ブラー→垂直ブラー→加算合成の4パスを実行しバックバッファへ合成する
     * @param srvManager SRVディスクリプタヒープ管理
     */
    void Apply(SrvManager* srvManager);

    // パラメータ

    void SetEnabled(bool e) { enabled_ = e; }
    bool IsEnabled() const { return enabled_; }

    void SetThreshold(float t)
    {
        if (brightCbData_) {
            brightCbData_->threshold = t;
        }
    }
    float GetThreshold() const { return brightCbData_ ? brightCbData_->threshold : 0.7f; }

    void SetIntensity(float i)
    {
        if (brightCbData_) {
            brightCbData_->intensity = i;
        }
    }
    float GetIntensity() const { return brightCbData_ ? brightCbData_->intensity : 1.0f; }

    // シーン RTV ハンドル（BeginScene 後に GrayscaleEffect などとの切り替えに使用）
    D3D12_CPU_DESCRIPTOR_HANDLE GetSceneRTVHandle() const { return sceneRtvHandle_; }

private:
    BloomEffect() = default;
    ~BloomEffect() = default;
    BloomEffect(const BloomEffect&) = delete;
    BloomEffect& operator=(const BloomEffect&) = delete;

    // ヘルパー

    /// リソースバリアを発行するユーティリティ
    void Barrier(ID3D12GraphicsCommandList* cmd,
        ID3D12Resource* res,
        D3D12_RESOURCE_STATES before,
        D3D12_RESOURCE_STATES after) const;

    /// RTV / SRV 付きの UNORM テクスチャを生成する
    void CreateRenderTexture(ID3D12Device* device,
        SrvManager* srvManager,
        UINT width, UINT height,
        Microsoft::WRL::ComPtr<ID3D12Resource>& outRes,
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>& outRtvHeap,
        D3D12_CPU_DESCRIPTOR_HANDLE& outRtvHandle,
        uint32_t& outSrvIndex,
        DXGI_FORMAT rtvFmt,
        DXGI_FORMAT srvFmt);

    // ルートシグネチャ生成ヘルパー
    Microsoft::WRL::ComPtr<ID3D12RootSignature>
    CreateRS_CB_SRV(ID3D12Device* device) const; // b0 + t0 (Bright/Blur)

    Microsoft::WRL::ComPtr<ID3D12RootSignature>
    CreateRS_SRV2(ID3D12Device* device) const; // t0 + t1 (Combine)

    // PSO 生成ヘルパー
    Microsoft::WRL::ComPtr<ID3D12PipelineState>
    CreatePSO(ID3D12Device* device,
        ID3D12RootSignature* rootSig,
        IDxcBlob* vsBlob, IDxcBlob* psBlob,
        DXGI_FORMAT rtvFmt) const;

    engine::DirectXCommon* dxCommon_ = nullptr;

    // シーンキャプチャ RT
    Microsoft::WRL::ComPtr<ID3D12Resource> sceneTexture_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> sceneRtvHeap_;
    D3D12_CPU_DESCRIPTOR_HANDLE sceneRtvHandle_ = { };
    uint32_t sceneSrvIndex_ = UINT32_MAX;
    bool sceneFirstFrame_ = true;

    // Bloom ピンポン RT [0],[1]
    // 常に Full resolution (UNORM, ガンマ非補正の中間バッファ)
    Microsoft::WRL::ComPtr<ID3D12Resource> bloomTex_[2];
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> bloomRtvHeap_[2];
    D3D12_CPU_DESCRIPTOR_HANDLE bloomRtvHandle_[2] = { };
    uint32_t bloomSrvIndex_[2] = { UINT32_MAX, UINT32_MAX };
    bool bloomInSrv_[2] = { false, false }; ///< true=SRV状態

    // PSO & ルートシグネチャ
    Microsoft::WRL::ComPtr<ID3D12RootSignature> brightRS_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> brightPSO_;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> blurRS_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> blurPSO_;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> combineRS_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> combinePSO_;

    // 定数バッファ
    /**
     * @brief 輝度抽出パス（BrightPass）PS用の定数バッファに1:1で対応する構造体
     */
    struct BrightParams {
        float threshold;
        float intensity;
        float pad[2];
    };
    Microsoft::WRL::ComPtr<ID3D12Resource> brightCb_;
    BrightParams* brightCbData_ = nullptr;

    /**
     * @brief ガウシアンブラーパス（水平/垂直共用）PS用の定数バッファに1:1で対応する構造体
     */
    struct BlurParams {
        float dirX;
        float dirY;
        float texW;
        float texH;
    };
    Microsoft::WRL::ComPtr<ID3D12Resource> blurCb_[2]; ///< [0]=水平, [1]=垂直
    BlurParams* blurCbData_[2] = { };

    bool enabled_ = false;
};

} // namespace engine::graphics
