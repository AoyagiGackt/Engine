#include "MeshSliceEffect.h"
#include "Camera.h"
#include "EngineAssert.h"
#include "Model.h"
#include "SrvManager.h"
#include "TextureManager.h"
#include <algorithm>
#include <cmath>
using namespace engine;
using namespace engine::graphics;

using namespace Microsoft::WRL;

void MeshSliceEffect::Initialize(DirectXCommon* dxCommon)
{
    ENGINE_ASSERT(dxCommon);
    dxCommon_ = dxCommon;

    // 破片ごとの定数バッファは最大数ぶんを最初に確保しておく
    pieceCB_ = dxCommon_->CreateBufferResource(kCBSlotSize * kMaxPieces);
    pieceCB_->Map(0, nullptr, reinterpret_cast<void**>(&pieceCBData_));

    CreatePipeline();
}

void MeshSliceEffect::Start(const Model* model, const Vector3& worldPos, const Vector3& scale, uint32_t seed)
{
    if (model == nullptr || model->GetVertexCount() == 0) {
        return;
    }

    worldPos_ = worldPos;
    scale_ = scale;
    textureFilePath_ = model->GetTextureFilePath();
    TextureManager::GetInstance()->LoadTexture(textureFilePath_);

    std::mt19937 rng(seed);
    std::vector<SliceVertex> vertices;
    BuildPieces(model, rng, vertices);
    if (pieces_.empty()) {
        return;
    }

    UploadVertices(vertices);

    timer_ = 0.0f;
    active_ = true;
}

void MeshSliceEffect::Reset()
{
    active_ = false;
    timer_ = 0.0f;
    pieces_.clear();
}

bool MeshSliceEffect::IsBursting() const
{
    return active_ && timer_ >= kHoldTime + kSlideTime;
}

float MeshSliceEffect::GetOverlayWeight() const
{
    if (!active_) {
        return 0.0f;
    }
    const float burstStart = kHoldTime + kSlideTime;
    if (timer_ < burstStart) {
        return 1.0f;
    }
    return std::clamp(1.0f - (timer_ - burstStart) / kBurstTime, 0.0f, 1.0f);
}

// 切断

MeshSliceEffect::SliceVertex MeshSliceEffect::LerpVertex(const SliceVertex& a, const SliceVertex& b, float t)
{
    SliceVertex v;
    v.position = { a.position.x + (b.position.x - a.position.x) * t,
        a.position.y + (b.position.y - a.position.y) * t,
        a.position.z + (b.position.z - a.position.z) * t,
        1.0f };
    v.texcoord = { a.texcoord.x + (b.texcoord.x - a.texcoord.x) * t,
        a.texcoord.y + (b.texcoord.y - a.texcoord.y) * t };
    v.normal = Normalize(Lerp(a.normal, b.normal, t));
    v.cap = a.cap + (b.cap - a.cap) * t;
    return v;
}

void MeshSliceEffect::ClipTriangle(const SliceVertex tri[3], const float dist[3], bool keepPositive,
    std::vector<SliceVertex>& outTris, std::vector<Vector3>* outCutPoints)
{
    SliceVertex poly[4];
    int polyCount = 0;

    for (int i = 0; i < 3; ++i) {
        const int j = (i + 1) % 3;
        const float di = keepPositive ? dist[i] : -dist[i];
        const float dj = keepPositive ? dist[j] : -dist[j];

        if (di >= 0.0f) {
            poly[polyCount] = tri[i];
            polyCount++;
        }
        if ((di > 0.0f && dj < 0.0f) || (di < 0.0f && dj > 0.0f)) {
            const float t = di / (di - dj);
            SliceVertex m = LerpVertex(tri[i], tri[j], t);
            poly[polyCount] = m;
            polyCount++;
            if (outCutPoints != nullptr) {
                outCutPoints->push_back({ m.position.x, m.position.y, m.position.z });
            }
        }
    }

    // 扇状に三角形へ分解（頂点順序は維持される）
    for (int k = 1; k + 1 < polyCount; ++k) {
        outTris.push_back(poly[0]);
        outTris.push_back(poly[k]);
        outTris.push_back(poly[k + 1]);
    }
}

