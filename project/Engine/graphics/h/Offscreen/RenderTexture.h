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
     * @brief 指定サイズの RTV/SRV 兼用テクスチャ（RGBA8_UNORM）を確保する
     * @param dxCommon   デバイス取得に使う DirectX 基盤
     * @param srvManager SRV 確保に使う SrvManager（Finalize でも同じインスタンスを渡すこと）
     * @param width      テクスチャ幅（ピクセル）
     * @param height     テクスチャ高さ（ピクセル）
     */
    void Initialize(engine::DirectXCommon* dxCommon, SrvManager* srvManager, uint32_t width, uint32_t height);
    /**
     * @brief レンダーターゲットとして設定し、ビューポート/シザーを適用して赤でクリアする
     * @note 2回目以降は PIXEL_SHADER_RESOURCE → RENDER_TARGET のバリアを行う
     */
    void BeginRendering();
    /**
     * @brief 描画を終え、リソース状態を RENDER_TARGET から PIXEL_SHADER_RESOURCE へ遷移する
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
