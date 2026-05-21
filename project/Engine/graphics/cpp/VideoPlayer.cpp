#include "VideoPlayer.h"
#include "Logger.h"
#include "SrvManager.h"
#include "StringUtlity.h"
#include <cassert>

using namespace Microsoft::WRL;

VideoPlayer::~VideoPlayer()
{
    Finalize();
}

void VideoPlayer::Initialize(DirectXCommon* dxCommon, SpriteCommon* spriteCommon, const std::string& filePath)
{
    dxCommon_ = dxCommon;
    spriteCommon_ = spriteCommon;

    HRESULT hr;

    // Media Foundation の初期化
    hr = MFStartup(MF_VERSION);
    assert(SUCCEEDED(hr));

    ComPtr<IMFAttributes> attributes;
    hr = MFCreateAttributes(&attributes, 1);
    assert(SUCCEEDED(hr));
    hr = attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
    assert(SUCCEEDED(hr));

    std::wstring wFilePath = StringUtility::ConvertString(filePath);

    hr = MFCreateSourceReaderFromURL(wFilePath.c_str(), attributes.Get(), &pSourceReader_);
    
    if (FAILED(hr)) {
        Logger::Log("Failed to load video: " + filePath);
        assert(false);
    }

    // デコードフォーマットをRGB32に設定
    ComPtr<IMFMediaType> pMediaType;
    MFCreateMediaType(&pMediaType);
    pMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    pMediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    hr = pSourceReader_->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, pMediaType.Get());
    assert(SUCCEEDED(hr));

    // 動画の解像度を取得
    ComPtr<IMFMediaType> pOutputMediaType;
    hr = pSourceReader_->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &pOutputMediaType);
    assert(SUCCEEDED(hr));

    UINT32 width = 0, height = 0;
    MFGetAttributeSize(pOutputMediaType.Get(), MF_MT_FRAME_SIZE, &width, &height);
    videoWidth_ = width;
    videoHeight_ = height;

    // GPUで読む用テクスチャ (DEFAULTヒープ)
    D3D12_HEAP_PROPERTIES defaultHeapProps {};
    defaultHeapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC resDesc {};
    resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resDesc.Width = videoWidth_;
    resDesc.Height = videoHeight_;
    resDesc.DepthOrArraySize = 1;
    resDesc.MipLevels = 1;
    resDesc.Format = DXGI_FORMAT_B8G8R8X8_UNORM; // RGB32
    resDesc.SampleDesc.Count = 1;

    hr = dxCommon_->GetDevice()->CreateCommittedResource(
        &defaultHeapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
        IID_PPV_ARGS(&textureResource_));
    assert(SUCCEEDED(hr));

    // CPUから毎フレーム書き込む用バッファ (UPLOADヒープ)
    D3D12_HEAP_PROPERTIES uploadHeapProps {};
    uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    UINT64 uploadBufferSize = 0;
    dxCommon_->GetDevice()->GetCopyableFootprints(&resDesc, 0, 1, 0, nullptr, nullptr, nullptr, &uploadBufferSize);

    D3D12_RESOURCE_DESC uploadBufferDesc {};
    uploadBufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    uploadBufferDesc.Width = uploadBufferSize;
    uploadBufferDesc.Height = 1;
    uploadBufferDesc.DepthOrArraySize = 1;
    uploadBufferDesc.MipLevels = 1;
    uploadBufferDesc.SampleDesc.Count = 1;
    uploadBufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    hr = dxCommon_->GetDevice()->CreateCommittedResource(
        &uploadHeapProps, D3D12_HEAP_FLAG_NONE, &uploadBufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&uploadBuffer_));
    assert(SUCCEEDED(hr));

    // SRV（テクスチャのインデックス）を作成
    srvIndex_ = SrvManager::GetInstance()->Allocate();
    SrvManager::GetInstance()->CreateSRVforTexture2D(
        srvIndex_, textureResource_.Get(), resDesc.Format, 1);

    // スプライト生成
    sprite_ = std::make_unique<Sprite>();
    sprite_->Initialize(spriteCommon_, "Resources/uvChecker.png");

    // 始まる場所
    sprite_->SetPosition({ 960.0f, 15.0f });

    // ウィンドウサイズ指定
    sprite_->SetSize({ 300.0f, 450.0f });

    // スプライトに動画のテクスチャを使うよう指示する
    sprite_->SetExternalTexture(srvIndex_);
}