void MeshSliceEffect::SplitPiece(const PieceBuild& src, const Vector3& n, const Vector3& p0,
    PieceBuild& outFront, PieceBuild& outBack)
{
    constexpr float kEps = 1.0e-5f;
    std::vector<Vector3> cutPoints; // 交線上の点（2個で1辺）

    const size_t triCount = src.tris.size() / 3;
    for (size_t t = 0; t < triCount; ++t) {
        const SliceVertex* v = &src.tris[t * 3];

        float d[3];
        for (int i = 0; i < 3; ++i) {
            d[i] = Dot({ v[i].position.x - p0.x, v[i].position.y - p0.y, v[i].position.z - p0.z }, n);
        }

        // 3頂点が片側に収まる三角形はそのまま振り分ける
        if (d[0] >= -kEps && d[1] >= -kEps && d[2] >= -kEps) {
            outFront.tris.insert(outFront.tris.end(), v, v + 3);
            continue;
        }
        if (d[0] <= kEps && d[1] <= kEps && d[2] <= kEps) {
            outBack.tris.insert(outBack.tris.end(), v, v + 3);
            continue;
        }

        // 平面をまたぐ三角形は両側に切り分ける（交点は表側の処理でだけ集める）
        ClipTriangle(v, d, true, outFront.tris, &cutPoints);
        ClipTriangle(v, d, false, outBack.tris, nullptr);
    }

    // 切断面のフタ（交線の重心から扇状に張る発光フラグ付き）
    if (cutPoints.size() >= 4) {
        Vector3 c = { };
        for (const auto& p : cutPoints) {
            c = c + p;
        }
        c = c * (1.0f / static_cast<float>(cutPoints.size()));

        auto addCap = [&c, &cutPoints](PieceBuild& dst, const Vector3& capNormal) {
            for (size_t k = 0; k + 1 < cutPoints.size(); k += 2) {
                const Vector3& a = cutPoints[k];
                const Vector3& b = cutPoints[k + 1];
                dst.tris.push_back({ { c.x, c.y, c.z, 1.0f }, { 0.5f, 0.5f }, capNormal, 1.0f });
                dst.tris.push_back({ { a.x, a.y, a.z, 1.0f }, { 0.5f, 0.5f }, capNormal, 1.0f });
                dst.tris.push_back({ { b.x, b.y, b.z, 1.0f }, { 0.5f, 0.5f }, capNormal, 1.0f });
            }
        };
        addCap(outFront, { -n.x, -n.y, -n.z });
        addCap(outBack, n);
    }
}

void MeshSliceEffect::BuildPieces(const Model* model, std::mt19937& rng, std::vector<SliceVertex>& outVertices)
{
    // 元メッシュを1つの破片として取り込む
    PieceBuild whole;
    const auto& srcVerts = model->GetVertices();
    const auto& srcIdx = model->GetIndices();
    auto pushVert = [&whole](const Model::VertexData& v) {
        whole.tris.push_back({ v.position, v.texcoord, v.normal, 0.0f });
    };
    if (!srcIdx.empty()) {
        for (uint32_t i : srcIdx) {
            pushVert(srcVerts[i]);
        }
    } else {
        for (const auto& v : srcVerts) {
            pushVert(v);
        }
    }

    // メッシュ全体の重心と半径（切断位置と運動量のスケール基準）
    Vector3 meshCenter = { };
    for (const auto& v : whole.tris) {
        meshCenter = meshCenter + Vector3 { v.position.x, v.position.y, v.position.z };
    }
    meshCenter = meshCenter * (1.0f / static_cast<float>(whole.tris.size()));

    float radius = 0.0f;
    for (const auto& v : whole.tris) {
        radius = (std::max)(radius, Distance(meshCenter, { v.position.x, v.position.y, v.position.z }));
    }
    if (radius <= 0.0f) {
        radius = 1.0f;
    }
    meshRadius_ = radius;
    slideDist_ = radius * 0.35f;

    // 中心付近を通るランダムな平面群で分割していく
    // 法線はほぼXY面内に寄せて、横視点から切り口が見えやすい向きにする
    std::uniform_real_distribution<float> angDist(0.0f, 6.2831853f);
    std::uniform_real_distribution<float> tiltDist(-0.35f, 0.35f);
    std::uniform_real_distribution<float> offDist(-0.3f, 0.3f);

    std::vector<PieceBuild> built;
    built.push_back(std::move(whole));
    std::vector<std::pair<Vector3, Vector3>> planes; // 法線・通過点

    for (int i = 0; i < kPlaneCount; ++i) {
        const float ang = angDist(rng);
        const Vector3 n = Normalize({ std::cos(ang), std::sin(ang), tiltDist(rng) });
        const Vector3 p0 = meshCenter + Vector3 { offDist(rng) * radius, offDist(rng) * radius, 0.0f };
        planes.emplace_back(n, p0);

        std::vector<PieceBuild> next;
        for (auto& piece : built) {
            if (static_cast<int>(next.size()) >= kMaxPieces - 1) {
                next.push_back(std::move(piece));
                continue;
            }
            PieceBuild front, back;
            SplitPiece(piece, n, p0, front, back);
            if (front.tris.empty() || back.tris.empty()) {
                next.push_back(std::move(piece));
            } else {
                next.push_back(std::move(front));
                next.push_back(std::move(back));
            }
        }
        built = std::move(next);
    }

    // 破片ごとの頂点範囲と運動パラメータを確定する
    std::uniform_real_distribution<float> jitterDist(-0.25f, 0.25f);
    std::uniform_real_distribution<float> speedDist(2.5f, 4.5f);
    std::uniform_real_distribution<float> upDist(1.0f, 2.5f);
    std::uniform_real_distribution<float> spinDist(-6.0f, 6.0f);

    outVertices.clear();
    pieces_.clear();
    for (auto& pb : built) {
        Piece pc;
        pc.vertexOffset = static_cast<uint32_t>(outVertices.size());
        pc.vertexCount = static_cast<uint32_t>(pb.tris.size());
        outVertices.insert(outVertices.end(), pb.tris.begin(), pb.tris.end());

        Vector3 c = { };
        for (const auto& v : pb.tris) {
            c = c + Vector3 { v.position.x, v.position.y, v.position.z };
        }
        pc.centroid = c * (1.0f / static_cast<float>(pb.tris.size()));

        // ずれる方向 = 各切断面のどちら側にいるかを合成したもの
        Vector3 dir = { };
        for (const auto& [n, p0] : planes) {
            const float side = (Dot(Subtract(pc.centroid, p0), n) >= 0.0f) ? 1.0f : -1.0f;
            dir = dir + n * side;
        }
        dir = dir + Vector3 { jitterDist(rng), jitterDist(rng), jitterDist(rng) };
        if (Length(dir) < 1.0e-4f) {
            dir = { 0.0f, 1.0f, 0.0f };
        }
        pc.slideDir = Normalize(dir);

        pc.velocity = pc.slideDir * (radius * speedDist(rng)) + Vector3 { 0.0f, radius * upDist(rng), 0.0f };
        pc.angularVel = { spinDist(rng), spinDist(rng), spinDist(rng) };

        pieces_.push_back(pc);
    }
}

