/**
 * @file CascadedShadowMap.cpp
 * @brief 3段のカスケードシャドウマップ（CascadedShadowMap）の実装
 */
#include "CascadedShadowMap.h"
#include "GraphicsTuning.h"
#include <string>
#include "PipelineHelper.h"
#include "ResourceAlignment.h"
#include "EngineAssert.h"
#include "ShadowManager.h"
#include "WinApp.h"
#include <cmath>

namespace engine::graphics {

using namespace Microsoft::WRL;


/** @brief Texture2DArray シャドウマップ・DSV/SRV・カスケード定数バッファを確保する */
void CascadedShadowMap::Initialize(DirectXCommon* dxCommon, SrvManager* srvManager) {
    // 投影範囲を初期化時に読み込み、不正な距離順序は既定設定に戻す。
    float defaultSplits[kNumCascades] = {};
    bool validSplits = true;
    for (uint32_t i = 0; i < kNumCascades; ++i) {
        auto& config = cascadeConfigs_[i];
        const auto defaults = config;
        defaultSplits[i] = defaults.splitDist;
        const std::string prefix = "shadow.cascade" + std::to_string(i) + ".";
        config.orthoWidth = GraphicsTuning::Positive((prefix + "width").c_str(), defaults.orthoWidth);
        config.orthoHeight = GraphicsTuning::Positive((prefix + "height").c_str(), defaults.orthoHeight);
        config.nearZ = GraphicsTuning::Positive((prefix + "nearClip").c_str(), defaults.nearZ);
        config.farZ = GraphicsTuning::Positive((prefix + "farClip").c_str(), defaults.farZ);
        config.splitDist = GraphicsTuning::Positive((prefix + "splitDistance").c_str(), defaults.splitDist);
        if (config.farZ <= config.nearZ) {
            config.nearZ = defaults.nearZ;
            config.farZ = defaults.farZ;
        }
        validSplits = validSplits && (i == 0 || config.splitDist > cascadeConfigs_[i - 1].splitDist);
    }
    // 分割距離が手前から順に並んでいなければ、分割距離だけ全段既定値に戻す
    if (!validSplits) {
        for (uint32_t i = 0; i < kNumCascades; ++i) {
            cascadeConfigs_[i].splitDist = defaultSplits[i];
        }
    }

    dxCommon_ = dxCommon;
    srvManager_ = srvManager;
    ID3D12Device* device = dxCommon->GetDevice();

    // Texture2DArray[3] (R32_TYPELESS)
    {
        shadowTex_ = pipelineHelper::CreateDepthTexture(device, kShadowMapSize, kShadowMapSize, kNumCascades, DXGI_FORMAT_R32_TYPELESS);

        // DSV ヒープ（3スライス分）
        dsvHeap_ = DirectXCommon::CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, kNumCascades);

        UINT dsvSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
        D3D12_CPU_DESCRIPTOR_HANDLE base = dsvHeap_->GetCPUDescriptorHandleForHeapStart();

        for (uint32_t i = 0; i < kNumCascades; ++i) {
            dsvHandles_[i] = { base.ptr + (SIZE_T)i * dsvSize };

            D3D12_DEPTH_STENCIL_VIEW_DESC dsvViewDesc = { };
            dsvViewDesc.Format = DXGI_FORMAT_D32_FLOAT;
            dsvViewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
            dsvViewDesc.Texture2DArray.FirstArraySlice = i;
            dsvViewDesc.Texture2DArray.ArraySize = 1;
            dsvViewDesc.Texture2DArray.MipSlice = 0;
            device->CreateDepthStencilView(shadowTex_.Get(), &dsvViewDesc, dsvHandles_[i]);

            cascadeInDepthWrite_[i] = true; // 初期状態 DEPTH_WRITE
        }

        // SRV (Texture2DArray, R32_FLOAT)
        shadowSrvIndex_ = srvManager->Allocate();
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = { };
        srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2DArray.MipLevels = 1;
        srvDesc.Texture2DArray.ArraySize = kNumCascades;
        srvDesc.Texture2DArray.FirstArraySlice = 0;
        device->CreateShaderResourceView(shadowTex_.Get(), &srvDesc,
            srvManager->GetCPUDescriptorHandle(shadowSrvIndex_));
    }

    // 定数バッファ
    {
        cascadeCBRes_ = pipelineHelper::CreateUploadBuffer(device, AlignConstantBufferSize(sizeof(CascadeDataLayout)));
        cascadeCBRes_->Map(0, nullptr, reinterpret_cast<void**>(&cascadeCBData_));

        cascadeCBData_->numCascades = float(kNumCascades);
        for (uint32_t i = 0; i < kNumCascades; ++i) {
            cascadeCBData_->splitDist[i] = cascadeConfigs_[i].splitDist;
            cascadeCBData_->cascadeVP[i] = MakeIdentity4x4();
        }
    }

