/**
 * @file PipelineHelper.cpp
 * @brief PipelineHelperの描画資源とGPU処理の管理に関する具体的な処理を実装するファイル
 */
#include "PipelineHelper.h"
#include <iterator>

namespace engine::graphics::PipelineHelper {

D3D12_DESCRIPTOR_RANGE MakeSrvRange(UINT baseRegister)
{
    D3D12_DESCRIPTOR_RANGE range { };
    range.BaseShaderRegister = baseRegister;
    range.NumDescriptors = 1;
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    return range;
}

D3D12_ROOT_PARAMETER MakeCbvParam(UINT shaderRegister, D3D12_SHADER_VISIBILITY visibility)
{
    D3D12_ROOT_PARAMETER param { };
    param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    param.ShaderVisibility = visibility;
    param.Descriptor.ShaderRegister = shaderRegister;
    return param;
}

D3D12_ROOT_PARAMETER MakeSrvTableParam(const D3D12_DESCRIPTOR_RANGE* range, D3D12_SHADER_VISIBILITY visibility)
{
    D3D12_ROOT_PARAMETER param { };
    param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    param.ShaderVisibility = visibility;
    param.DescriptorTable.pDescriptorRanges = range;
    param.DescriptorTable.NumDescriptorRanges = 1;
    return param;
}

std::array<D3D12_STATIC_SAMPLER_DESC, 2> MakeDefaultSamplers(float s0MaxLod)
{
    std::array<D3D12_STATIC_SAMPLER_DESC, 2> samplers { };
    // s0: 通常テクスチャ用リニアフィルタ
    samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    samplers[0].MaxLOD = s0MaxLod;
    samplers[0].ShaderRegister = 0; // s0
    samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    // s1: シャドウマップ PCF 比較サンプラー
    samplers[1].Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    samplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samplers[1].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    samplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    samplers[1].MaxLOD = D3D12_FLOAT32_MAX;
    samplers[1].ShaderRegister = 1; // s1
    samplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    return samplers;
}

Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateRootSignature(
    ID3D12Device* device, const D3D12_ROOT_SIGNATURE_DESC& desc)
{
    Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
    if (FAILED(hr)) {
        return nullptr;
    }
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
    hr = device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(),
        IID_PPV_ARGS(&rootSignature));
    if (FAILED(hr)) {
        return nullptr;
    }
    return rootSignature;
}

D3D12_RENDER_TARGET_BLEND_DESC MakeBlendDesc(BlendMode mode)
{
    /** @brief ブレンドモード1つぶんの色の合成式 */
    struct BlendFactors {
        BOOL enable;
        D3D12_BLEND src;
        D3D12_BLEND dest;
        D3D12_BLEND_OP op;
    };
    // BlendMode の並び順
    static constexpr BlendFactors kBlendFactors[static_cast<size_t>(BlendMode::Count)] = {
        { FALSE, D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD }, // None
        { TRUE, D3D12_BLEND_SRC_ALPHA, D3D12_BLEND_INV_SRC_ALPHA, D3D12_BLEND_OP_ADD }, // Alpha
        { TRUE, D3D12_BLEND_SRC_ALPHA, D3D12_BLEND_ONE, D3D12_BLEND_OP_ADD }, // Add
        { TRUE, D3D12_BLEND_SRC_ALPHA, D3D12_BLEND_ONE, D3D12_BLEND_OP_REV_SUBTRACT }, // Subtract
        { TRUE, D3D12_BLEND_DEST_COLOR, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD }, // Multiply
    };
    const size_t index = static_cast<size_t>(mode);
    const BlendFactors& factors = kBlendFactors[index < std::size(kBlendFactors) ? index : 0];

    D3D12_RENDER_TARGET_BLEND_DESC blend { };
    blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    blend.BlendEnable = factors.enable;
    blend.SrcBlend = factors.src;
    blend.DestBlend = factors.dest;
    blend.BlendOp = factors.op;
    blend.SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.DestBlendAlpha = D3D12_BLEND_ZERO;
    blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    return blend;
}

D3D12_GRAPHICS_PIPELINE_STATE_DESC MakeDefault3dPsoDesc()
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc { };
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.DepthStencilState.DepthEnable = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
    psoDesc.SampleDesc.Count = 1;
    return psoDesc;
}

} // namespace engine::graphics::PipelineHelper
