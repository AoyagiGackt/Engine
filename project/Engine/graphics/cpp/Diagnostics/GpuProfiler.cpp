/**
 * @file GpuProfiler.cpp
 * @brief GpuProfilerが担当する処理を実装するファイル
 */
#include "GpuProfiler.h"
#include "FrameProfiler.h"
#include "DirectXCommon.h"
#include "EngineAssert.h"
#ifdef USE_IMGUI
#include <imgui.h>
#endif

namespace engine::graphics {
using engine::DirectXCommon;

GpuProfiler* GpuProfiler::GetInstance()
{
    static GpuProfiler inst;
    return &inst;
}

void GpuProfiler::Initialize(DirectXCommon* dxCommon)
{
    dxCommon_ = dxCommon;
    auto* device = dxCommon->GetDevice();

    UINT64 freq = 1;
    dxCommon->GetCommandQueue()->GetTimestampFrequency(&freq);
    gpuFreq_ = freq;

    D3D12_QUERY_HEAP_DESC qhDesc = { };
    qhDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    qhDesc.Count = static_cast<UINT>(kTimestampCount);
    qhDesc.NodeMask = 0;
    HRESULT hr = device->CreateQueryHeap(&qhDesc, IID_PPV_ARGS(&queryHeap_));
    ENGINE_ASSERT(SUCCEEDED(hr));

    D3D12_HEAP_PROPERTIES hp = { };
    hp.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC rb = { };
    rb.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rb.Width = static_cast<UINT64>(kTimestampCount) * sizeof(UINT64);
    rb.Height = 1;
    rb.DepthOrArraySize = 1;
    rb.MipLevels = 1;
    rb.SampleDesc.Count = 1;
    rb.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    hr = device->CreateCommittedResource(
        &hp, D3D12_HEAP_FLAG_NONE, &rb,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        IID_PPV_ARGS(&readbackBuf_));
    ENGINE_ASSERT(SUCCEEDED(hr));

    results_.fill(0.0f);
}

void GpuProfiler::Finalize()
{
    resolved_ = false;
    readbackBuf_.Reset();
    queryHeap_.Reset();
    dxCommon_ = nullptr;
}

void GpuProfiler::BeginScope(Scope s, ID3D12GraphicsCommandList* cmd)
{
    cmd->EndQuery(queryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, static_cast<int>(s) * 2);
}

void GpuProfiler::EndScope(Scope s, ID3D12GraphicsCommandList* cmd)
{
    cmd->EndQuery(queryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, static_cast<int>(s) * 2 + 1);
}

void GpuProfiler::Resolve(ID3D12GraphicsCommandList* cmd)
{
    cmd->ResolveQueryData(
        queryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
        0, static_cast<UINT>(kTimestampCount),
        readbackBuf_.Get(), 0);
    resolved_ = true;
}

void GpuProfiler::ReadBack()
{
    if (!resolved_ || !readbackBuf_) {
        return;
    }
    D3D12_RANGE readRange = { 0, static_cast<SIZE_T>(kTimestampCount) * sizeof(UINT64) };
    UINT64* data = nullptr;
    if (FAILED(readbackBuf_->Map(0, &readRange, reinterpret_cast<void**>(&data)))) {
        return;
    }

    for (int i = 0; i < static_cast<int>(Count); ++i) {
        UINT64 begin = data[static_cast<size_t>(i) * 2];
        UINT64 end = data[static_cast<size_t>(i) * 2 + 1];
        results_[static_cast<size_t>(i)] = (end >= begin)
            ? static_cast<float>((end - begin) * 1000u) / static_cast<float>(gpuFreq_)
            : 0.0f;
    }

    D3D12_RANGE writeRange = { 0, 0 };
    readbackBuf_->Unmap(0, &writeRange);
}

void GpuProfiler::DrawImGui()
{
#ifdef USE_IMGUI
    auto* cpu = FrameProfiler::GetInstance();
    ImGui::SetNextWindowSize(ImVec2(230, 130), ImGuiCond_Once);
    ImGui::SetNextWindowPos(ImVec2(10, 580), ImGuiCond_Once);
    if (ImGui::Begin("Profiler")) {
        ImGui::Text("CPU  %.2f ms  (%.1f FPS)", cpu->GetMs(), cpu->GetFPS());
        ImGui::Separator();
        ImGui::Text("GPU Pass Breakdown:");
        static const char* kNames[static_cast<int>(Count)] = { "Shadow ", "SSAO   ", "Main3D " };
        float total = 0.0f;
        for (int i = 0; i < static_cast<int>(Count); ++i) {
            ImGui::Text("  %s  %.3f ms", kNames[i], results_[i]);
            total += results_[i];
        }
        ImGui::Text("  Total    %.3f ms", total);
    }
    ImGui::End();
#endif
}

} // namespace engine::graphics
