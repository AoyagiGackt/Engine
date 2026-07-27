/**
 * @file ParticleManager.cpp
 * @brief ParticleManagerの描画資源とGPU処理の管理に関する具体的な処理を実装するファイル
 */
#include "ParticleManager.h"
#include "EngineAssert.h"
#include "GameConstants.h"
#include "TextureManager.h"
#include <cmath>
#include <d3dx12.h>
#include <numbers>
#include <random>
using namespace engine;
using namespace engine::graphics;

using namespace Microsoft::WRL;

// ══════════════════════════════════════════════════════
// 初期化とグループ管理
// ══════════════════════════════════════════════════════

ParticleManager* ParticleManager::GetInstance()
{
    static ParticleManager instance;
    return &instance;
}

//  初期化 / 終了

void ParticleManager::Initialize(DirectXCommon* dxCommon)
{
    ENGINE_ASSERT(dxCommon);
    dxCommon_ = dxCommon;

    CreateRootSignature();
    CreatePipelineState();
    CreateCSRootSignature();
    CreateCSPipelineState();
    CreateCSEmitRootSignature();
    CreateCSEmitPipelineState();

    // CS 定数バッファ (UPLOAD heap, 256 bytes, 常時マップ)
    csConstantsBuffer_ = dxCommon_->CreateBufferResource(256);
    csConstantsBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&csConstantsData_));

    CreateQuadGeometry();
}

void ParticleManager::Finalize()
{
    particleGroups_.clear();

    if (csConstantsBuffer_) {
        csConstantsBuffer_->Unmap(0, nullptr);
        csConstantsData_ = nullptr;
    }

    quadIndexBuffer_.Reset();
    quadVertexBuffer_.Reset();
    csEmitPipelineState_.Reset();
    csEmitRootSignature_.Reset();
    csPipelineState_.Reset();
    csRootSignature_.Reset();
    graphicsPipelineStateAlpha_.Reset();
    graphicsPipelineState_.Reset();
    rootSignature_.Reset();
    dxCommon_ = nullptr;
}

//  グループ生成

void ParticleManager::CreateParticleGroup(const std::string& name,
    const std::string& textureFilePath)
{
    ENGINE_ASSERT(!particleGroups_.contains(name));

    ParticleGroup& group = particleGroups_[name];
    group.textureFilePath = textureFilePath;

    TextureManager::GetInstance()->LoadTexture(textureFilePath);

    CreateParticleStateBuffers(group);
    CreateParticleInstancingResource(group);
    InitParticleGroupState(group);
}

void ParticleManager::CreateParticleStateBuffers(ParticleGroup& group)
{
    ID3D12Device* device = dxCommon_->GetDevice();
    const UINT64 stateSize = sizeof(GPUParticleState) * ParticleGroup::kNumMaxInstance;

    // particleStateBuffer: DEFAULT heap, UAV
    {
        D3D12_HEAP_PROPERTIES hp { D3D12_HEAP_TYPE_DEFAULT };
        D3D12_RESOURCE_DESC rd { };
        rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width = stateSize;
        rd.Height = rd.DepthOrArraySize = rd.MipLevels = 1;
        rd.SampleDesc.Count = 1;
        rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        HRESULT hr = device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_COMMON, nullptr, // バッファは常に COMMON で作成される (#1328 対策)
            IID_PPV_ARGS(&group.particleStateBuffer));
        ENGINE_ASSERT(SUCCEEDED(hr));
    }

    // particleUploadBuffer: UPLOAD heap, 常時マップ
    {
        D3D12_HEAP_PROPERTIES hp { D3D12_HEAP_TYPE_UPLOAD };
        D3D12_RESOURCE_DESC rd { };
        rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width = stateSize;
        rd.Height = rd.DepthOrArraySize = rd.MipLevels = 1;
        rd.SampleDesc.Count = 1;
        rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        HRESULT hr = device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&group.particleUploadBuffer));
        ENGINE_ASSERT(SUCCEEDED(hr));
        hr = group.particleUploadBuffer->Map(0, nullptr,
            reinterpret_cast<void**>(&group.particleUploadData));
        ENGINE_ASSERT(SUCCEEDED(hr));
        memset(group.particleUploadData, 0, static_cast<size_t>(stateSize));
    }
}