void MeshSliceEffect::UploadVertices(const std::vector<SliceVertex>& vertices)
{
    const uint32_t count = static_cast<uint32_t>(vertices.size());
    if (count > vertexCapacity_) {
        vertexBuffer_ = dxCommon_->CreateBufferResource(sizeof(SliceVertex) * count);
        vertexBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData_));
        vertexCapacity_ = count;
    }
    std::copy(vertices.begin(), vertices.end(), vertexData_);

    vbv_.BufferLocation = vertexBuffer_->GetGPUVirtualAddress();
    vbv_.SizeInBytes = static_cast<UINT>(sizeof(SliceVertex) * count);
    vbv_.StrideInBytes = sizeof(SliceVertex);
}

// 更新・描画

void MeshSliceEffect::Update(float dt, Camera* camera)
{
    if (!active_ || camera == nullptr) {
        return;
    }

    timer_ += dt;
    const float burstStart = kHoldTime + kSlideTime;
    const float endTime = burstStart + kBurstTime;

    const float slideT = std::clamp((timer_ - kHoldTime) / kSlideTime, 0.0f, 1.0f);
    const float burstT = std::clamp((timer_ - burstStart) / kBurstTime, 0.0f, 1.0f);

    // ずれは減速で止まり、消滅は加速で消える
    const float slideEase = 1.0f - (1.0f - slideT) * (1.0f - slideT) * (1.0f - slideT);
    const float alpha = 1.0f - burstT * burstT;

    if (timer_ >= burstStart) {
        for (auto& pc : pieces_) {
            pc.velocity.y -= meshRadius_ * 10.0f * dt;
            pc.offset = pc.offset + pc.velocity * dt;
            pc.rotation = pc.rotation + pc.angularVel * dt;
        }
    } else {
        for (auto& pc : pieces_) {
            pc.offset = pc.slideDir * (slideEase * slideDist_);
        }
    }

    // 断面の発光はずれと同時に立ち上がり、飛散中は最大
    const float glowStrength = (timer_ >= burstStart) ? 1.2f : 0.15f + slideEase * 1.05f;
    const float tint = 1.0f + slideEase * 0.4f * (1.0f - burstT);

    const Matrix4x4 viewProj = Multiply(camera->GetViewMatrix(), camera->GetProjectionMatrix());
    const Matrix4x4 modelMat = MakeAffineMatrix(scale_, Vector3 { }, worldPos_);

    for (size_t i = 0; i < pieces_.size(); ++i) {
        const Piece& pc = pieces_[i];
        const Vector3 negC = { -pc.centroid.x, -pc.centroid.y, -pc.centroid.z };

        // 重心を支点に回転させてからずらし、モデルのワールド変換を掛ける
        const Matrix4x4 local = Multiply(MakeTranslateMatrix(negC),
            MakeAffineMatrix({ 1.0f, 1.0f, 1.0f }, pc.rotation, pc.centroid + pc.offset));
        const Matrix4x4 world = Multiply(local, modelMat);

        auto* params = reinterpret_cast<PieceParams*>(pieceCBData_ + i * kCBSlotSize);
        params->wvp = Multiply(world, viewProj);
        params->world = world;
        params->color = { tint, tint, tint, alpha };
        params->glow = { 0.65f, 0.9f, 1.0f, glowStrength };
    }

    if (timer_ >= endTime) {
        active_ = false;
    }
}

