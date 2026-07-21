/**
 * @file BladeFlashEffect.cpp
 * @brief BladeFlashEffectの画面効果の生成、更新、描画に関する具体的な処理を実装するファイル
 */
#include "BladeFlashEffect.h"
#include "Camera.h"
#include "EngineAssert.h"
#include <algorithm>
#include <cmath>
using namespace engine;
using namespace engine::graphics;

using namespace Microsoft::WRL;

void BladeFlashEffect::Initialize(DirectXCommon* dxCommon)
{
    ENGINE_ASSERT(dxCommon);
    dxCommon_ = dxCommon;

    sceneCB_ = dxCommon_->CreateBufferResource(sizeof(SceneCB));
    sceneCB_->Map(0, nullptr, reinterpret_cast<void**>(&sceneCBData_));
    sceneCBData_->viewProj = MakeIdentity4x4();

    vertexBuffer_ = dxCommon_->CreateBufferResource(sizeof(BladeVertex) * kMaxBlades * kVertsPerBlade);
    vertexBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData_));
    vbv_.BufferLocation = vertexBuffer_->GetGPUVirtualAddress();
    vbv_.SizeInBytes = static_cast<UINT>(sizeof(BladeVertex) * kMaxBlades * kVertsPerBlade);
    vbv_.StrideInBytes = sizeof(BladeVertex);

    CreatePipeline();
}

void BladeFlashEffect::Emit(const Vector3& center, int count, float radius, float minLen, float maxLen)
{
    std::uniform_real_distribution<float> angDist(0.0f, 6.2831853f);
    std::uniform_real_distribution<float> radDist(0.0f, radius);
    std::uniform_real_distribution<float> zDist(-0.4f, 0.4f);
    std::uniform_real_distribution<float> lenDist(minLen, maxLen);
    std::uniform_real_distribution<float> widthDist(0.045f, 0.09f);
    std::uniform_real_distribution<float> lifeDist(0.14f, 0.30f);
    std::uniform_real_distribution<float> driftDist(-1.5f, 1.5f);
    std::uniform_real_distribution<float> hueDist(0.0f, 1.0f);

    for (int i = 0; i < count; ++i) {
        if (static_cast<int>(blades_.size()) >= kMaxBlades) {
            break;
        }

        Blade b;
        const float posAng = angDist(rng_);
        const float posRad = radDist(rng_);
        b.pos = { center.x + std::cos(posAng) * posRad,
            center.y + std::sin(posAng) * posRad,
            center.z + zDist(rng_) };

        b.angle = angDist(rng_);
        b.halfLen = lenDist(rng_) * 0.5f;
        b.halfWidth = b.halfLen * widthDist(rng_);
        b.life = lifeDist(rng_);
        b.drift = Vector3 { std::cos(b.angle), std::sin(b.angle), 0.0f } * driftDist(rng_);

        // 大半は氷青、たまに紫がかった刃を混ぜる
        if (hueDist(rng_) < 0.8f) {
            b.coreColor = { 0.95f, 1.0f, 1.0f, 1.0f };
            b.edgeColor = { 0.35f, 0.7f, 1.0f, 1.0f };
        } else {
            b.coreColor = { 0.9f, 0.8f, 1.0f, 1.0f };
            b.edgeColor = { 0.55f, 0.3f, 1.0f, 1.0f };
        }

        blades_.push_back(b);
    }
}