void ParticleManager::CreateParticleInstancingResource(ParticleGroup& group)
{
    ID3D12Device* device = dxCommon_->GetDevice();
    const UINT64 instancSize = sizeof(ParticleForGPU) * ParticleGroup::kNumMaxInstance;

    // instancingResource: DEFAULT heap, UAV (CS が書く)
    {
        D3D12_HEAP_PROPERTIES hp { D3D12_HEAP_TYPE_DEFAULT };
        D3D12_RESOURCE_DESC rd { };
        rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width = instancSize;
        rd.Height = rd.DepthOrArraySize = rd.MipLevels = 1;
        rd.SampleDesc.Count = 1;
        rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        HRESULT hr = device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_COMMON, nullptr, // バッファは常に COMMON で作成される (#1328 対策)
            IID_PPV_ARGS(&group.instancingResource));
        ENGINE_ASSERT(SUCCEEDED(hr));
    }

    // SRV for instancingResource (VS が t0 として読む)
    group.srvIndex = SrvManager::GetInstance()->Allocate();
    D3D12_CPU_DESCRIPTOR_HANDLE cpuH = SrvManager::GetInstance()->GetCPUDescriptorHandle(group.srvIndex);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc { };
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.NumElements = ParticleGroup::kNumMaxInstance;
    srvDesc.Buffer.StructureByteStride = sizeof(ParticleForGPU);
    device->CreateShaderResourceView(group.instancingResource.Get(), &srvDesc, cpuH);
}

void ParticleManager::InitParticleGroupState(ParticleGroup& group)
{
    group.slotExpiry.fill(0.0f);
    group.aliveCount = 0;
    group.groupTime = 0.0f;
    group.needsInit = true;
    group.instancingInSRV = false;

    group.freeList.clear();
    group.freeList.reserve(ParticleGroup::kNumMaxInstance);
    for (uint32_t i = ParticleGroup::kNumMaxInstance; i-- > 0;) {
        group.freeList.push_back(i);
    }

    // エミッターバッファ (UPLOAD heap, 256 bytes, 常時マップ)
    group.emitterBuffer = dxCommon_->CreateBufferResource(256);
    group.emitterBuffer->Map(0, nullptr, reinterpret_cast<void**>(&group.emitterData));
    memset(group.emitterData, 0, sizeof(Emitter));
    group.emitterData->lifeTime = 1.0f; // デフォルト 1 秒
}

//  スロット管理

uint32_t ParticleManager::AllocateSlot(ParticleGroup& group)
{
    if (group.freeList.empty()) {
        return UINT32_MAX;
    }
    uint32_t slot = group.freeList.back();
    group.freeList.pop_back();
    return slot;
}

// ══════════════════════════════════════════════════════
// GPU更新
// ══════════════════════════════════════════════════════

void ParticleManager::UpdateCSConstants(Camera* camera, float dt)
{
    Matrix4x4 billboard = MakeIdentity4x4();
    Matrix4x4 cameraView = camera->GetViewMatrix();
    billboard.m[0][0] = cameraView.m[0][0];
    billboard.m[0][1] = cameraView.m[1][0];
    billboard.m[0][2] = cameraView.m[2][0];
    billboard.m[1][0] = cameraView.m[0][1];
    billboard.m[1][1] = cameraView.m[1][1];
    billboard.m[1][2] = cameraView.m[2][1];
    billboard.m[2][0] = cameraView.m[0][2];
    billboard.m[2][1] = cameraView.m[1][2];
    billboard.m[2][2] = cameraView.m[2][2];

    csConstantsData_->billboard = billboard;
    csConstantsData_->viewProj = Multiply(camera->GetViewMatrix(), camera->GetProjectionMatrix());
    csConstantsData_->deltaTime = dt;
    csConstantsData_->maxParticles = ParticleGroup::kNumMaxInstance;
}