void MeshSliceEffect::Draw()
{
    if (!active_ || pieces_.empty()) {
        return;
    }

    ID3D12GraphicsCommandList* cmd = dxCommon_->GetCommandList();

    cmd->SetGraphicsRootSignature(rootSignature_.Get());
    cmd->SetPipelineState(pipelineState_.Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->IASetVertexBuffers(0, 1, &vbv_);

    SrvManager::GetInstance()->PreDraw();
    D3D12_GPU_DESCRIPTOR_HANDLE texHandle = TextureManager::GetInstance()->GetSrvHandleGPU(textureFilePath_);
    cmd->SetGraphicsRootDescriptorTable(1, texHandle);

    const D3D12_GPU_VIRTUAL_ADDRESS cbBase = pieceCB_->GetGPUVirtualAddress();
    for (size_t i = 0; i < pieces_.size(); ++i) {
        cmd->SetGraphicsRootConstantBufferView(0, cbBase + i * kCBSlotSize);
        cmd->DrawInstanced(pieces_[i].vertexCount, 1, pieces_[i].vertexOffset, 0);
    }
}

void MeshSliceEffect::CreatePipeline()
{
    ID3D12Device* device = dxCommon_->GetDevice();

    D3D12_DESCRIPTOR_RANGE texRange[1] = { };
    texRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    texRange[0].NumDescriptors = 1;
    texRange[0].BaseShaderRegister = 0; // t0
    texRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParams[2] = { };
    // [0] CBV b0 (破片ごとのパラメータ)
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParams[0].Descriptor.ShaderRegister = 0;
    // [1] SRV t0 (テクスチャ)
    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[1].DescriptorTable.pDescriptorRanges = texRange;
    rootParams[1].DescriptorTable.NumDescriptorRanges = 1;

    D3D12_STATIC_SAMPLER_DESC sampler = { };
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rsDesc = { };
    rsDesc.NumParameters = _countof(rootParams);
    rsDesc.pParameters = rootParams;
    rsDesc.NumStaticSamplers = 1;
    rsDesc.pStaticSamplers = &sampler;
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> sigBlob, errBlob;
    HRESULT hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        &sigBlob, &errBlob);
    ENGINE_ASSERT(SUCCEEDED(hr));
    hr = device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(),
        IID_PPV_ARGS(&rootSignature_));
    ENGINE_ASSERT(SUCCEEDED(hr));

    ComPtr<IDxcBlob> vsBlob = dxCommon_->CompileShader(L"Resources/shaders/meshslice/MeshSlice.VS.hlsl", L"vs_6_0");
    ComPtr<IDxcBlob> psBlob = dxCommon_->CompileShader(L"Resources/shaders/meshslice/MeshSlice.PS.hlsl", L"ps_6_0");

    D3D12_INPUT_ELEMENT_DESC inputElems[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 1, DXGI_FORMAT_R32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = { };
    psoDesc.pRootSignature = rootSignature_.Get();
    psoDesc.InputLayout = { inputElems, _countof(inputElems) };
    psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
    psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };

    // 通常アルファブレンド（破片本体はほぼ不透明で、最後だけフェードする）
    auto& rt = psoDesc.BlendState.RenderTarget[0];
    rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    rt.BlendEnable = TRUE;
    rt.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    rt.BlendOp = D3D12_BLEND_OP_ADD;
    rt.SrcBlendAlpha = D3D12_BLEND_ONE;
    rt.DestBlendAlpha = D3D12_BLEND_ZERO;
    rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;

    // 切断で生じる裏面や断面も見えるので両面描画
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;

    // 深度書き込みあり（破片同士の前後関係をZバッファで解決する）
    psoDesc.DepthStencilState.DepthEnable = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
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
