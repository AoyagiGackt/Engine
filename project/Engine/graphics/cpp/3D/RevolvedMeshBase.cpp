/**
 * @file RevolvedMeshBase.cpp
 * @brief 回転体メッシュ（円柱・リング）の共通のGPU資源と描画（RevolvedMeshBase）の実装
 */
#include "RevolvedMeshBase.h"
#include "PipelineHelper.h"
#include "DirectXCommon.h"
#include "EngineAssert.h"
#include "SrvManager.h"
#include "TextureManager.h"
#include <cmath>
#include <numbers>

namespace engine::graphics {
namespace {

// 回転体の最小の分割数
constexpr int kMinDivisions = 3;

}
using namespace Microsoft::WRL;

/** @brief テクスチャ読み込み・頂点バッファ・マテリアル定数バッファの確保を行う（Initialize共通部） */
void RevolvedMeshBase::SetupCommonBuffers(DirectXCommon* dxCommon, const std::string& textureFilePath,
    int divisions, size_t materialCBSize, void** mappedMaterialOut) {
    ENGINE_ASSERT(dxCommon);
    ENGINE_ASSERT(divisions >= kMinDivisions);
    dxCommon_ = dxCommon;
    textureFilePath_ = textureFilePath;
    divisions_ = divisions;
    vertexCount_ = divisions_ * kVerticesPerDivision;

    TextureManager::GetInstance()->LoadTexture(textureFilePath_);

    materialBuffer_ = dxCommon_->CreateBufferResource(materialCBSize);
    materialBuffer_->Map(0, nullptr, mappedMaterialOut);

    vertexBuffer_ = dxCommon_->CreateBufferResource(sizeof(VertexData) * vertexCount_);
    vertexBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData_));
    vbv_.BufferLocation = vertexBuffer_->GetGPUVirtualAddress();
    vbv_.SizeInBytes = static_cast<UINT>(sizeof(VertexData) * vertexCount_);
    vbv_.StrideInBytes = sizeof(VertexData);
}

/** @brief カメラのビュー射影から求めたWVP行列を派生クラスのマテリアル定数バッファへ反映する */
void RevolvedMeshBase::Update(Camera* camera) {
    Matrix4x4 world = MakeAffineMatrix({ scale_, scale_, scale_ }, rotation_, position_);
    Matrix4x4 viewProj = Multiply(camera->GetViewMatrix(), camera->GetProjectionMatrix());
    SetWVP(Multiply(world, viewProj));
}

/** @brief ルートシグネチャ・PSO・頂点バッファ・マテリアル定数バッファを使って描画する */
void RevolvedMeshBase::Draw() {
    ID3D12GraphicsCommandList* cmd = dxCommon_->GetCommandList();

    pipelineHelper::SetTrianglePipeline(cmd, rootSignature_.Get(), pipelineState_.Get());
    cmd->IASetVertexBuffers(0, 1, &vbv_);

    cmd->SetGraphicsRootConstantBufferView(pipelineHelper::effectMeshRootSlot::kConstants, materialBuffer_->GetGPUVirtualAddress());

    SrvManager::GetInstance()->PreDraw();
    D3D12_GPU_DESCRIPTOR_HANDLE texHandle = TextureManager::GetInstance()->GetSrvHandleGPU(textureFilePath_);
    cmd->SetGraphicsRootDescriptorTable(pipelineHelper::effectMeshRootSlot::kTexture, texHandle);

    cmd->DrawInstanced(vertexCount_, 1, 0, 0);
}

/** @brief ルートシグネチャ・PSOを生成する（シェーダーパスとサンプラーのV方向モードのみ派生クラスが指定） */
void RevolvedMeshBase::CreatePipelineCommon(const std::wstring& vsPath, const std::wstring& psPath,
    D3D12_TEXTURE_ADDRESS_MODE addressV) {
    ID3D12Device* device = dxCommon_->GetDevice();

    D3D12_STATIC_SAMPLER_DESC sampler = pipelineHelper::MakePixelSampler(D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP, addressV, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

    rootSignature_ = pipelineHelper::CreateConstantTextureSignature(device, D3D12_SHADER_VISIBILITY_ALL,
        sampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
    ENGINE_ASSERT(rootSignature_);

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = pipelineHelper::MakeEffectMeshPsoDesc(BlendMode::kAdd, D3D12_DEPTH_WRITE_MASK_ZERO);
    psoDesc.pRootSignature = rootSignature_.Get();
    psoDesc.InputLayout = { pipelineHelper::kEffectMeshInputLayout, _countof(pipelineHelper::kEffectMeshInputLayout) };

    // 加算ブレンド（発光/エフェクト向け）・カリングなし・深度書き込みなしは MakeEffectMeshPsoDesc で設定済み
    pipelineState_ = pipelineHelper::CreateGraphicsPipeline(dxCommon_, psoDesc,
        vsPath.c_str(), psPath.c_str());
}

/** @brief index 番目の区間の角度と UV を返す（円周を divisions_ 等分する） */
RevolvedMeshBase::SegmentAngles RevolvedMeshBase::SegmentAt(int index) const {
    const float radianPerDivide = 2.0f * std::numbers::pi_v<float> / static_cast<float>(divisions_);
    const float angle = static_cast<float>(index) * radianPerDivide;
    const float angleNext = static_cast<float>(index + 1) * radianPerDivide;
    return {
        std::sin(angle), std::cos(angle),
        std::sin(angleNext), std::cos(angleNext),
        static_cast<float>(index) / static_cast<float>(divisions_),
        static_cast<float>(index + 1) / static_cast<float>(divisions_),
    };
}

/** @brief index 番目の区間の四角形を、三角形2枚として頂点バッファへ書き込む */
void RevolvedMeshBase::WriteSegmentQuad(int index, const SegmentAngles& segment,
    const Vector4& upperCur, const Vector4& upperNext, const Vector4& lowerCur, const Vector4& lowerNext,
    const Vector3& normalCur, const Vector3& normalNext) {
    VertexData* v = vertexData_ + index * kVerticesPerDivision;
    // 三角形1: upperCur, upperNext, lowerCur
    v[0] = { upperCur, { segment.u, 0.0f }, normalCur };
    v[1] = { upperNext, { segment.uNext, 0.0f }, normalNext };
    v[2] = { lowerCur, { segment.u, 1.0f }, normalCur };
    // 三角形2: lowerCur, upperNext, lowerNext
    v[3] = { lowerCur, { segment.u, 1.0f }, normalCur };
    v[4] = { upperNext, { segment.uNext, 0.0f }, normalNext };
    v[5] = { lowerNext, { segment.uNext, 1.0f }, normalNext };
}

} // namespace engine::graphics