    // ダミー定数バッファ (256 bytes 以上)
    {
        dummyCBRes_ = pipelineHelper::CreateUploadBuffer(device, kConstantBufferAlignment);
    }
}

// ComputeCascadeVP

/** @brief 指定カスケードのライトのビュー射影行列を求める（作れなければ単位行列） */
Matrix4x4 CascadedShadowMap::ComputeCascadeVP(const Vector3& lightDir, uint32_t cascadeIdx) {
    const CascadeConfig& cfg = cascadeConfigs_[cascadeIdx];

    // 通常の影と同じ設定から投影中心とライト位置を求める
    const Vector3 center = ShadowManager::SceneCenter();
    const Vector3 eye = ShadowManager::LightPosition(lightDir);

    Matrix4x4 view {};
    if (!TryMakeLightViewMatrix(eye, center, view)) {
        return MakeIdentity4x4();
    }
    const Matrix4x4 proj = MakeCenteredOrthographicMatrix(cfg.orthoWidth, cfg.orthoHeight, cfg.nearZ, cfg.farZ);

    return Multiply(view, proj);
}

/** @brief 各カスケードの固定フラスタムパラメータからライト視点の VP 行列を再計算する */
void CascadedShadowMap::Update(const Vector3& lightDir) {
    for (uint32_t i = 0; i < kNumCascades; ++i) {
        cascadeVP_[i] = ComputeCascadeVP(lightDir, i);
        cascadeCBData_->cascadeVP[i] = cascadeVP_[i];
    }
}

// BeginCascade / EndCascade

/** @brief 指定カスケードの DSV を深度書き込み用にセットし、クリアしてビューポートを設定する */
void CascadedShadowMap::BeginCascade(ID3D12GraphicsCommandList* cmd, uint32_t cascadeIdx) {
    ENGINE_ASSERT(cascadeIdx < kNumCascades);

    // サブリソース = 配列スライス (mip0, arraySlice=cascadeIdx)
    UINT subresource = cascadeIdx; // D3D12CalcSubresource(0, cascadeIdx, 0, 1, kNumCascades)

    if (!cascadeInDepthWrite_[cascadeIdx]) {
        D3D12_RESOURCE_BARRIER b = { };
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = shadowTex_.Get();
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        b.Transition.Subresource = subresource;
        cmd->ResourceBarrier(1, &b);
        cascadeInDepthWrite_[cascadeIdx] = true;
    }

    cmd->OMSetRenderTargets(0, nullptr, FALSE, &dsvHandles_[cascadeIdx]);
    cmd->ClearDepthStencilView(dsvHandles_[cascadeIdx], D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    D3D12_VIEWPORT vp = { 0, 0, float(kShadowMapSize), float(kShadowMapSize), 0.0f, 1.0f };
    D3D12_RECT scissor = { 0, 0, static_cast<LONG>(kShadowMapSize), static_cast<LONG>(kShadowMapSize) };
    pipelineHelper::SetViewportAndScissor(cmd, vp, scissor);
}

/** @brief BeginCascade で書き込み状態にした全カスケードを PIXEL_SHADER_RESOURCE 状態へ遷移する */
void CascadedShadowMap::EndCascade(ID3D12GraphicsCommandList* cmd) {
    // すべてのカスケードを SRV 状態に遷移（BeginCascade をすべて終えてから呼ぶ）
    for (uint32_t i = 0; i < kNumCascades; ++i) {
        if (!cascadeInDepthWrite_[i]) {
            continue;
        }

        D3D12_RESOURCE_BARRIER b = { };
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = shadowTex_.Get();
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        b.Transition.Subresource = i;
        cmd->ResourceBarrier(1, &b);
        cascadeInDepthWrite_[i] = false;
    }
}

// SetShadowMapSRV / SetCascadeDataCBV

/** @brief シャドウマップ（Texture2DArray）の SRV を影用のルートスロットへバインドする */
void CascadedShadowMap::SetShadowMapSRV(ID3D12GraphicsCommandList* cmd, SrvManager* srvManager) {
    cmd->SetGraphicsRootDescriptorTable(pipelineHelper::modelRootSlot::kShadow, srvManager->GetGPUDescriptorHandle(shadowSrvIndex_));
}

/** @brief カスケードVP行列・スプリット距離を格納した定数バッファを指定スロットへバインドする */
void CascadedShadowMap::SetCascadeDataCBV(ID3D12GraphicsCommandList* cmd, uint32_t slot) {
    cmd->SetGraphicsRootConstantBufferView(slot, cascadeCBRes_->GetGPUVirtualAddress());
}

} // namespace engine::graphics
