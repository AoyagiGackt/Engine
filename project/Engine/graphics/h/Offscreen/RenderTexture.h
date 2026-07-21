/**
 * @file RenderTexture.h
 * @brief RenderTextureが公開する型とAPIを定義するファイル
 */
#pragma once
#include "DirectXCommon.h"
#include "SrvManager.h"
#include <wrl/client.h>
namespace engine::graphics {

class RenderTexture {
public:
    void Initialize(engine::DirectXCommon* dxCommon, SrvManager* srvManager, uint32_t width, uint32_t height);
    void BeginRendering();
    void EndRendering();

    /** @brief 確保したSRVインデックスをSrvManagerへ返却する（再生成前・破棄前に呼ぶこと） */
    void Finalize(SrvManager* srvManager);

    uint32_t GetSrvIndex() const { return srvIndex_; }

private:
    engine::DirectXCommon* dxCommon_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap_;
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle_ = { };
    uint32_t srvIndex_ = 0;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    bool isFirstFrame_ = true;
};

} // namespace engine::graphics
