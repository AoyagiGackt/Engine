#include "EditorGamePreview.h"
#include "DirectXCommon.h"
#include "EngineAssert.h"
#include "SrvManager.h"
#include "StageEditor.h"
using namespace engine;
using namespace engine::graphics;
namespace engine::game {
void EditorGamePreview::Prepare(DirectXCommon& graphics, StageEditor& editor)
{
#ifdef USE_IMGUI
    auto desc = graphics.GetCurrentBackBufferResource()->GetDesc();
    if (!texture_ || texture_->GetDesc().Width != desc.Width
        || texture_->GetDesc().Height != desc.Height) {
        graphics.WaitForGpu();
        texture_.Reset();
        desc.Flags = D3D12_RESOURCE_FLAG_NONE;
        D3D12_HEAP_PROPERTIES heap = {};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        const HRESULT result = graphics.GetDevice()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
            &desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&texture_));
        ENGINE_ASSERT(SUCCEEDED(result));
        if (!allocated_) {
            srv_ = SrvManager::GetInstance()->Allocate();
            allocated_ = true;
        }
        SrvManager::GetInstance()->CreateSRVforTexture2D(srv_, texture_.Get(),
            graphics.GetBackBufferFormat(), 1);
    }
    const auto view = graphics.GetCenteredClientViewport();
    editor.SetGamePreview(SrvManager::GetInstance()->GetGPUDescriptorHandle(srv_).ptr,
        view.TopLeftX / desc.Width, view.TopLeftY / desc.Height,
        (view.TopLeftX + view.Width) / desc.Width, (view.TopLeftY + view.Height) / desc.Height);
#endif
}
void EditorGamePreview::Capture(DirectXCommon& graphics, const StageEditor& editor)
{
#ifdef USE_IMGUI
    if (!editor.UsesGameWindow() || !texture_) { return; }
    auto* cmd = graphics.GetCommandList();
    auto* backBuffer = graphics.GetCurrentBackBufferResource();
    D3D12_RESOURCE_BARRIER before[] = {
        DirectXCommon::MakeTransitionBarrier(backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE),
        DirectXCommon::MakeTransitionBarrier(texture_.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST)
    };
    cmd->ResourceBarrier(2, before);
    cmd->CopyResource(texture_.Get(), backBuffer);
    D3D12_RESOURCE_BARRIER after[] = {
        DirectXCommon::MakeTransitionBarrier(backBuffer, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
        DirectXCommon::MakeTransitionBarrier(texture_.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
    };
    cmd->ResourceBarrier(2, after);
    // ゲーム映像は小窓にだけ表示し、編集画面の背景は落ち着いた色にする。
    constexpr float kEditorBackgroundColor[] = { 0.018f, 0.022f, 0.032f, 1.0f };
    const float* background = kEditorBackgroundColor;
    cmd->ClearRenderTargetView(graphics.GetCurrentBackBufferHandle(), background, 0, nullptr);
#endif
}
void EditorGamePreview::Finalize()
{
#ifdef USE_IMGUI
    texture_.Reset();
    if (allocated_) { SrvManager::GetInstance()->Free(srv_); allocated_ = false; }
#endif
}
}
