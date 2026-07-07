/**
 * @file GpuMarker.h
 * @brief PIX/RenderDoc等のGPUキャプチャツール向けに、コマンドリストへ区間マーカーを打つファイル
 * @note WinPixEventRuntime等の外部ライブラリを使わず、D3D12標準のBeginEvent/EndEventのみで実装する
 */
#pragma once
#include <cstring>
#include <d3d12.h>
namespace engine::graphics {

/**
 * @brief コンストラクタでBeginEvent、デストラクタでEndEventを呼ぶRAIIクラス
 * @note スコープを抜ければ（早期returnでも）必ずEndEventが呼ばれる
 */
class GpuMarker {
public:
    GpuMarker(ID3D12GraphicsCommandList* cmd, const char* label) : cmd_(cmd)
    {
        cmd_->BeginEvent(0, label, static_cast<UINT>(std::strlen(label) + 1));
    }
    ~GpuMarker() { cmd_->EndEvent(); }

    GpuMarker(const GpuMarker&) = delete;
    GpuMarker& operator=(const GpuMarker&) = delete;

private:
    ID3D12GraphicsCommandList* cmd_;
};

} // namespace engine::graphics
