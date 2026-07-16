#include "CascadedShadowMap.h"
#include "EngineAssert.h"
#include "ShadowManager.h"
#include "WinApp.h"
#include <cmath>
using namespace engine;
using namespace engine::graphics;

using namespace Microsoft::WRL;

constexpr CascadedShadowMap::CascadeConfig CascadedShadowMap::kCascadeConfigs[CascadedShadowMap::kNumCascades];

void CascadedShadowMap::Initialize(DirectXCommon* dxCommon, SrvManager* srvManager)
{
    dxCommon_ = dxCommon;
    srvManager_ = srvManager;
    ID3D12Device* device = dxCommon->GetDevice();

    // Texture2DArray[3] (R32_TYPELESS)
    {
        D3D12_RESOURCE_DESC desc = { };
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = kShadowMapSize;
        desc.Height = kShadowMapSize;
        desc.DepthOrArraySize = kNumCascades;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R32_TYPELESS;
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        D3D12_CLEAR_VALUE cv = { };
        cv.Format = DXGI_FORMAT_D32_FLOAT;
        cv.DepthStencil.Depth = 1.0f;

        D3D12_HEAP_PROPERTIES heap = { D3D12_HEAP_TYPE_DEFAULT };
        HRESULT hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
            &desc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &cv,
            IID_PPV_ARGS(&shadowTex_));
        ENGINE_ASSERT(SUCCEEDED(hr));

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
        D3D12_HEAP_PROPERTIES heap = { D3D12_HEAP_TYPE_UPLOAD };
        D3D12_RESOURCE_DESC desc = { };
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = (sizeof(CascadeDataLayout) + 255) & ~255u;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&cascadeCBRes_));
        cascadeCBRes_->Map(0, nullptr, reinterpret_cast<void**>(&cascadeCBData_));

        cascadeCBData_->numCascades = float(kNumCascades);
        for (uint32_t i = 0; i < kNumCascades; ++i) {
            cascadeCBData_->splitDist[i] = kCascadeConfigs[i].splitDist;
            cascadeCBData_->cascadeVP[i] = MakeIdentity4x4();
        }
    }

    // ダミー定数バッファ (256 bytes 以上)
    {
        D3D12_HEAP_PROPERTIES heap = { D3D12_HEAP_TYPE_UPLOAD };
        D3D12_RESOURCE_DESC desc = { };
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = 256;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&dummyCBRes_));
    }
}

// ComputeCascadeVP

Matrix4x4 CascadedShadowMap::ComputeCascadeVP(const Vector3& lightDir, uint32_t cascadeIdx)
{
    const CascadeConfig& cfg = kCascadeConfigs[cascadeIdx];

    // ライト位置 = シーン中心 - lightDir * distance
    // ShadowManager のシーン中心・ライト距離定数をそのまま使用
    Vector3 center = { ShadowManager::kSceneCenterX, ShadowManager::kSceneCenterY, ShadowManager::kSceneCenterZ };
    Vector3 eye = {
        center.x - lightDir.x * ShadowManager::kLightDistance,
        center.y - lightDir.y * ShadowManager::kLightDistance,
        center.z - lightDir.z * ShadowManager::kLightDistance,
    };

    Vector3 fwd = {
        center.x - eye.x,
        center.y - eye.y,
        center.z - eye.z,
    };
    float fwdLen = std::sqrt(fwd.x * fwd.x + fwd.y * fwd.y + fwd.z * fwd.z);
    if (fwdLen < 1e-6f) {
        return MakeIdentity4x4();
    }
    fwd = { fwd.x / fwdLen, fwd.y / fwdLen, fwd.z / fwdLen };

    Vector3 up = (std::abs(fwd.y) > 0.99f) ? Vector3 { 0, 0, 1 } : Vector3 { 0, 1, 0 };

    Vector3 right = {
        up.y * fwd.z - up.z * fwd.y,
        up.z * fwd.x - up.x * fwd.z,
        up.x * fwd.y - up.y * fwd.x,
    };
    float rLen = std::sqrt(right.x * right.x + right.y * right.y + right.z * right.z);
    if (rLen < 1e-6f) {
        return MakeIdentity4x4();
    }
    right = { right.x / rLen, right.y / rLen, right.z / rLen };

    Vector3 realUp = {
        fwd.y * right.z - fwd.z * right.y,
        fwd.z * right.x - fwd.x * right.z,
        fwd.x * right.y - fwd.y * right.x,
    };

    float dotR = right.x * eye.x + right.y * eye.y + right.z * eye.z;
    float dotU = realUp.x * eye.x + realUp.y * eye.y + realUp.z * eye.z;
    float dotF = fwd.x * eye.x + fwd.y * eye.y + fwd.z * eye.z;

    Matrix4x4 view = { };
    view.m[0][0] = right.x;
    view.m[0][1] = right.y;
    view.m[0][2] = right.z;
    view.m[0][3] = 0;
    view.m[1][0] = realUp.x;
    view.m[1][1] = realUp.y;
    view.m[1][2] = realUp.z;
    view.m[1][3] = 0;
    view.m[2][0] = fwd.x;
    view.m[2][1] = fwd.y;
    view.m[2][2] = fwd.z;
    view.m[2][3] = 0;
    view.m[3][0] = -dotR;
    view.m[3][1] = -dotU;
    view.m[3][2] = -dotF;
    view.m[3][3] = 1;

    // 正射影（DirectX 深度 0-1）
    const float w = cfg.orthoWidth, h = cfg.orthoHeight;
    const float n = cfg.nearZ, f = cfg.farZ;

    Matrix4x4 proj = { };
    proj.m[0][0] = 2.0f / w;
    proj.m[1][1] = 2.0f / h;
    proj.m[2][2] = 1.0f / (f - n);
    proj.m[3][2] = -n / (f - n);
    proj.m[3][3] = 1.0f;

    return Multiply(view, proj);
}

void CascadedShadowMap::Update(const Vector3& lightDir)
{
    for (uint32_t i = 0; i < kNumCascades; ++i) {
        cascadeVP_[i] = ComputeCascadeVP(lightDir, i);
        cascadeCBData_->cascadeVP[i] = cascadeVP_[i];
    }
}

// BeginCascade / EndCascade

void CascadedShadowMap::BeginCascade(ID3D12GraphicsCommandList* cmd, uint32_t cascadeIdx)
{
    ENGINE_ASSERT(cascadeIdx < kNumCascades);

    // サブリソース = 배열スライス (mip0, arraySlice=cascadeIdx)
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
    cmd->RSSetViewports(1, &vp);
    cmd->RSSetScissorRects(1, &scissor);
}

void CascadedShadowMap::EndCascade(ID3D12GraphicsCommandList* cmd)
{
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

void CascadedShadowMap::SetShadowMapSRV(ID3D12GraphicsCommandList* cmd, SrvManager* srvManager)
{
    cmd->SetGraphicsRootDescriptorTable(4, srvManager->GetGPUDescriptorHandle(shadowSrvIndex_));
}

void CascadedShadowMap::SetCascadeDataCBV(ID3D12GraphicsCommandList* cmd, uint32_t slot)
{
    cmd->SetGraphicsRootConstantBufferView(slot, cascadeCBRes_->GetGPUVirtualAddress());
}
