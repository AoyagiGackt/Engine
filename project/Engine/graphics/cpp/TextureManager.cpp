#include "TextureManager.h"
#include "DirectXTex.h"
#include "SrvManager.h"
#include "StringUtlity.h"
#include <vector>
#include <cassert>
#include <algorithm>
#include <cctype>

using namespace Microsoft::WRL;

TextureManager* TextureManager::GetInstance()
{
    static TextureManager instance;
    return &instance;
}

void TextureManager::Initialize(DirectXCommon* dxCommon)
{
    dxCommon_ = dxCommon;
    textureDatas_.clear();
}

void TextureManager::LoadTexture(const std::string& filePath)
{
    // 読み込み済みなら早期リターン
    if (textureDatas_.contains(filePath)) {
        return;
    }

    assert(dxCommon_);
    ID3D12Device* device = dxCommon_->GetDevice();

    // 画像ファイルを読み込む
    std::wstring filePathW = StringUtility::ConvertString(filePath);

    // 拡張子を取得して小文字化
    std::string ext = filePath.substr(filePath.find_last_of('.') + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });

    // 最終的な画像データを格納するScratchImage
    DirectX::ScratchImage finalImage {};
    HRESULT hr;

    if (ext == "dds") {
        // DDSファイルはミップマップが埋め込み済みのためそのまま読み込む
        hr = DirectX::LoadFromDDSFile(filePathW.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, finalImage);
        assert(SUCCEEDED(hr));
    } else {
        // WICファイル（PNG/JPG等）はミップマップを生成する
        DirectX::ScratchImage image {};
        // GIFはパレット形式のためFORCE_SRGBが使えない
        DirectX::WIC_FLAGS wicFlags = (ext == "gif") ? DirectX::WIC_FLAGS_DEFAULT_SRGB : DirectX::WIC_FLAGS_FORCE_SRGB;
        hr = DirectX::LoadFromWICFile(filePathW.c_str(), wicFlags, nullptr, image);
        assert(SUCCEEDED(hr));

        // ミップマップ生成。失敗した場合（非対応フォーマット・奇数サイズ等）は元画像をミップレベル1として使用する
        hr = DirectX::GenerateMipMaps(image.GetImages(), image.GetImageCount(), image.GetMetadata(), DirectX::TEX_FILTER_BOX, 0, finalImage);
        if (FAILED(hr)) {
            hr = finalImage.InitializeFromImage(*image.GetImages());
            assert(SUCCEEDED(hr));
        }
    }

    // リソース作成
    const DirectX::TexMetadata& metadata = finalImage.GetMetadata();
    D3D12_RESOURCE_DESC resourceDesc {};
    resourceDesc.Width = UINT(metadata.width);
    resourceDesc.Height = UINT(metadata.height);
    resourceDesc.MipLevels = UINT16(metadata.mipLevels);
    resourceDesc.DepthOrArraySize = UINT16(metadata.arraySize);
    resourceDesc.Format = metadata.format;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION(metadata.dimension);

    D3D12_HEAP_PROPERTIES heapProperties {};
    heapProperties.Type = D3D12_HEAP_TYPE_CUSTOM;
    heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
    heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;

    ComPtr<ID3D12Resource> resource;
    hr = device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&resource));
    assert(SUCCEEDED(hr));

    // データ転送（2Dテクスチャ・キューブマップ両対応）
    for (size_t arrayIndex = 0; arrayIndex < metadata.arraySize; ++arrayIndex) {
        for (size_t mipLevel = 0; mipLevel < metadata.mipLevels; ++mipLevel) {
            const DirectX::Image* img = finalImage.GetImage(mipLevel, arrayIndex, 0);
            UINT subresource = D3D12CalcSubresource(
                UINT(mipLevel), UINT(arrayIndex), 0,
                UINT(metadata.mipLevels), UINT(metadata.arraySize));
            resource->WriteToSubresource(
                subresource,
                nullptr,
                img->pixels,
                UINT(img->rowPitch),
                UINT(img->slicePitch));
        }
    }

    // SRV作成（キューブマップか2Dテクスチャかで種別を分ける）
    uint32_t srvIndex = SrvManager::GetInstance()->Allocate();

    if (metadata.IsCubemap()) {
        SrvManager::GetInstance()->CreateSRVforTextureCube(
            srvIndex, resource.Get(), metadata.format, UINT(metadata.mipLevels));
    } else {
        SrvManager::GetInstance()->CreateSRVforTexture2D(
            srvIndex, resource.Get(), metadata.format, UINT(metadata.mipLevels));
    }

    // データ保存
    TextureData& data = textureDatas_[filePath];
    data.resource = resource;
    data.srvIndex = srvIndex; // インデックスを保存
    data.metadata = metadata;
}

uint32_t TextureManager::GetTextureIndexByFilePath(const std::string& filePath)
{
    assert(textureDatas_.contains(filePath));
    return textureDatas_[filePath].srvIndex;
}

D3D12_GPU_DESCRIPTOR_HANDLE TextureManager::GetSrvHandleGPU(const std::string& filePath)
{
    assert(textureDatas_.contains(filePath));
    return SrvManager::GetInstance()->GetGPUDescriptorHandle(textureDatas_[filePath].srvIndex);
}

const DirectX::TexMetadata& TextureManager::GetMetaData(const std::string& filePath)
{
    assert(textureDatas_.contains(filePath));
    return textureDatas_[filePath].metadata;
}

void TextureManager::Finalize()
{
    textureDatas_.clear();
}