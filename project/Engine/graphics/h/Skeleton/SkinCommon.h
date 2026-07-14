#pragma once
#include "BlendMode.h"
#include "DirectXCommon.h"
#include <wrl/client.h>
namespace engine::graphics {

// スキニング（ボーンアニメーション）専用の PSO / Root Signature を管理するクラス
// ModelCommon と同じスロット 0-5 を保ちつつ、スロット 6 (VS b1) にスキニングパレットを追加
class SkinCommon {
public:
    void Initialize(engine::DirectXCommon* dxCommon);
    void CommonDrawSettings(BlendMode blendMode = BlendMode::Alpha);

    engine::DirectXCommon* GetDxCommon() const { return dxCommon_; }

private:
    engine::DirectXCommon* dxCommon_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineStates_[static_cast<size_t>(BlendMode::Count)];
};

} // namespace engine::graphics