void VideoPlayer::Update()
{
    // 60FPS環境での簡易フレームスキップ
    timeCount_ += 1.0f / 60.0f;
    
    if (timeCount_ < frameDuration_) {
        if (sprite_) {
            sprite_->Update();
        }

        return;
    }

    timeCount_ -= frameDuration_;

    if (!pSourceReader_) {
        return;
    }

    DWORD flags = 0;
    LONGLONG timestamp = 0;
    ComPtr<IMFSample> pSample;

    HRESULT hr = pSourceReader_->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, &flags, &timestamp, &pSample);

    // ピクセルデータの取得とコピー
    if (SUCCEEDED(hr) && pSample) {
        ComPtr<IMFMediaBuffer> pBuffer;
        hr = pSample->ConvertToContiguousBuffer(&pBuffer);
        
        if (SUCCEEDED(hr)) {
            BYTE* pData = nullptr;
            DWORD maxLength = 0, currentLength = 0;
            pBuffer->Lock(&pData, &maxLength, &currentLength);

            BYTE* pMappedData = nullptr;
            uploadBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&pMappedData));

            D3D12_RESOURCE_DESC desc = textureResource_->GetDesc();
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
            UINT numRows;
            UINT64 rowSizeInBytes, totalBytes;
            dxCommon_->GetDevice()->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &numRows, &rowSizeInBytes, &totalBytes);

            LONG mfStride = videoWidth_ * 4;

            for (int y = 0; y < videoHeight_; ++y) {
                int srcY = y;
                memcpy(pMappedData + y * footprint.Footprint.RowPitch, pData + srcY * mfStride, videoWidth_ * 4);
            }

            uploadBuffer_->Unmap(0, nullptr);
            pBuffer->Unlock();

            isNewFrame_ = true;
            currentFootprint_ = footprint;

            // UPLOAD から DEFAULT へテクスチャの中身を転送
            ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();

            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = textureResource_.Get();
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
            commandList->ResourceBarrier(1, &barrier);

            D3D12_TEXTURE_COPY_LOCATION dst = {};
            dst.pResource = textureResource_.Get();
            dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            dst.SubresourceIndex = 0;

            D3D12_TEXTURE_COPY_LOCATION src = {};
            src.pResource = uploadBuffer_.Get();
            src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            src.PlacedFootprint = footprint;

            commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            commandList->ResourceBarrier(1, &barrier);
        }
    } else if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
        // 動画の最後まで行ったら最初からループ再生
        PROPVARIANT var;
        PropVariantInit(&var);
        var.vt = VT_I8;
        var.hVal.QuadPart = 0;
        pSourceReader_->SetCurrentPosition(GUID_NULL, var);
        PropVariantClear(&var);
    }

    if (sprite_) {
        sprite_->Update();
    }
}

void VideoPlayer::Draw()
{
    if (isNewFrame_) {
        ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();

        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = textureResource_.Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        commandList->ResourceBarrier(1, &barrier);

        D3D12_TEXTURE_COPY_LOCATION dst = {};
        dst.pResource = textureResource_.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = 0;

        D3D12_TEXTURE_COPY_LOCATION src = {};
        src.pResource = uploadBuffer_.Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint = currentFootprint_;

        commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        commandList->ResourceBarrier(1, &barrier);

        isNewFrame_ = false;
    }

    if (sprite_) {
        // スプライトを描画
        sprite_->Draw();
    }
}

void VideoPlayer::Finalize()
{
    pSourceReader_.Reset();
    textureResource_.Reset();
    uploadBuffer_.Reset();
    sprite_.reset();
}