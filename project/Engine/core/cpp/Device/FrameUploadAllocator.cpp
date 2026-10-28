/**
 * @file FrameUploadAllocator.cpp
 * @brief フレーム別のアップロード領域（FrameUploadAllocator）の実装
 */
#include "FrameUploadAllocator.h"
#include "DirectXCommon.h"
#include "EngineAssert.h"
#include <algorithm>
#include <cstring>
namespace engine {

/** @brief 使うデバイスとフレーム数を設定する（ページは最初の確保時に作る） */
void FrameUploadAllocator::Initialize(DirectXCommon* dxCommon, UINT frameCount) {
    dxCommon_ = dxCommon;
    frames_.resize(frameCount);
}

/** @brief これから記録するフレームの領域を、先頭から使い直す */
void FrameUploadAllocator::BeginFrame(UINT frameIndex) {
    frameIndex_ = frameIndex;
    // ページは残したまま、使用量だけ戻す
    for (Page& page : frames_.at(frameIndex_)) {
        page.used = 0;
    }
}

/** @brief 指定サイズが入るページを今のフレームの中から探し、無ければ追加する */
FrameUploadAllocator::Page& FrameUploadAllocator::FindOrAddPage(size_t alignedSize) {
    std::vector<Page>& pages = frames_.at(frameIndex_);
    // 空きが足りるページがあればそれを使う
    auto found = std::find_if(pages.begin(), pages.end(), [alignedSize](const Page& page) {
        return alignedSize <= page.capacity - page.used;
    });
    if (found != pages.end()) {
        return *found;
    }

    // 足りなければ新しいページを作り、Map したまま保持する
    Page added;
    added.capacity = (std::max)(kPageBytes, alignedSize);
    added.resource = dxCommon_->CreateBufferResource(added.capacity);
    const HRESULT hr = added.resource->Map(0, nullptr, reinterpret_cast<void**>(&added.data));
    ENGINE_ASSERT(SUCCEEDED(hr));
    pages.push_back(std::move(added));
    return pages.back();
}

/** @brief データを今のフレームの領域へコピーし、その GPU アドレスを返す */
D3D12_GPU_VIRTUAL_ADDRESS FrameUploadAllocator::Allocate(const void* data, size_t size) {
    // 定数バッファビューの先頭は 256 バイト境界に揃える必要がある
    constexpr size_t kAlignment = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
    const size_t alignedSize = (size + kAlignment - 1) & ~(kAlignment - 1);

    // 空いている位置へコピーし、使用量を進める
    Page& page = FindOrAddPage(alignedSize);
    const D3D12_GPU_VIRTUAL_ADDRESS address = page.resource->GetGPUVirtualAddress() + page.used;
    std::memcpy(page.data + page.used, data, size);
    page.used += alignedSize;
    return address;
}

} // namespace engine