void BladeFlashEffect::Update(float dt, Camera* camera)
{
    if (camera != nullptr) {
        sceneCBData_->viewProj = Multiply(camera->GetViewMatrix(), camera->GetProjectionMatrix());
    }

    for (auto& b : blades_) {
        b.age += dt;
        b.pos = b.pos + b.drift * dt;
    }
    blades_.erase(
        std::remove_if(blades_.begin(), blades_.end(),
            [](const Blade& b) { return b.age >= b.life; }),
        blades_.end());

    // 頂点バッファ再構築（中心が白熱し、先端・側面へ透ける菱形）
    vertexCount_ = 0;
    for (const auto& b : blades_) {
        const float t = b.age / b.life;

        // 出現直後に一気に伸び、余韻で消える
        const float grow = (std::min)(1.0f, t * 5.0f);
        const float fade = (1.0f - t) * (1.0f - t) * (std::min)(1.0f, t * 8.0f + 0.35f);
        const float width = b.halfWidth * (1.0f - t * 0.5f);

        const Vector3 dir = Vector3 { std::cos(b.angle), std::sin(b.angle), 0.0f } * (b.halfLen * grow);
        const Vector3 perp = Vector3 { -std::sin(b.angle), std::cos(b.angle), 0.0f } * width;

        const Vector3 tipA = b.pos + dir;
        const Vector3 tipB = { b.pos.x - dir.x, b.pos.y - dir.y, b.pos.z - dir.z };
        const Vector3 sideA = b.pos + perp;
        const Vector3 sideB = { b.pos.x - perp.x, b.pos.y - perp.y, b.pos.z - perp.z };

        const Vector4 core = { b.coreColor.x, b.coreColor.y, b.coreColor.z, fade };
        const Vector4 side = { b.edgeColor.x, b.edgeColor.y, b.edgeColor.z, fade * 0.5f };
        const Vector4 tip = { b.edgeColor.x, b.edgeColor.y, b.edgeColor.z, 0.0f };

        auto pushTri = [this](const Vector3& p0, const Vector4& c0,
                           const Vector3& p1, const Vector4& c1,
                           const Vector3& p2, const Vector4& c2) {
            vertexData_[vertexCount_ + 0] = { { p0.x, p0.y, p0.z, 1.0f }, c0 };
            vertexData_[vertexCount_ + 1] = { { p1.x, p1.y, p1.z, 1.0f }, c1 };
            vertexData_[vertexCount_ + 2] = { { p2.x, p2.y, p2.z, 1.0f }, c2 };
            vertexCount_ += 3;
        };
        pushTri(b.pos, core, tipA, tip, sideA, side);
        pushTri(b.pos, core, sideA, side, tipB, tip);
        pushTri(b.pos, core, tipB, tip, sideB, side);
        pushTri(b.pos, core, sideB, side, tipA, tip);
    }
}

void BladeFlashEffect::Draw()
{
    if (vertexCount_ == 0) {
        return;
    }

    ID3D12GraphicsCommandList* cmd = dxCommon_->GetCommandList();

    cmd->SetGraphicsRootSignature(rootSignature_.Get());
    cmd->SetPipelineState(pipelineState_.Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->IASetVertexBuffers(0, 1, &vbv_);
    cmd->SetGraphicsRootConstantBufferView(0, sceneCB_->GetGPUVirtualAddress());
    cmd->DrawInstanced(vertexCount_, 1, 0, 0);
}

void BladeFlashEffect::CreatePipeline()
{
    ID3D12Device* device = dxCommon_->GetDevice();

    D3D12_ROOT_PARAMETER rootParams[1] = { };
    // [0] CBV b0 (ビュープロジェクション行列)
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rootParams[0].Descriptor.ShaderRegister = 0;

    D3D12_ROOT_SIGNATURE_DESC rsDesc = { };
    rsDesc.NumParameters = _countof(rootParams);
    rsDesc.pParameters = rootParams;
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> sigBlob, errBlob;
    HRESULT hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        &sigBlob, &errBlob);
    ENGINE_ASSERT(SUCCEEDED(hr));
    hr = device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(),
        IID_PPV_ARGS(&rootSignature_));
    ENGINE_ASSERT(SUCCEEDED(hr));

    ComPtr<IDxcBlob> vsBlob = dxCommon_->CompileShader(L"Resources/shaders/bladeflash/BladeFlash.VS.hlsl", L"vs_6_0");
    ComPtr<IDxcBlob> psBlob = dxCommon_->CompileShader(L"Resources/shaders/bladeflash/BladeFlash.PS.hlsl", L"ps_6_0");

    D3D12_INPUT_ELEMENT_DESC inputElems[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = { };
    psoDesc.pRootSignature = rootSignature_.Get();
    psoDesc.InputLayout = { inputElems, _countof(inputElems) };
    psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
    psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };

    // 加算ブレンド（発光する刃）
    auto& rt = psoDesc.BlendState.RenderTarget[0];
    rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    rt.BlendEnable = TRUE;
    rt.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    rt.DestBlend = D3D12_BLEND_ONE;
    rt.BlendOp = D3D12_BLEND_OP_ADD;
    rt.SrcBlendAlpha = D3D12_BLEND_ONE;
    rt.DestBlendAlpha = D3D12_BLEND_ZERO;
    rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;

    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;

    psoDesc.DepthStencilState.DepthEnable = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

    psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
    psoDesc.SampleDesc.Count = 1;

    hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState_));
    ENGINE_ASSERT(SUCCEEDED(hr));
}
