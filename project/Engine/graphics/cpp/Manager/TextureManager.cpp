#include "TextureManager.h"
#include "DirectXTex.h"
#include "SrvManager.h"
#include "StringUtility.h"
#include <vector>
#include "EngineAssert.h"
#include <algorithm>
#include <cctype>
using namespace engine;
using namespace engine::graphics;

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

    ID3D12Device* device = dxCommon_->GetDevice();
    HRESULT hr;

    // コピーキューを作成（グラフィックスキューと独立して動作する）
    D3D12_COMMAND_QUEUE_DESC copyQueueDesc{};
    copyQueueDesc.Type     = D3D12_COMMAND_LIST_TYPE_COPY;
    copyQueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    copyQueueDesc.Flags    = D3D12_COMMAND_QUEUE_FLAG_NONE;
    hr = device->CreateCommandQueue(&copyQueueDesc, IID_PPV_ARGS(&copyQueue_));
    ENGINE_ASSERT(SUCCEEDED(hr));

    // コピーキュー用アロケータ・コマンドリストを作成（再利用するため永続化）
    hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY, IID_PPV_ARGS(&copyAllocator_));
    ENGINE_ASSERT(SUCCEEDED(hr));
    hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COPY,
        copyAllocator_.Get(), nullptr, IID_PPV_ARGS(&copyCmdList_));
    ENGINE_ASSERT(SUCCEEDED(hr));
    // LoadTexture() がいつでも記録できるよう Open 状態のままにしておく

    // コピーフェンスとイベントを作成（再利用するため永続化）
    hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&copyFence_));
    ENGINE_ASSERT(SUCCEEDED(hr));
    copyFenceEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    ENGINE_ASSERT(copyFenceEvent_ != nullptr);

    // バリア遷移用（グラフィックスキュー）アロケータ・コマンドリストを作成（再利用するため永続化）
    hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&transAllocator_));
    ENGINE_ASSERT(SUCCEEDED(hr));
    hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        transAllocator_.Get(), nullptr, IID_PPV_ARGS(&transCmdList_));
    ENGINE_ASSERT(SUCCEEDED(hr));
    transCmdList_->Close(); // FlushUploads() で Reset して使うため最初は閉じておく
}

