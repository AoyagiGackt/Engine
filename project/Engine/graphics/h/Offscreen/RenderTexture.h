/**
 * @file RenderTexture.h
 * @brief RenderTextureの描画資源とGPU処理の管理に関する公開型と操作インターフェースを定義するファイル
 */
#pragma once
#include "DirectXCommon.h"
#include "SrvManager.h"
#include <wrl/client.h>
namespace engine::graphics {

class RenderTexture {
public:
    /**
     * @brief Initialize に対応する処理を開始する
     * @param dxCommon 処理に使用する値
     * @param srvManager 処理に使用する値
     * @param width 処理に使用する値
     * @param height 処理に使用する値
     * @return なし
     */
    void Initialize(engine::DirectXCommon* dxCommon, SrvManager* srvManager, uint32_t width, uint32_t height);
    /**
     * @brief BeginRendering に対応する処理を実行する
     * @return なし
     */
    void BeginRendering();
    /**
     * @brief EndRendering に対応する処理を実行する
     * @return なし
     */
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
