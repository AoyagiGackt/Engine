/**
 * @file WeaponTrailEffect.cpp
 * @brief WeaponTrailEffectの軌跡サンプルの管理、Catmull-Rom補間による帯の生成、描画を実装するファイル
 */
#include "WeaponTrailEffect.h"
#include "Camera.h"
#include "EngineAssert.h"
#include <algorithm>
using namespace engine;
using namespace engine::graphics;

using namespace Microsoft::WRL;

namespace {

constexpr float kMinLifetime = 0.001f; // 寿命0で割らないための下限

/** @brief Catmull-Rom曲線上の点を返す（p1→p2間をtで補間し、p0/p3は接線を決める前後の点） */
Vector3 CatmullRom(const Vector3& p0, const Vector3& p1, const Vector3& p2, const Vector3& p3, float t)
{
    const float t2 = t * t;
    const float t3 = t2 * t;
    auto axis = [t, t2, t3](float a0, float a1, float a2, float a3) {
        return 0.5f * ((2.0f * a1) + (-a0 + a2) * t + (2.0f * a0 - 5.0f * a1 + 4.0f * a2 - a3) * t2 + (-a0 + 3.0f * a1 - 3.0f * a2 + a3) * t3);
    };
    return { axis(p0.x, p1.x, p2.x, p3.x), axis(p0.y, p1.y, p2.y, p3.y), axis(p0.z, p1.z, p2.z, p3.z) };
}

} // namespace

void WeaponTrailEffect::Initialize(DirectXCommon* dxCommon)
{
    ENGINE_ASSERT(dxCommon);
    dxCommon_ = dxCommon;

    sceneCB_ = dxCommon_->CreateBufferResource(sizeof(SceneCB));
    sceneCB_->Map(0, nullptr, reinterpret_cast<void**>(&sceneCBData_));
    sceneCBData_->viewProj = MakeIdentity4x4();

    vertexBuffer_ = dxCommon_->CreateBufferResource(sizeof(TrailVertex) * kMaxVertices);
    vertexBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData_));
    vbv_.BufferLocation = vertexBuffer_->GetGPUVirtualAddress();
    vbv_.SizeInBytes = static_cast<UINT>(sizeof(TrailVertex) * kMaxVertices);
    vbv_.StrideInBytes = sizeof(TrailVertex);

    CreatePipeline();
}

void WeaponTrailEffect::AddSample(const Vector3& base, const Vector3& tip)
{
    if (static_cast<int>(samples_.size()) >= kMaxSamples) {
        samples_.pop_front();
    }
    samples_.push_back({ base, tip, 0.0f, strokeId_ });
}

void WeaponTrailEffect::Update(float dt, Camera* camera)
{
    if (camera != nullptr) {
        sceneCBData_->viewProj = Multiply(camera->GetViewMatrix(), camera->GetProjectionMatrix());
    }

    for (auto& s : samples_) {
        s.age += dt;
    }
    while (!samples_.empty() && samples_.front().age >= style_.lifetime) {
        samples_.pop_front();
    }

    RebuildVertices();
}

Vector4 WeaponTrailEffect::MakeColor(const Vector4& color, float age) const
{
    // 生まれた直後が最も濃く、寿命に向かって二乗で抜ける（尾がすっと消える）
    const float life = (std::max)(style_.lifetime, kMinLifetime);
    float fade = std::clamp(1.0f - age / life, 0.0f, 1.0f);
    fade *= fade;
    return { color.x, color.y, color.z, color.w * fade };
}

void WeaponTrailEffect::RebuildVertices()
{
    vertexCount_ = 0;
    const int count = static_cast<int>(samples_.size());
    const int subdiv = std::clamp(style_.subdivisions, 1, kMaxSubdivisions);

    auto pushVertex = [this](const Vector3& p, const Vector4& c) {
        vertexData_[vertexCount_++] = { { p.x, p.y, p.z, 1.0f }, c };
    };

    for (int i = 0; i + 1 < count; ++i) {
        const Sample& a = samples_[i];
        const Sample& b = samples_[i + 1];
        if (a.stroke != b.stroke) {
            continue;
        }
        // 帯の端では前後の点が無いので端点自身を使う（端で曲線が外へはみ出さない）
        const Sample& prev = (i > 0 && samples_[i - 1].stroke == a.stroke) ? samples_[i - 1] : a;
        const Sample& next = (i + 2 < count && samples_[i + 2].stroke == a.stroke) ? samples_[i + 2] : b;

        for (int s = 0; s < subdiv; ++s) {
            if (vertexCount_ + kVertsPerQuad > kMaxVertices) {
                return;
            }
            const float t0 = static_cast<float>(s) / static_cast<float>(subdiv);
            const float t1 = static_cast<float>(s + 1) / static_cast<float>(subdiv);

            const Vector3 base0 = CatmullRom(prev.base, a.base, b.base, next.base, t0);
            const Vector3 base1 = CatmullRom(prev.base, a.base, b.base, next.base, t1);
            const Vector3 tip0 = CatmullRom(prev.tip, a.tip, b.tip, next.tip, t0);
            const Vector3 tip1 = CatmullRom(prev.tip, a.tip, b.tip, next.tip, t1);
            const float age0 = a.age + (b.age - a.age) * t0;
            const float age1 = a.age + (b.age - a.age) * t1;

            const Vector4 baseColor0 = MakeColor(style_.baseColor, age0);
            const Vector4 baseColor1 = MakeColor(style_.baseColor, age1);
            const Vector4 tipColor0 = MakeColor(style_.tipColor, age0);
            const Vector4 tipColor1 = MakeColor(style_.tipColor, age1);

            pushVertex(base0, baseColor0);
            pushVertex(tip0, tipColor0);
            pushVertex(tip1, tipColor1);
            pushVertex(base0, baseColor0);
            pushVertex(tip1, tipColor1);
            pushVertex(base1, baseColor1);
        }
    }
}

bool WeaponTrailEffect::GetRecentTipMotion(float window, Vector3& outMotion) const
{
    if (samples_.size() < 2) {
        return false;
    }
    const Sample& newest = samples_.back();
    const Sample* oldest = nullptr;
    for (auto it = samples_.rbegin() + 1; it != samples_.rend(); ++it) {
        if (it->stroke != newest.stroke || it->age - newest.age > window) {
            break;
        }
        oldest = &(*it);
    }
    if (oldest == nullptr) {
        return false;
    }
    outMotion = { newest.tip.x - oldest->tip.x, newest.tip.y - oldest->tip.y, newest.tip.z - oldest->tip.z };
    return true;
}

void WeaponTrailEffect::Draw()
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

void WeaponTrailEffect::CreatePipeline()
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

    // 位置+頂点色だけの加算描画は刃フラッシュと同じなのでシェーダーを共用する
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

    // 加算ブレンド（重なった所ほど白く光る）
    auto& rt = psoDesc.BlendState.RenderTarget[0];
    rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    rt.BlendEnable = TRUE;
    rt.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    rt.DestBlend = D3D12_BLEND_ONE;
    rt.BlendOp = D3D12_BLEND_OP_ADD;
    rt.SrcBlendAlpha = D3D12_BLEND_ONE;
    rt.DestBlendAlpha = D3D12_BLEND_ZERO;
    rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;

    // 振りの向きで帯が裏返るので両面描画
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