void TextureManager::LoadTexture(const std::string& filePath)
{
    // 読み込み済みなら早期リターン
    if (textureDatas_.contains(filePath)) {
        return;
    }

    ENGINE_ASSERT(dxCommon_);
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
        ENGINE_ASSERT(SUCCEEDED(hr));
    } else {
        // WICファイル（PNG/JPG等）はミップマップを生成する
        DirectX::ScratchImage image {};
        // GIFはパレット形式のためFORCE_SRGBが使えない
        DirectX::WIC_FLAGS wicFlags = (ext == "gif") ? DirectX::WIC_FLAGS_DEFAULT_SRGB : DirectX::WIC_FLAGS_FORCE_SRGB;
        hr = DirectX::LoadFromWICFile(filePathW.c_str(), wicFlags, nullptr, image);
        ENGINE_ASSERT(SUCCEEDED(hr));

        // ミップマップ生成失敗した場合（非対応フォーマット・奇数サイズ等）は元画像をミップレベル1として使用する
        hr = DirectX::GenerateMipMaps(image.GetImages(), image.GetImageCount(), image.GetMetadata(), DirectX::TEX_FILTER_BOX, 0, finalImage);
        if (FAILED(hr)) {
            hr = finalImage.InitializeFromImage(*image.GetImages());
            ENGINE_ASSERT(SUCCEEDED(hr));
        }
    }

    // テクスチャリソース記述子
    const DirectX::TexMetadata& metadata = finalImage.GetMetadata();
    D3D12_RESOURCE_DESC resourceDesc {};
    resourceDesc.Width            = UINT(metadata.width);
    resourceDesc.Height           = UINT(metadata.height);
    resourceDesc.MipLevels        = UINT16(metadata.mipLevels);
    resourceDesc.DepthOrArraySize = UINT16(metadata.arraySize);
    resourceDesc.Format           = metadata.format;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Dimension        = D3D12_RESOURCE_DIMENSION(metadata.dimension);

    // VRAM（DEFAULT heap）にテクスチャリソースを作成
    // COMMON 状態で作成することで、コピーキューが COPY_DEST へ自動昇格し、
    // ExecuteCommandLists 後に COMMON へ自動復帰する（implicit promotion / decay）
    D3D12_HEAP_PROPERTIES defaultHeap {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

    ComPtr<ID3D12Resource> resource;
    hr = device->CreateCommittedResource(
        &defaultHeap,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&resource));
    ENGINE_ASSERT(SUCCEEDED(hr));

    // GetCopyableFootprints でサブリソースごとのレイアウトと合計サイズを取得
    const UINT subresourceCount = UINT(metadata.mipLevels * metadata.arraySize);
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(subresourceCount);
    std::vector<UINT> numRows(subresourceCount);
    std::vector<UINT64> rowSizes(subresourceCount);
    UINT64 totalSize = 0;
    device->GetCopyableFootprints(&resourceDesc, 0, subresourceCount, 0,
        footprints.data(), numRows.data(), rowSizes.data(), &totalSize);

    D3D12_HEAP_PROPERTIES uploadHeap {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC uploadDesc {};
    uploadDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    uploadDesc.Width            = totalSize;
    uploadDesc.Height           = 1;
    uploadDesc.DepthOrArraySize = 1;
    uploadDesc.MipLevels        = 1;
    uploadDesc.SampleDesc.Count = 1;
    uploadDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ComPtr<ID3D12Resource> uploadBuffer;
    hr = device->CreateCommittedResource(
        &uploadHeap,
        D3D12_HEAP_FLAG_NONE,
        &uploadDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&uploadBuffer));
    ENGINE_ASSERT(SUCCEEDED(hr));

    // アップロードバッファをマップして全サブリソースを1行ずつ書き込む
    BYTE* pMappedData = nullptr;
    hr = uploadBuffer->Map(0, nullptr, reinterpret_cast<void**>(&pMappedData));
    ENGINE_ASSERT(SUCCEEDED(hr));

    for (size_t arrayIndex = 0; arrayIndex < metadata.arraySize; ++arrayIndex) {
        for (size_t mipLevel = 0; mipLevel < metadata.mipLevels; ++mipLevel) {
            const UINT subresource = UINT(mipLevel + arrayIndex * metadata.mipLevels);
            const DirectX::Image* img = finalImage.GetImage(mipLevel, arrayIndex, 0);
            const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& fp = footprints[subresource];
            for (UINT row = 0; row < numRows[subresource]; ++row) {
                memcpy(
                    pMappedData + fp.Offset + static_cast<UINT64>(row) * fp.Footprint.RowPitch,
                    img->pixels + static_cast<UINT64>(row) * img->rowPitch,
                    rowSizes[subresource]);
            }
        }
    }

    uploadBuffer->Unmap(0, nullptr);

    // 永続コピーコマンドリストにサブリソースごとの転送コマンドを記録する
    // （実行は FlushUploads() で一括して行う）
    for (UINT subresource = 0; subresource < subresourceCount; ++subresource) {
        D3D12_TEXTURE_COPY_LOCATION dst {};
        dst.pResource        = resource.Get();
        dst.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = subresource;

        D3D12_TEXTURE_COPY_LOCATION src {};
        src.pResource       = uploadBuffer.Get();
        src.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint = footprints[subresource];

        copyCmdList_->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    }

    // GPU完了まで uploadBuffer と resource を保持する（FlushUploads() で解放）
    pendingUploadBuffers_.push_back(uploadBuffer);
    pendingResources_.push_back(resource);
    hasPendingCopies_ = true;

    // SRV作成はCPUのみの操作なので GPU完了前でも安全
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
    ENGINE_ASSERT(textureDatas_.contains(filePath));
    return textureDatas_[filePath].srvIndex;
}

D3D12_GPU_DESCRIPTOR_HANDLE TextureManager::GetSrvHandleGPU(const std::string& filePath)
{
    ENGINE_ASSERT(textureDatas_.contains(filePath));
    return SrvManager::GetInstance()->GetGPUDescriptorHandle(textureDatas_[filePath].srvIndex);
}

