/**
 * @file ImpactRadialBlurEffect.h
 * @brief バックバッファをキャプチャして放射状のブラーを重ねる、一瞬だけの衝撃演出を描画するファイル
 */
#pragma once
#include "DirectXCommon.h"
#include "SrvManager.h"
#include <cstdint>
#include <wrl/client.h>

namespace engine::graphics {

/**
 * @brief 衝撃用ラジアルブラー
 * @note ImageFilterのようにシーンの描画先をオフスクリーンへ切り替えず、描き終えたバックバッファを
 *       コピーしてブラーを掛けて描き戻す（描画先の切り替えに対応していない描画が混ざっても絵が欠けない）。
 *       バックバッファ直描き時のみ有効
 */
class ImpactRadialBlurEffect {
public:
    void Initialize(engine::DirectXCommon* dxCommon, SrvManager* srvManager);

    /** @brief ブラーの中心（画面UV 0〜1）と強さを設定する（強さ0で描画しない） */
    void SetParams(float centerU, float centerV, float strength, int sampleCount);

    /** @brief 描画すべき強さがあるか */
    bool IsActive() const { return strength_ > kMinStrength; }

    /** @brief バックバッファをキャプチャし、ブラーを掛けて描き戻す（3D描画の後、UIの前に呼ぶ） */
    void CaptureAndApply();

private:
    static constexpr float kMinStrength = 0.001f; ///< これ未満は描画しない

    /** @brief RadialBlurPS.hlsl の RadialBlurParams と一致させること */
    struct RadialBlurParams {
        float centerX = 0.5f;
        float centerY = 0.5f;
        float strength = 0.0f;
        int sampleCount = 16;
    };

    void CreatePipeline();

    engine::DirectXCommon* dxCommon_ = nullptr;
    SrvManager* srvManager_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
    Microsoft::WRL::ComPtr<ID3D12Resource> cbResource_;
    RadialBlurParams* cbData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> captureTexture_;
    uint32_t captureSrvIndex_ = UINT32_MAX;

    float strength_ = 0.0f;
};

} // namespace engine::graphics
