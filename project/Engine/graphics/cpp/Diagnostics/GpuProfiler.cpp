/**
 * @file GpuProfiler.cpp
 * @brief タイムスタンプクエリでGPUの処理時間を測る（GpuProfiler）の実装
 */
#include "GpuProfiler.h"
#include "PipelineHelper.h"
#include "DirectXCommon.h"
#include "EngineAssert.h"
#include "FrameProfiler.h"
#ifdef USE_IMGUI
#include <imgui.h>
#endif

namespace {

// GPU時間の単位と表示レイアウト
constexpr unsigned int kMillisecondsPerSecond = 1000u;
constexpr int kProfilerWindowWidth = 230;
constexpr int kProfilerWindowHeight = 130;
constexpr int kProfilerWindowX = 10;
constexpr int kProfilerWindowY = 580;

}

namespace engine::graphics {
using engine::DirectXCommon;

/** @brief シングルトンインスタンスを取得する */
GpuProfiler* GpuProfiler::GetInstance()
{
    static GpuProfiler inst;
    return &inst;
}

/** @brief タイムスタンプクエリヒープとリードバックバッファを生成する */
void GpuProfiler::Initialize(DirectXCommon* dxCommon)
{
    dxCommon_ = dxCommon;
    auto* device = dxCommon->GetDevice();

    // GPUの時刻の刻み（1秒あたりの数）を取り、ミリ秒への変換に使う
    UINT64 freq = 1;
    dxCommon->GetCommandQueue()->GetTimestampFrequency(&freq);
    gpuFreq_ = freq;

    // 区間の始まりと終わりの時刻を書き込むクエリヒープ
    D3D12_QUERY_HEAP_DESC qhDesc = { };
    qhDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    qhDesc.Count = static_cast<UINT>(kTimestampCount);
    qhDesc.NodeMask = 0;
    HRESULT hr = device->CreateQueryHeap(&qhDesc, IID_PPV_ARGS(&queryHeap_));
    ENGINE_ASSERT(SUCCEEDED(hr));

    // GPUが書いた時刻をCPUから読むための読み戻しバッファ
    D3D12_HEAP_PROPERTIES hp = { };
    hp.Type = D3D12_HEAP_TYPE_READBACK;
    const D3D12_RESOURCE_DESC rb = PipelineHelper::MakeBufferDesc(static_cast<UINT64>(kTimestampCount) * sizeof(UINT64));
    hr = device->CreateCommittedResource(
        &hp, D3D12_HEAP_FLAG_NONE, &rb,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        IID_PPV_ARGS(&readbackBuf_));
    ENGINE_ASSERT(SUCCEEDED(hr));

    results_.fill(0.0f);
}

/** @brief クエリヒープとリードバックバッファを解放する */
void GpuProfiler::Finalize()
{
    resolved_ = false;
    readbackBuf_.Reset();
    queryHeap_.Reset();
    dxCommon_ = nullptr;
}

/** @brief 指定スコープの計測開始タイムスタンプを打つ */
void GpuProfiler::BeginScope(Scope s, ID3D12GraphicsCommandList* cmd)
{
    cmd->EndQuery(queryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, static_cast<int>(s) * 2);
}

/** @brief 指定スコープの計測終了タイムスタンプを打つ */
void GpuProfiler::EndScope(Scope s, ID3D12GraphicsCommandList* cmd)
{
    cmd->EndQuery(queryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, static_cast<int>(s) * 2 + 1);
}

/** @brief 積んだ全タイムスタンプクエリの結果をリードバックバッファへ解決する */
void GpuProfiler::Resolve(ID3D12GraphicsCommandList* cmd)
{
    cmd->ResolveQueryData(
        queryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
        0, static_cast<UINT>(kTimestampCount),
        readbackBuf_.Get(), 0);
    resolved_ = true;
}

/** @brief リードバックバッファをCPU側にマップし、各パスの経過時間(ms)を results_ に反映する */
void GpuProfiler::ReadBack()
{
    if (!resolved_ || !readbackBuf_)
    {
        return;
    }
    D3D12_RANGE readRange = { 0, static_cast<SIZE_T>(kTimestampCount) * sizeof(UINT64) };
    UINT64* data = nullptr;
    if (FAILED(readbackBuf_->Map(0, &readRange, reinterpret_cast<void**>(&data))))
    {
        return;
    }

    for (int i = 0; i < static_cast<int>(Count); ++i)
    {
        UINT64 begin = data[static_cast<size_t>(i) * 2];
        UINT64 end = data[static_cast<size_t>(i) * 2 + 1];
        results_[static_cast<size_t>(i)] = (end >= begin)
            ? static_cast<float>((end - begin) * kMillisecondsPerSecond) / static_cast<float>(gpuFreq_)
            : 0.0f;
    }

    D3D12_RANGE writeRange = { 0, 0 };
    readbackBuf_->Unmap(0, &writeRange);
}

/** @brief CPU/GPUのフレーム時間とパス別コストを表示するImGuiウィンドウを描画する */
void GpuProfiler::DrawImGui()
{
#ifdef USE_IMGUI
    auto* cpu = FrameProfiler::GetInstance();
    ImGui::SetNextWindowSize(ImVec2(kProfilerWindowWidth, kProfilerWindowHeight), ImGuiCond_Once);
    ImGui::SetNextWindowPos(ImVec2(kProfilerWindowX, kProfilerWindowY), ImGuiCond_Once);
    if (ImGui::Begin("Profiler"))
    {
        ImGui::Text("CPU  %.2f ms  (%.1f FPS)", cpu->GetMs(), cpu->GetFPS());
        ImGui::Separator();
        ImGui::Text("GPU Pass Breakdown:");
        static const char* kNames[static_cast<int>(Count)] = { "Shadow ", "SSAO   ", "Main3D " };
        float total = 0.0f;
        for (int i = 0; i < static_cast<int>(Count); ++i)
        {
            ImGui::Text("  %s  %.3f ms", kNames[i], results_[i]);
            total += results_[i];
        }
        ImGui::Text("  Total    %.3f ms", total);
    }
    ImGui::End();
#endif
}

} // namespace engine::graphics