const DirectX::TexMetadata& TextureManager::GetMetaData(const std::string& filePath)
{
    ENGINE_ASSERT(textureDatas_.contains(filePath));
    return textureDatas_[filePath].metadata;
}

void TextureManager::FlushUploads()
{
    if (!hasPendingCopies_) {
        return;
    }
    hasPendingCopies_ = false;

    HRESULT hr;

    // -------------------------------------------------------
    // 1. コピーコマンドリストを閉じてコピーキューで一括実行
    // -------------------------------------------------------
    hr = copyCmdList_->Close();
    ENGINE_ASSERT(SUCCEEDED(hr));
    ID3D12CommandList* copyCmds[] = { copyCmdList_.Get() };
    copyQueue_->ExecuteCommandLists(1, copyCmds);

    // -------------------------------------------------------
    // 2. コピーキュー完了を待機（全テクスチャを通じて1回のみ）
    // -------------------------------------------------------
    ++copyFenceValue_;
    hr = copyQueue_->Signal(copyFence_.Get(), copyFenceValue_);
    ENGINE_ASSERT(SUCCEEDED(hr));
    if (copyFence_->GetCompletedValue() < copyFenceValue_) {
        copyFence_->SetEventOnCompletion(copyFenceValue_, copyFenceEvent_);
        WaitForSingleObject(copyFenceEvent_, INFINITE);
    }
    // コピー完了後にアップロードバッファを解放する
    pendingUploadBuffers_.clear();

    // -------------------------------------------------------
    // 3. COMMON → PIXEL_SHADER_RESOURCE バリアをグラフィックスキューで一括処理
    //    （コピーキューの ExecuteCommandLists 後、リソースは COMMON 状態に復帰している）
    // -------------------------------------------------------
    hr = transAllocator_->Reset();
    ENGINE_ASSERT(SUCCEEDED(hr));
    hr = transCmdList_->Reset(transAllocator_.Get(), nullptr);
    ENGINE_ASSERT(SUCCEEDED(hr));

    std::vector<D3D12_RESOURCE_BARRIER> barriers;
    barriers.reserve(pendingResources_.size());
    for (auto& res : pendingResources_) {
        D3D12_RESOURCE_BARRIER b{};
        b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource   = res.Get();
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
        b.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barriers.push_back(b);
    }
    if (!barriers.empty()) {
        transCmdList_->ResourceBarrier(UINT(barriers.size()), barriers.data());
    }
    hr = transCmdList_->Close();
    ENGINE_ASSERT(SUCCEEDED(hr));
    ID3D12CommandList* transCmds[] = { transCmdList_.Get() };
    dxCommon_->GetCommandQueue()->ExecuteCommandLists(1, transCmds);

    // グラフィックスキューの完了待機（DirectXCommon の永続フェンスを再利用）
    auto* gfxFence = dxCommon_->GetFence();
    dxCommon_->IncrementFenceValue();
    UINT64 gfxFenceVal = dxCommon_->GetFenceValue();
    hr = dxCommon_->GetCommandQueue()->Signal(gfxFence, gfxFenceVal);
    ENGINE_ASSERT(SUCCEEDED(hr));
    if (gfxFence->GetCompletedValue() < gfxFenceVal) {
        gfxFence->SetEventOnCompletion(gfxFenceVal, dxCommon_->GetFenceEvent());
        WaitForSingleObject(dxCommon_->GetFenceEvent(), INFINITE);
    }
    pendingResources_.clear();

    // -------------------------------------------------------
    // 4. コピーアロケータとコマンドリストをリセットして次のバッチに備える
    // -------------------------------------------------------
    hr = copyAllocator_->Reset();
    ENGINE_ASSERT(SUCCEEDED(hr));
    hr = copyCmdList_->Reset(copyAllocator_.Get(), nullptr);
    ENGINE_ASSERT(SUCCEEDED(hr));
}