void ParticleManager::TransitionInstancingToUAV(ParticleGroup& group, ID3D12GraphicsCommandList* cmd)
{
    if (!group.instancingInSRV) {
        return;
    }
    DirectXCommon::TransitionBarrier(cmd, group.instancingResource.Get(),
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    group.instancingInSRV = false;
}

void ParticleManager::ExpireAndRespawnSlots(ParticleGroup& group)
{
    if (!group.autoRespawn) {
        for (uint32_t i = 0; i < ParticleGroup::kNumMaxInstance; ++i) {
            if (group.slotExpiry[i] > 0.0f && group.groupTime >= group.slotExpiry[i]) {
                group.freeList.push_back(i);
                group.slotExpiry[i] = 0.0f;
                group.aliveCount--;
            }
        }
        return;
    }

    static std::mt19937 rng { std::random_device { }() };
    const ParticleGroup::RespawnConfig& cfg = group.respawnConfig;
    std::uniform_real_distribution<float> xzDist(-cfg.radius, cfg.radius);
    std::uniform_real_distribution<float> yDist(0.0f, 12.0f);
    std::uniform_real_distribution<float> lifeDist(cfg.lifeTimeMin, cfg.lifeTimeMax);
    std::uniform_real_distribution<float> velXZDist(-0.3f, 0.3f);
    std::uniform_real_distribution<float> velYDist(0.1f, 0.6f);

    for (uint32_t i = 0; i < cfg.count; ++i) {
        if (group.slotExpiry[i] <= 0.0f || group.groupTime < group.slotExpiry[i]) {
            continue;
        }
        float lifeTime = lifeDist(rng);
        GPUParticleState& p = group.particleUploadData[i];
        p.position = { cfg.center.x + xzDist(rng), cfg.center.y + yDist(rng), cfg.center.z + xzDist(rng) };
        p.lifeTime = lifeTime;
        p.velocity = { velXZDist(rng), velYDist(rng), velXZDist(rng) };
        p.currentTime = 0.0f;
        p.color = cfg.color;
        p.scale = { cfg.scale, cfg.scale, cfg.scale };
        p.rotateZ = 0.0f;
        p.alive = 1;
        p.curveFlag = 0;
        group.slotExpiry[i] = group.groupTime + lifeTime + 0.1f;
        group.pendingSlots.push_back(i);
    }
}

void ParticleManager::FlushPendingSlotsToGPU(ParticleGroup& group, ID3D12GraphicsCommandList* cmd)
{
    if (!group.needsInit && group.pendingSlots.empty()) {
        return;
    }

    // バッファは COMMON で作成される（D3D12 #1328 対策）
    DirectXCommon::TransitionBarrier(cmd, group.particleStateBuffer.Get(),
        group.particleStateFresh ? D3D12_RESOURCE_STATE_COMMON : D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_DEST);
    group.particleStateFresh = false;

    if (group.needsInit) {
        cmd->CopyBufferRegion(group.particleStateBuffer.Get(), 0,
            group.particleUploadBuffer.Get(), 0,
            sizeof(GPUParticleState) * ParticleGroup::kNumMaxInstance);
        group.needsInit = false;
    }
    for (uint32_t slot : group.pendingSlots) {
        UINT64 offset = static_cast<UINT64>(slot) * sizeof(GPUParticleState);
        cmd->CopyBufferRegion(group.particleStateBuffer.Get(), offset,
            group.particleUploadBuffer.Get(), offset, sizeof(GPUParticleState));
    }
    group.pendingSlots.clear();

    DirectXCommon::TransitionBarrier(cmd, group.particleStateBuffer.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
}

void ParticleManager::DispatchEmitCS(ParticleGroup& group, ID3D12GraphicsCommandList* cmd, float dt)
{
    Emitter* e = group.emitterData;
    e->time = *reinterpret_cast<uint32_t*>(&group.groupTime); // float ビット列を uint でそのまま渡す
    e->seed++;
    if (e->frequency > 0.0f) {
        e->frequencyTime += dt;
        if (e->frequency <= e->frequencyTime) {
            e->frequencyTime -= e->frequency;
            e->emit = 1;
        } else {
            e->emit = 0;
        }
    }
    if (e->emit == 0) {
        return;
    }

    // スロット有効期限を延長
    const uint32_t count = (std::min)(e->count, ParticleGroup::kNumMaxInstance);
    const float maxExpiry = group.groupTime + e->lifeTime + 0.1f;
    for (uint32_t i = 0; i < count; ++i) {
        group.slotExpiry[i] = maxExpiry;
    }

    // 発射前にステートバッファ全体をアップロード（GPU キャッシュをフラッシュ）
    DirectXCommon::TransitionBarrier(cmd, group.particleStateBuffer.Get(),
        group.particleStateFresh ? D3D12_RESOURCE_STATE_COMMON : D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_DEST);
    group.particleStateFresh = false;

    cmd->CopyBufferRegion(group.particleStateBuffer.Get(), 0,
        group.particleUploadBuffer.Get(), 0,
        sizeof(GPUParticleState) * ParticleGroup::kNumMaxInstance);

    DirectXCommon::TransitionBarrier(cmd, group.particleStateBuffer.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    cmd->SetComputeRootSignature(csEmitRootSignature_.Get());
    cmd->SetPipelineState(csEmitPipelineState_.Get());
    cmd->SetComputeRootConstantBufferView(0, group.emitterBuffer->GetGPUVirtualAddress());
    cmd->SetComputeRootUnorderedAccessView(1, group.particleStateBuffer->GetGPUVirtualAddress());
    cmd->Dispatch(1, 1, 1);

    D3D12_RESOURCE_BARRIER uavB { };
    uavB.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavB.UAV.pResource = group.particleStateBuffer.Get();
    cmd->ResourceBarrier(1, &uavB);

    // one-shot (frequency == 0) はディスパッチ後にリセット
    if (e->frequency == 0.0f) {
        e->emit = 0;
    }
}

void ParticleManager::DispatchUpdateCS(ParticleGroup& group, ID3D12GraphicsCommandList* cmd)
{
    cmd->SetComputeRootSignature(csRootSignature_.Get());
    cmd->SetPipelineState(csPipelineState_.Get());
    cmd->SetComputeRootConstantBufferView(0, csConstantsBuffer_->GetGPUVirtualAddress());
    cmd->SetComputeRootUnorderedAccessView(1, group.particleStateBuffer->GetGPUVirtualAddress());
    cmd->SetComputeRootUnorderedAccessView(2, group.instancingResource->GetGPUVirtualAddress());

    const UINT threadGroups = (ParticleGroup::kNumMaxInstance + 63) / 64;
    cmd->Dispatch(threadGroups, 1, 1);

    // particleStateBuffer・instancingResource の書き込み完了を保証
    D3D12_RESOURCE_BARRIER uavBs[2] { };
    uavBs[0].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBs[0].UAV.pResource = group.particleStateBuffer.Get();
    uavBs[1].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBs[1].UAV.pResource = group.instancingResource.Get();
    cmd->ResourceBarrier(2, uavBs);

    // instancingResource: UAV → NON_PIXEL_SHADER_RESOURCE（VS が SRV として読む）
    DirectXCommon::TransitionBarrier(cmd, group.instancingResource.Get(),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    group.instancingInSRV = true;
}

// ══════════════════════════════════════════════════════
// フレーム更新と描画
// ══════════════════════════════════════════════════════

void ParticleManager::Update(Camera* camera)
{
    constexpr float dt = GameConstants::kFrameDeltaTime;
    UpdateCSConstants(camera, dt);

    auto* cmd = dxCommon_->GetCommandList();
    for (auto& [name, group] : particleGroups_) {
        group.groupTime += dt;
        TransitionInstancingToUAV(group, cmd);
        ExpireAndRespawnSlots(group);
        FlushPendingSlotsToGPU(group, cmd);
        DispatchEmitCS(group, cmd, dt);
        DispatchUpdateCS(group, cmd);
    }
}

//  Draw: インスタンシング描画

void ParticleManager::Draw(Camera* camera)
{
    (void)camera;

    auto* cmd = dxCommon_->GetCommandList();

    cmd->SetGraphicsRootSignature(rootSignature_.Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->IASetVertexBuffers(0, 1, &quadVBV_);
    cmd->IASetIndexBuffer(&quadIBV_);

    SrvManager::GetInstance()->PreDraw();

    ID3D12PipelineState* activePSO = nullptr;

    for (auto& [name, group] : particleGroups_) {
        if (group.aliveCount == 0) {
            continue;
        }

        ID3D12PipelineState* pso = group.additiveBlend
            ? graphicsPipelineState_.Get()
            : graphicsPipelineStateAlpha_.Get();
        if (pso != activePSO) {
            cmd->SetPipelineState(pso);
            activePSO = pso;
        }

        cmd->SetGraphicsRootDescriptorTable(
            0, SrvManager::GetInstance()->GetGPUDescriptorHandle(group.srvIndex));

        D3D12_GPU_DESCRIPTOR_HANDLE texH = TextureManager::GetInstance()->GetSrvHandleGPU(group.textureFilePath);
        cmd->SetGraphicsRootDescriptorTable(1, texH);

        // instancingResource のスロットは非連続なため instance count を aliveCount に
        // 減らすには CS 側で alive スロットを詰める変更が必要現状は全 1024 を渡し、
        // シェーダー側で alive=0 の instance を early-out する
        cmd->DrawIndexedInstanced(6, ParticleGroup::kNumMaxInstance, 0, 0, 0);
    }
}

//  テクスチャ変更

void ParticleManager::SetTexture(const std::string& groupName,
    const std::string& textureFilePath)
{
    if (!particleGroups_.contains(groupName)) {
        return;
    }

    ParticleGroup& group = particleGroups_[groupName];
    group.textureFilePath = textureFilePath;
    TextureManager::GetInstance()->LoadTexture(textureFilePath);
}

void ParticleManager::SetAdditiveBlend(const std::string& name, bool additive)
{
    if (particleGroups_.contains(name)) {
        particleGroups_[name].additiveBlend = additive;
    }
}

bool ParticleManager::IsGroupAlive(const std::string& name) const
{
    auto it = particleGroups_.find(name);
    if (it == particleGroups_.end()) {
        return false;
    }
    const ParticleGroup& group = it->second;
    for (uint32_t i = 0; i < ParticleGroup::kNumMaxInstance; ++i) {
        if (group.groupTime < group.slotExpiry[i]) {
            return true;
        }
    }
    return false;
}
