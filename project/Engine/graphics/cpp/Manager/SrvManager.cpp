/**
 * @file SrvManager.cpp
 * @brief SrvManagerの描画資源とGPU処理の管理に関する具体的な処理を実装するファイル
 */
#include "SrvManager.h"
#include "EngineAssert.h"
#include <algorithm>
using namespace engine;
using namespace engine::graphics;

using namespace Microsoft::WRL;

SrvManager* SrvManager::GetInstance()
{
    static SrvManager instance;
    return &instance;
}

void SrvManager::Initialize(DirectXCommon* dxCommon)
{
    dxCommon_ = dxCommon;
    ID3D12Device* device = dxCommon_->GetDevice();

    // デスクリプタヒープの生成
    D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc = { };
    descriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    descriptorHeapDesc.NumDescriptors = kMaxSRVCount;
    descriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    HRESULT hr = device->CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(&descriptorHeap_));
    ENGINE_ASSERT(SUCCEEDED(hr));

    // デスクリプタ1個分のサイズを取得
    descriptorSize_ = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // インデックスをリセット
    useIndex_ = 0;
}

void SrvManager::PreDraw()
{
    ++frameCount_;

    // 解放から十分なフレーム数が経過した（＝GPUが使い終わったはずの）インデックスを
    // フリーリストへ移し、以後のAllocate()で再利用できるようにする
    pendingFree_.erase(
        std::remove_if(pendingFree_.begin(), pendingFree_.end(),
            [this](const std::pair<uint64_t, uint32_t>& p) {
                if (p.first + kFreeDelayFrames > frameCount_) {
                    return false;
                }
                freeList_.push_back(p.second);
                return true;
            }),
        pendingFree_.end());

    // 描画フレームの最初にヒープをセットする
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    ID3D12DescriptorHeap* descriptorHeaps[] = { descriptorHeap_.Get() };
    commandList->SetDescriptorHeaps(1, descriptorHeaps);
}

uint32_t SrvManager::Allocate()
{
    if (!freeList_.empty()) {
        uint32_t index = freeList_.back();
        freeList_.pop_back();
        return index;
    }

    ENGINE_ASSERT(useIndex_ < kMaxSRVCount);
    return useIndex_++;
}

void SrvManager::Free(uint32_t srvIndex)
{
    pendingFree_.push_back({ frameCount_, srvIndex });
}

void SrvManager::CreateSRVforTexture2D(uint32_t srvIndex, ID3D12Resource* pResource, DXGI_FORMAT Format, UINT MipLevels)
{
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = { };
    srvDesc.Format = Format;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = MipLevels;

    // 指定されたインデックスの場所にSRVを作成
    dxCommon_->GetDevice()->CreateShaderResourceView(pResource, &srvDesc, GetCPUDescriptorHandle(srvIndex));
}

void SrvManager::CreateSRVforTextureCube(uint32_t srvIndex, ID3D12Resource* pResource, DXGI_FORMAT Format, UINT MipLevels)
{
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = { };
    srvDesc.Format = Format;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
    srvDesc.TextureCube.MipLevels = MipLevels;
    srvDesc.TextureCube.MostDetailedMip = 0;
    srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;

    dxCommon_->GetDevice()->CreateShaderResourceView(pResource, &srvDesc, GetCPUDescriptorHandle(srvIndex));
}

void SrvManager::CreateSRVforDepthTexture(uint32_t srvIndex, ID3D12Resource* pResource)
{
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = { };
    srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    dxCommon_->GetDevice()->CreateShaderResourceView(pResource, &srvDesc, GetCPUDescriptorHandle(srvIndex));
}

void SrvManager::Finalize()
{
    descriptorHeap_.Reset();
}

D3D12_CPU_DESCRIPTOR_HANDLE SrvManager::GetCPUDescriptorHandle(uint32_t index)
{
    D3D12_CPU_DESCRIPTOR_HANDLE handleCPU = descriptorHeap_->GetCPUDescriptorHandleForHeapStart();
    handleCPU.ptr += (static_cast<unsigned long long>(index) * descriptorSize_);
    return handleCPU;
}

D3D12_GPU_DESCRIPTOR_HANDLE SrvManager::GetGPUDescriptorHandle(uint32_t index)
{
    D3D12_GPU_DESCRIPTOR_HANDLE handleGPU = descriptorHeap_->GetGPUDescriptorHandleForHeapStart();
    handleGPU.ptr += (static_cast<unsigned long long>(index) * descriptorSize_);
    return handleGPU;
}