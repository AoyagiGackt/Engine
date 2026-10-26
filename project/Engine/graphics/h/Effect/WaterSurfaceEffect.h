/** @file WaterSurfaceEffect.h
 * @brief 背景の屈折と変形した水面を合成する描画資源 */
#pragma once
#include "DirectXCommon.h"
#include "Vector4.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <wrl/client.h>

namespace engine::graphics {
class SrvManager;

/** @brief 水面の点列と背景画像を使って水を合成する */
class WaterSurfaceEffect {
public:
    static constexpr size_t kMaxSamples = 128;
    static constexpr size_t kHeaderVectors = 8;
    static constexpr size_t kVectorBytes = 16;
    /** @brief HLSL の定数バッファと同じ配置の描画設定 */
    struct Parameters {
        Vector4 bounds{}; ///< 水域の左端、右端、水面、水底のワールド座標
        Vector4 view{}; ///< カメラの中心と可視範囲の半幅、半高
        Vector4 animation{}; ///< 時刻、点数、屈折量、屈折速度
        Vector4 optical{}; ///< 網目の密度、速度、強度、泡の幅
        Vector4 deepColor{};
        Vector4 surfaceColor{};
        Vector4 captureUv{}; ///< キャプチャ上の表示領域の原点と大きさ
        Vector4 detail{}; ///< 明るい層の深さ、泡の強度
        std::array<Vector4, kMaxSamples> heights{};
    };
    static_assert(offsetof(Parameters, heights) == kHeaderVectors * kVectorBytes);
    static_assert(sizeof(Parameters) == (kHeaderVectors + kMaxSamples) * kVectorBytes);

    /** @brief 永続マップを閉じて所有する描画資源を解放する */
    ~WaterSurfaceEffect();
    /** @brief 描画中に確保を行わないよう全資源を用意する */
    void Initialize(engine::DirectXCommon* dxCommon, SrvManager* srvManager);
    /** @brief ポストエフェクト使用時の背景を専用の描画先へ送る */
    void BeginBackground(bool targetsBackBuffer);
    /** @brief 更新済みの波形を定数バッファへ転送する */
    void Update(const Parameters& parameters);
    /** @brief 背景確定後に屈折と水色を現在の描画先へ合成する */
    void Draw();

private:
    engine::DirectXCommon* dxCommon_ = nullptr;
    SrvManager* srvManager_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> captureTexture_;
    Microsoft::WRL::ComPtr<ID3D12Resource> backgroundTexture_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> backgroundRtvHeap_;
    D3D12_CPU_DESCRIPTOR_HANDLE backgroundRtv_{};
    uint32_t captureSrv_ = UINT32_MAX;
    uint32_t backgroundSrv_ = UINT32_MAX;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
    Microsoft::WRL::ComPtr<ID3D12Resource> constants_;
    std::byte* mapped_ = nullptr;
    size_t constantStride_{};
    bool usesBackground_ = false;
};
} // namespace engine::graphics
