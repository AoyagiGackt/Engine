/**
 * @file SkinCS.h
 * @brief スキニングをコンピュートシェーダーで計算する処理（SkinCS）の宣言
 */
#pragma once
#include "DirectXCommon.h"
#include <d3d12.h>
#include <wrl/client.h>
namespace engine::graphics {

// Compute Shader による GPU スキニングを管理するクラス
// データフロー
//   元の頂点バッファ (SRV, t0)
//   × スキニングパレット (CBV, b0)
//   → 計算済み頂点バッファ (UAV, u0)
//   → 描画パスで頂点バッファ (VBV) として使用
// 使い方
//   1. Initialize() で初期化
//   2. 毎フレーム Update() より後で Dispatch() を呼ぶ
//   3. Draw() で GetOutputVBV() を IASetVertexBuffers に渡す
/**
 * @brief Compute Shader でボーン変形を計算し、結果を頂点バッファとして出力するクラス
 */
class SkinCS {
public:
    // 出力頂点レイアウト  ModelCommon の入力レイアウトと完全一致
    // POSITION + TEXCOORD + NORMAL + TANGENT の順に格納する
    /**
     * @brief SkinningCS.hlsl が書き込む変形後頂点1個分のレイアウト（接線を含む48バイト）
     */
    struct OutputVertex {
        /** @brief 変形後の同次座標 */
        float position[4]; // 16 バイト
        /** @brief 元の頂点から引き継ぐUV座標 */
        float texcoord[2]; //  8 バイト
        /** @brief スキン行列で変換した法線 */
        float normal[3]; // 12 バイト
        /** @brief 法線と直交する接線 */
        float tangent[3];
    };

    /** @brief 標準頂点を構成する浮動小数点要素数 */
    static constexpr size_t kOutputFloatCount = 12;
    static_assert(sizeof(OutputVertex) == sizeof(float) * kOutputFloatCount);

    // inputBuffer   SkinnedModel::GetVertexResource() を渡す (GENERIC_READ 状態)
    // vertexCount   SkinnedModel::GetVertexCount()
    void Initialize(engine::DirectXCommon* dxCommon,
        ID3D12Resource* inputBuffer,
        UINT vertexCount);

    // スキニング計算を Dispatch する。Draw() より前に呼ぶこと
    // paletteCBAddress は、フレーム領域へ転送済みのパレットを指す
    void Dispatch(ID3D12GraphicsCommandList* cmd,
        D3D12_GPU_VIRTUAL_ADDRESS paletteCBAddress);

    // 計算済み頂点バッファのビューを返すIASetVertexBuffers に渡す
    const D3D12_VERTEX_BUFFER_VIEW& GetOutputVBV() const {
        return outputVBV_;
    }

private:
    Microsoft::WRL::ComPtr<ID3D12RootSignature> csRootSig_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> csPSO_;

    // CS 出力バッファ (DEFAULT heap, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)
    Microsoft::WRL::ComPtr<ID3D12Resource> outputBuffer_;
    D3D12_VERTEX_BUFFER_VIEW outputVBV_ { };

    D3D12_GPU_VIRTUAL_ADDRESS inputGpuVA_ = 0; // 入力バッファの GPU VA
    UINT vertexCount_ = 0;

    // 2 フレーム目以降、Dispatch 前に VERTEX_AND_CONSTANT_BUFFER → UAV の遷移が必要
    bool needsPreTransition_ = false;
};

} // namespace engine::graphics
