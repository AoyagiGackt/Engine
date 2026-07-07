#pragma once
#include "DirectXCommon.h"
#include "SrvManager.h"
#include <wrl/client.h>
namespace engine::graphics {

/**
 * @brief 「有効かどうか」と「オフスクリーンRTV」を提供するポストエフェクトの共通インターフェース
 * @note GetActiveSceneRTVHandle() がこのインターフェース経由で各エフェクトを走査することで、
 * 新しいポストエフェクトを追加してもその関数自体を変更しなくて済むようにする（Strategyパターン）
 */
class IPostEffectSource {
public:
    virtual ~IPostEffectSource() = default;
    virtual bool IsEnabled() const = 0;
    virtual D3D12_CPU_DESCRIPTOR_HANDLE GetSceneRTVHandle() const = 0;
};

// シーンをオフスクリーンに描画し、フルスクリーンPSで加工してバックバッファへ合成する処理の共通基盤
// GrayscaleEffect/HsvFilterのように定数バッファ1つ+SRV1枚のフルスクリーンパスはこれを継承する
class PostEffectFullscreenPass : public IPostEffectSource {
public:
    void BeginScene();
    void EndScene();
    void Apply(SrvManager* srvManager);

    void SetEnabled(bool v) { enabled_ = v; }
    bool IsEnabled()  const override { return enabled_; }

    D3D12_CPU_DESCRIPTOR_HANDLE GetSceneRTVHandle() const override { return rtvHandle_; }

protected:
    PostEffectFullscreenPass() = default;
    ~PostEffectFullscreenPass() = default;
    PostEffectFullscreenPass(const PostEffectFullscreenPass&) = delete;
    PostEffectFullscreenPass& operator=(const PostEffectFullscreenPass&) = delete;

    // オフスクリーンRTV/SRV・定数バッファ(256byte固定)・ルートシグネチャ/PSOを生成する
    // 戻り値はマップ済み定数バッファの先頭アドレス（派生クラスが自分のParams構造体にキャストして使う）
    void* InitializeCommon(engine::DirectXCommon* dxCommon, SrvManager* srvManager, const wchar_t* psShaderPath);
    void  FinalizeCommon();

    engine::DirectXCommon* dxCommon_ = nullptr;

private:
    Microsoft::WRL::ComPtr<ID3D12Resource>       sceneTexture_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap_;
    D3D12_CPU_DESCRIPTOR_HANDLE                  rtvHandle_ = {};
    uint32_t                                     srvIndex_  = UINT32_MAX;
    bool                                         isFirstFrame_ = true;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
    Microsoft::WRL::ComPtr<ID3D12Resource>      cbResource_;
    void*                                       cbData_ = nullptr;

    bool enabled_ = false;
};

} // namespace engine::graphics
