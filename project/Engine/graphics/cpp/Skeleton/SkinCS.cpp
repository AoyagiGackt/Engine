/**
 * @file SkinCS.cpp
 * @brief スキニングをコンピュートシェーダーで計算する処理（SkinCS）の実装
 */
#include "SkinCS.h"
#include "PipelineHelper.h"
#include "EngineAssert.h"

namespace engine::graphics {
namespace {

// スキニングのスレッドグループ
constexpr UINT kComputeThreadsPerGroup = 64;

/** @brief スキニング用コンピュートシェーダーのルートパラメータ番号 */
namespace skinComputeRootSlot {
constexpr UINT kVertexCount = 0; ///< 頂点数（32ビット定数, b1）
constexpr UINT kInputVertices = 1; ///< 入力頂点バッファ（SRV, t0）
constexpr UINT kPalette = 2; ///< スキニングパレット（CBV, b0）
constexpr UINT kOutputVertices = 3; ///< 出力頂点バッファ（UAV, u0）
constexpr UINT kCount = 4; ///< パラメータの数
}

}

using namespace Microsoft::WRL;

/** @brief スキニング計算用のコンピュートシェーダーの資源を用意する */
void SkinCS::Initialize(DirectXCommon* dxCommon,
    ID3D12Resource* inputBuffer,
    UINT vertexCount) {
    ENGINE_ASSERT(inputBuffer);
    ENGINE_ASSERT(vertexCount > 0);

    vertexCount_ = vertexCount;
    inputGpuVA_ = inputBuffer->GetGPUVirtualAddress();
    ID3D12Device* device = dxCommon->GetDevice();

    // コンピュートルートシグネチャ
    //   スロット 0 (b1): 頂点数 (32ビット定数)
    //   スロット 1 (t0): 入力頂点バッファ (SRV)
    //   スロット 2 (b0): スキニングパレット (CBV)
    //   スロット 3 (u0): 出力頂点バッファ (UAV)

    D3D12_ROOT_PARAMETER params[skinComputeRootSlot::kCount] { };

    params[skinComputeRootSlot::kVertexCount].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[skinComputeRootSlot::kVertexCount].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[skinComputeRootSlot::kVertexCount].Constants.ShaderRegister = 1; // b1
    params[skinComputeRootSlot::kVertexCount].Constants.RegisterSpace = 0;
    params[skinComputeRootSlot::kVertexCount].Constants.Num32BitValues = 1;

    params[skinComputeRootSlot::kInputVertices].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[skinComputeRootSlot::kInputVertices].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[skinComputeRootSlot::kInputVertices].Descriptor.ShaderRegister = 0; // t0
    params[skinComputeRootSlot::kInputVertices].Descriptor.RegisterSpace = 0;

    params[skinComputeRootSlot::kPalette].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[skinComputeRootSlot::kPalette].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[skinComputeRootSlot::kPalette].Descriptor.ShaderRegister = 0; // b0
    params[skinComputeRootSlot::kPalette].Descriptor.RegisterSpace = 0;

    params[skinComputeRootSlot::kOutputVertices].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    params[skinComputeRootSlot::kOutputVertices].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[skinComputeRootSlot::kOutputVertices].Descriptor.ShaderRegister = 0; // u0
    params[skinComputeRootSlot::kOutputVertices].Descriptor.RegisterSpace = 0;

    D3D12_ROOT_SIGNATURE_DESC rsDesc { };
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    rsDesc.pParameters = params;
    rsDesc.NumParameters = skinComputeRootSlot::kCount;

    ComPtr<ID3DBlob> sigBlob, errBlob;
    HRESULT hr = D3D12SerializeRootSignature(
        &rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob);
    ENGINE_ASSERT(SUCCEEDED(hr));
    device->CreateRootSignature(
        0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(),
        IID_PPV_ARGS(&csRootSig_));

    // コンピュートPSO

    Microsoft::WRL::ComPtr<IDxcBlob> csBlob = dxCommon->CompileShader(
        L"Resources/shaders/skinned/SkinningCS.hlsl", L"cs_6_0");
    ENGINE_ASSERT(csBlob);

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc { };
    psoDesc.pRootSignature = csRootSig_.Get();
    psoDesc.CS = { csBlob->GetBufferPointer(), csBlob->GetBufferSize() };
    device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&csPSO_));

    // 出力バッファ (DEFAULT heap, UAV フラグ付き)

    UINT64 outputSize = static_cast<UINT64>(sizeof(OutputVertex)) * vertexCount;

    D3D12_HEAP_PROPERTIES heap { D3D12_HEAP_TYPE_DEFAULT };
    D3D12_RESOURCE_DESC rd = pipelineHelper::MakeBufferDesc(outputSize);
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    device->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &rd,
        D3D12_RESOURCE_STATE_COMMON, nullptr,
        IID_PPV_ARGS(&outputBuffer_));

    outputVBV_.BufferLocation = outputBuffer_->GetGPUVirtualAddress();
    outputVBV_.SizeInBytes = static_cast<UINT>(outputSize);
    outputVBV_.StrideInBytes = sizeof(OutputVertex); // 36 bytes
}

/** @brief スキニング計算をコマンドリストに積む */
void SkinCS::Dispatch(ID3D12GraphicsCommandList* cmd,
    D3D12_GPU_VIRTUAL_ADDRESS paletteCBAddress) {
    // 前フレームの描画パスで VERTEX_AND_CONSTANT_BUFFER に遷移済みなら UAV に戻す
    if (needsPreTransition_) {
        DirectXCommon::TransitionBarrier(cmd, outputBuffer_.Get(),
            D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }

    cmd->SetComputeRootSignature(csRootSig_.Get());
    cmd->SetPipelineState(csPSO_.Get());

    // スロット 0: 頂点数 (32ビット定数, b1)
    cmd->SetComputeRoot32BitConstant(skinComputeRootSlot::kVertexCount, vertexCount_, 0);
    // スロット 1: 入力頂点バッファ (SRV, t0)
    cmd->SetComputeRootShaderResourceView(skinComputeRootSlot::kInputVertices, inputGpuVA_);
    // スロット 2: スキニングパレット (CBV, b0)
    cmd->SetComputeRootConstantBufferView(skinComputeRootSlot::kPalette, paletteCBAddress);
    // スロット 3: 出力頂点バッファ (UAV, u0)
    cmd->SetComputeRootUnorderedAccessView(skinComputeRootSlot::kOutputVertices, outputBuffer_->GetGPUVirtualAddress());

    UINT threadGroups = (vertexCount_ + kComputeThreadsPerGroup - 1) / kComputeThreadsPerGroup;
    cmd->Dispatch(threadGroups, 1, 1);

    // CS の書き込み完了を保証する UAV バリア
    D3D12_RESOURCE_BARRIER uavBarrier { };
    uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarrier.UAV.pResource = outputBuffer_.Get();
    cmd->ResourceBarrier(1, &uavBarrier);

    // 描画パスで頂点バッファとして読めるよう状態を遷移
    DirectXCommon::TransitionBarrier(cmd, outputBuffer_.Get(),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);

    needsPreTransition_ = true;
}

} // namespace engine::graphics