void TextureManager::LoadFromRawRGBA8(const std::string& name,
    const uint8_t* rgbaData, uint32_t width, uint32_t height)
{
    if (textureDatas_.contains(name)) { return; }

    ID3D12Device* device = dxCommon_->GetDevice();
    HRESULT hr;

    // テクスチャリソース（DEFAULT heap）
    D3D12_RESOURCE_DESC resourceDesc{};
    resourceDesc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resourceDesc.Width            = width;
    resourceDesc.Height           = height;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels        = 1;
    resourceDesc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    D3D12_HEAP_PROPERTIES defaultHeap{};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

    ComPtr<ID3D12Resource> resource;
    hr = device->CreateCommittedResource(
        &defaultHeap, D3D12_HEAP_FLAG_NONE,
        &resourceDesc, D3D12_RESOURCE_STATE_COMMON,
        nullptr, IID_PPV_ARGS(&resource));
    ENGINE_ASSERT(SUCCEEDED(hr));

    // アップロードバッファ（行ピッチアライメントを考慮）
    const UINT64 rowPitch =
        ((static_cast<UINT64>(width) * 4 + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) /
          D3D12_TEXTURE_DATA_PITCH_ALIGNMENT) *
         D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
    const UINT64 uploadSize = rowPitch * height;

    D3D12_RESOURCE_DESC uploadDesc{};
    uploadDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    uploadDesc.Width            = uploadSize;
    uploadDesc.Height           = 1;
    uploadDesc.DepthOrArraySize = 1;
    uploadDesc.MipLevels        = 1;
    uploadDesc.SampleDesc.Count = 1;
    uploadDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    uploadDesc.Format           = DXGI_FORMAT_UNKNOWN;

    D3D12_HEAP_PROPERTIES uploadHeap{};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

    ComPtr<ID3D12Resource> uploadBuffer;
    hr = device->CreateCommittedResource(
        &uploadHeap, D3D12_HEAP_FLAG_NONE,
        &uploadDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr, IID_PPV_ARGS(&uploadBuffer));
    ENGINE_ASSERT(SUCCEEDED(hr));

    // ピクセルデータを行ごとにコピー（RowPitch のパディングに対応）
    BYTE* pMapped = nullptr;
    hr = uploadBuffer->Map(0, nullptr, reinterpret_cast<void**>(&pMapped));
    ENGINE_ASSERT(SUCCEEDED(hr));
    for (uint32_t row = 0; row < height; ++row) {
        memcpy(pMapped + row * rowPitch,
               rgbaData + static_cast<UINT64>(row) * width * 4,
               static_cast<size_t>(width) * 4);
    }
    uploadBuffer->Unmap(0, nullptr);

    // コピーコマンドを積む
    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource        = resource.Get();
    dst.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource                            = uploadBuffer.Get();
    src.Type                                 = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint.Offset               = 0;
    src.PlacedFootprint.Footprint.Format     = DXGI_FORMAT_R8G8B8A8_UNORM;
    src.PlacedFootprint.Footprint.Width      = width;
    src.PlacedFootprint.Footprint.Height     = height;
    src.PlacedFootprint.Footprint.Depth      = 1;
    src.PlacedFootprint.Footprint.RowPitch   = static_cast<UINT>(rowPitch);

    copyCmdList_->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    pendingUploadBuffers_.push_back(uploadBuffer);
    pendingResources_.push_back(resource);
    hasPendingCopies_ = true;

    // SRV 作成
    uint32_t srvIndex = SrvManager::GetInstance()->Allocate();
    SrvManager::GetInstance()->CreateSRVforTexture2D(
        srvIndex, resource.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, 1);

    TextureData& data    = textureDatas_[name];
    data.resource        = resource;
    data.srvIndex        = srvIndex;
    data.metadata        = {};
    data.metadata.width  = width;
    data.metadata.height = height;
    data.metadata.depth  = 1;
    data.metadata.arraySize = 1;
    data.metadata.mipLevels = 1;
    data.metadata.format    = DXGI_FORMAT_R8G8B8A8_UNORM;
    data.metadata.dimension = DirectX::TEX_DIMENSION_TEXTURE2D;
}

void TextureManager::Finalize()
{
    // 保留中の転送があれば完了させてからリソースを解放する
    FlushUploads();

    if (copyFenceEvent_) {
        CloseHandle(copyFenceEvent_);
        copyFenceEvent_ = nullptr;
    }
    textureDatas_.clear();
}