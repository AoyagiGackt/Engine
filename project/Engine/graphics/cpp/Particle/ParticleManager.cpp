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

ParticleManager* ParticleManager::GetInstance()
{
    static ParticleManager instance;
    return &instance;
}

// ============================================================
//  初期化 / 終了
// ============================================================

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

// ============================================================
//  グループ生成
// ============================================================

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

    // ---- particleStateBuffer: DEFAULT heap, UAV ----
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

    // ---- particleUploadBuffer: UPLOAD heap, 常時マップ ----
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

    // ---- instancingResource: DEFAULT heap, UAV (CS が書く) ----
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

    // ---- SRV for instancingResource (VS が t0 として読む) ----
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

    // ---- エミッターバッファ (UPLOAD heap, 256 bytes, 常時マップ) ----
    group.emitterBuffer = dxCommon_->CreateBufferResource(256);
    group.emitterBuffer->Map(0, nullptr, reinterpret_cast<void**>(&group.emitterData));
    memset(group.emitterData, 0, sizeof(Emitter));
    group.emitterData->lifeTime = 1.0f; // デフォルト 1 秒
}

// ============================================================
//  スロット管理
// ============================================================

uint32_t ParticleManager::AllocateSlot(ParticleGroup& group)
{
    if (group.freeList.empty()) {
        return UINT32_MAX;
    }
    uint32_t slot = group.freeList.back();
    group.freeList.pop_back();
    return slot;
}

// ============================================================
//  Emit 系
// ============================================================

void ParticleManager::Emit(const std::string& name,
    const Vector3& position,
    const Vector3& velocity)
{
    EmitWithColor(name, position, velocity, { 1.0f, 1.0f, 1.0f, 1.0f }, 1.0f, 1.0f);
}

void ParticleManager::EmitWithColor(const std::string& name,
    const Vector3& position,
    const Vector3& velocity,
    const Vector4& color,
    float lifeTime,
    float scale,
    bool flicker)
{
    ENGINE_ASSERT(particleGroups_.contains(name));
    ParticleGroup& group = particleGroups_[name];

    uint32_t slot = AllocateSlot(group);

    if (slot == UINT32_MAX) {
        return;
    }

    GPUParticleState& p = group.particleUploadData[slot];
    p.position = position;
    p.lifeTime = lifeTime;
    p.velocity = velocity;
    p.currentTime = 0.0f;
    p.color = color;
    p.scale = { scale, scale, scale };
    p.rotateZ = 0.0f;
    p.alive = 1;
    p.curveFlag = flicker ? 2u : 0u;

    group.slotExpiry[slot] = group.groupTime + lifeTime + 0.1f;
    group.aliveCount++;
    group.pendingSlots.push_back(slot);
}

void ParticleManager::EmitEllipse(const std::string& name,
    const Vector3& position,
    const Vector3& velocity,
    const Vector4& color,
    float lifeTime,
    float scaleX,
    float scaleY)
{
    ENGINE_ASSERT(particleGroups_.contains(name));
    ParticleGroup& group = particleGroups_[name];

    uint32_t slot = AllocateSlot(group);

    if (slot == UINT32_MAX) {
        return;
    }

    GPUParticleState& p = group.particleUploadData[slot];
    p.position = position;
    p.lifeTime = lifeTime;
    p.velocity = velocity;
    p.currentTime = 0.0f;
    p.color = color;
    p.scale = { scaleX, scaleY, 1.0f };
    p.rotateZ = 0.0f;
    p.alive = 1;
    p.curveFlag = 0;

    group.slotExpiry[slot] = group.groupTime + lifeTime + 0.1f;
    group.aliveCount++;
    group.pendingSlots.push_back(slot);
}

void ParticleManager::EmitSlash(const std::string& name,
    const Vector3& position,
    float angle,
    const Vector4& color,
    float radius)
{
    ENGINE_ASSERT(particleGroups_.contains(name));
    ParticleGroup& group = particleGroups_[name];

    constexpr float kGlowLifeTime = 0.30f;
    constexpr float kCoreLifeTime = 0.20f;
    constexpr float kShardLifeTime = 0.25f;
    constexpr int kShardCount = 4;

    auto emitOne = [&](const Vector3& velocity, const Vector4& c, float lifeTime,
                       float scaleX, float scaleY) {
        uint32_t slot = AllocateSlot(group);
        if (slot == UINT32_MAX) {
            return;
        }

        GPUParticleState& p = group.particleUploadData[slot];
        p.position = position;
        p.lifeTime = lifeTime;
        p.velocity = velocity;
        p.currentTime = 0.0f;
        p.color = c;
        p.scale = { scaleX, scaleY, 1.0f };
        p.rotateZ = angle;
        p.alive = 1;
        p.curveFlag = 0;

        group.slotExpiry[slot] = group.groupTime + lifeTime + 0.1f;
        group.aliveCount++;
        group.pendingSlots.push_back(slot);
    };

    // 残光（太く淡い層）と芯（細く白に寄せた層）を重ねる
    Vector4 glow = { color.x, color.y, color.z, color.w * 0.35f };
    Vector4 core = { color.x * 0.4f + 0.6f, color.y * 0.4f + 0.6f,
        color.z * 0.4f + 0.6f, color.w };
    emitOne({ 0.0f, 0.0f, 0.0f }, glow, kGlowLifeTime, radius * 2.0f, radius * 0.55f);
    emitOne({ 0.0f, 0.0f, 0.0f }, core, kCoreLifeTime, radius * 1.9f, radius * 0.18f);

    // 斬線に沿って両端へ抜ける光片
    const Vector3 dir = { std::cos(angle), std::sin(angle), 0.0f };
    for (int i = 0; i < kShardCount; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(kShardCount - 1);
        float sign = (i % 2 == 0) ? 1.0f : -1.0f;
        float speed = radius * (3.0f + 3.0f * t) * sign;
        Vector4 c = { color.x, color.y, color.z, color.w * (0.8f - t * 0.3f) };
        emitOne({ dir.x * speed, dir.y * speed, 0.0f }, c, kShardLifeTime,
            radius * 0.6f, radius * 0.05f);
    }
}

void ParticleManager::EmitScatterLoop(const std::string& name,
    const Vector3& center, float radius,
    uint32_t count, const Vector4& color,
    float lifeTimeMin, float lifeTimeMax, float scale)
{
    ENGINE_ASSERT(particleGroups_.contains(name));
    ParticleGroup& group = particleGroups_[name];
    count = (std::min)(count, ParticleGroup::kNumMaxInstance);

    static std::mt19937 rng { std::random_device { }() };
    std::uniform_real_distribution<float> xzDist(-radius, radius);
    std::uniform_real_distribution<float> yDist(0.0f, 12.0f);
    std::uniform_real_distribution<float> lifeDist(lifeTimeMin, lifeTimeMax);
    std::uniform_real_distribution<float> velXZDist(-0.3f, 0.3f);
    std::uniform_real_distribution<float> velYDist(0.1f, 0.6f);

    memset(group.particleUploadData, 0, sizeof(GPUParticleState) * ParticleGroup::kNumMaxInstance);
    group.slotExpiry.fill(0.0f);
    group.aliveCount = 0;
    group.freeList.clear();
    for (uint32_t i = ParticleGroup::kNumMaxInstance; i-- > count;) {
        group.freeList.push_back(i);
    }
    group.pendingSlots.clear();

    for (uint32_t i = 0; i < count; ++i) {
        float lifeTime = lifeDist(rng);
        std::uniform_real_distribution<float> timeDist(0.0f, lifeTime);
        float currentTime = timeDist(rng);

        GPUParticleState& p = group.particleUploadData[i];
        p.position = { center.x + xzDist(rng), center.y + yDist(rng), center.z + xzDist(rng) };
        p.lifeTime = lifeTime;
        p.velocity = { velXZDist(rng), velYDist(rng), velXZDist(rng) };
        p.currentTime = currentTime;
        p.color = color;
        p.scale = { scale, scale, scale };
        p.rotateZ = 0.0f;
        p.alive = 1;
        p.curveFlag = 0;

        group.slotExpiry[i] = group.groupTime + (lifeTime - currentTime) + 0.1f;
        group.aliveCount++;
    }

    group.needsInit = true;

    group.autoRespawn = true;
    group.respawnConfig = { center, radius, lifeTimeMin, lifeTimeMax, color, scale, count };
}

void ParticleManager::EmitBurst(const std::string& name,
    const Vector3& position,
    const Vector4& color,
    uint32_t count,
    float lifeTime,
    float scale,
    bool flicker)
{
    ENGINE_ASSERT(particleGroups_.contains(name));
    ParticleGroup& group = particleGroups_[name];

    count = (std::min)(count, ParticleGroup::kNumMaxInstance);

    // 全スロットをリセット（re-emit 時に前の状態を消す）
    memset(group.particleUploadData, 0,
        sizeof(GPUParticleState) * ParticleGroup::kNumMaxInstance);
    group.slotExpiry.fill(0.0f);
    group.aliveCount = 0;
    group.freeList.clear();
    for (uint32_t i = ParticleGroup::kNumMaxInstance; i-- > count;) {
        group.freeList.push_back(i);
    }

    for (uint32_t i = 0; i < count; ++i) {
        GPUParticleState& p = group.particleUploadData[i];
        // ランダムにばら撒く（初期化時と同様の範囲）
        static std::mt19937 rng { std::random_device { }() };
        std::uniform_real_distribution<float> distXZ(-20.0f, 20.0f);
        std::uniform_real_distribution<float> distY(0.0f, 12.0f);
        Vector3 randOffset = { distXZ(rng), distY(rng), distXZ(rng) };

        p.position = { position.x + randOffset.x, position.y + randOffset.y, position.z + randOffset.z };
        p.lifeTime = lifeTime;
        p.velocity = { 0.0f, 0.0f, 0.0f };
        p.currentTime = 0.0f;
        p.color = color;
        p.scale = { scale, scale, scale };
        p.rotateZ = 0.0f;
        p.alive = 1;
        p.curveFlag = flicker ? 2u : 0u;
        group.slotExpiry[i] = group.groupTime + lifeTime + 0.1f;
        group.aliveCount++;
    }

    group.needsInit = true;
    group.pendingSlots.clear();
    for (uint32_t i = 0; i < count; ++i) {
        group.pendingSlots.push_back(i);
    }
}

void ParticleManager::EmitHitStar(const std::string& name,
    const Vector3& position,
    const Vector4& color)
{
    ENGINE_ASSERT(particleGroups_.contains(name));
    ParticleGroup& group = particleGroups_[name];

    static std::default_random_engine engine { std::random_device { }() };
    std::uniform_real_distribution<float> rotDist(-std::numbers::pi_v<float>, std::numbers::pi_v<float>);
    std::uniform_real_distribution<float> scaleYDist(0.15f, 0.7f);
    std::uniform_real_distribution<float> speedDist(0.5f, 2.5f);
    std::uniform_real_distribution<float> lifeDist(0.2f, 0.5f);

    const int kCount = 8;

    for (int i = 0; i < kCount; ++i) {
        uint32_t slot = AllocateSlot(group);

        if (slot == UINT32_MAX) {
            break;
        }

        float rotAngle = rotDist(engine);
        float velAngle = rotDist(engine);
        float scaleY = scaleYDist(engine);
        float speed = speedDist(engine) * GameConstants::kFrameDeltaTime;
        float lifeTime = lifeDist(engine);

        Vector3 vel = { std::cos(velAngle) * speed, std::sin(velAngle) * speed, 0.0f };

        GPUParticleState& p = group.particleUploadData[slot];
        p.position = position;
        p.lifeTime = lifeTime;
        p.velocity = vel;
        p.currentTime = 0.0f;
        p.color = color;
        p.scale = { 0.04f, scaleY, 1.0f };
        p.rotateZ = rotAngle;
        p.alive = 1;
        p.curveFlag = 0;

        group.slotExpiry[slot] = group.groupTime + lifeTime + 0.1f;
        group.aliveCount++;
        group.pendingSlots.push_back(slot);
    }
}

void ParticleManager::EmitGravity(const std::string& name, const Vector3& position,
    const Vector3& velocity, const Vector4& color,
    float lifeTime, float scale)
{
    ENGINE_ASSERT(particleGroups_.contains(name));
    ParticleGroup& group = particleGroups_[name];

    uint32_t slot = AllocateSlot(group);
    if (slot == UINT32_MAX) {
        return;
    }

    GPUParticleState& p = group.particleUploadData[slot];
    p.position = position;
    p.lifeTime = lifeTime;
    p.velocity = velocity;
    p.currentTime = 0.0f;
    p.color = color;
    p.scale = { scale, scale, scale };
    p.rotateZ = 0.0f;
    p.alive = 1;
    p.curveFlag = 3;

    group.slotExpiry[slot] = group.groupTime + lifeTime + 0.1f;
    group.aliveCount++;
    group.pendingSlots.push_back(slot);
}

void ParticleManager::EmitRing(const std::string& name, const Vector3& position,
    float speed, const Vector4& color,
    uint32_t count, float lifeTime, float scale)
{
    ENGINE_ASSERT(particleGroups_.contains(name));
    ParticleGroup& group = particleGroups_[name];
    count = (std::min)(count, ParticleGroup::kNumMaxInstance);

    const float kTwoPi = 2.0f * std::numbers::pi_v<float>;

    for (uint32_t i = 0; i < count; ++i) {
        uint32_t slot = AllocateSlot(group);
        if (slot == UINT32_MAX) {
            break;
        }

        float angle = kTwoPi * static_cast<float>(i) / static_cast<float>(count);

        GPUParticleState& p = group.particleUploadData[slot];
        p.position = position;
        p.lifeTime = lifeTime;
        p.velocity = { std::cos(angle) * speed, std::sin(angle) * speed, 0.0f };
        p.currentTime = 0.0f;
        p.color = color;
        p.scale = { scale, scale, scale };
        p.rotateZ = 0.0f;
        p.alive = 1;
        p.curveFlag = 0;

        group.slotExpiry[slot] = group.groupTime + lifeTime + 0.1f;
        group.aliveCount++;
        group.pendingSlots.push_back(slot);
    }
}

void ParticleManager::EmitTrail(const std::string& name, const Vector3& position,
    const Vector4& color, float scale, float lifeTime)
{
    ENGINE_ASSERT(particleGroups_.contains(name));
    ParticleGroup& group = particleGroups_[name];

    uint32_t slot = AllocateSlot(group);
    if (slot == UINT32_MAX) {
        return;
    }

    GPUParticleState& p = group.particleUploadData[slot];
    p.position = position;
    p.lifeTime = lifeTime;
    p.velocity = { 0.0f, 0.0f, 0.0f };
    p.currentTime = 0.0f;
    p.color = color;
    p.scale = { scale, scale, scale };
    p.rotateZ = 0.0f;
    p.alive = 1;
    p.curveFlag = 4; // スケール縮小フェードアウト

    group.slotExpiry[slot] = group.groupTime + lifeTime + 0.1f;
    group.aliveCount++;
    group.pendingSlots.push_back(slot);
}

// ============================================================
//  Update: CS ディスパッチ（フェーズ分割版）
// ============================================================

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

// ============================================================
//  Draw: インスタンシング描画
// ============================================================

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

// ============================================================
//  テクスチャ変更
// ============================================================

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

// ============================================================
//  グラフィックスルートシグネチャ
// ============================================================

void ParticleManager::CreateRootSignature()
{
    ID3D12Device* device = dxCommon_->GetDevice();

    D3D12_DESCRIPTOR_RANGE rangeT0[1] { };
    rangeT0[0].BaseShaderRegister = 0;
    rangeT0[0].NumDescriptors = 1;
    rangeT0[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    rangeT0[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE rangeT1[1] { };
    rangeT1[0].BaseShaderRegister = 1;
    rangeT1[0].NumDescriptors = 1;
    rangeT1[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    rangeT1[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // slot 0: t0 (VS が読む instancing StructuredBuffer)
    // slot 1: t1 (PS が読む Texture2D)
    // 未使用の CBV スロット (b0/b1) は宣言しない - GBV が null アドレスを検出してクラッシュするため
    D3D12_ROOT_PARAMETER rootParameters[2] { };
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rootParameters[0].DescriptorTable.pDescriptorRanges = rangeT0;
    rootParameters[0].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[1].DescriptorTable.pDescriptorRanges = rangeT1;
    rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;

    D3D12_STATIC_SAMPLER_DESC sampler { };
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC desc { };
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    desc.pParameters = rootParameters;
    desc.NumParameters = _countof(rootParameters);
    desc.pStaticSamplers = &sampler;
    desc.NumStaticSamplers = 1;

    ComPtr<ID3DBlob> sigBlob, errBlob;
    HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1,
        &sigBlob, &errBlob);
    ENGINE_ASSERT(SUCCEEDED(hr));
    hr = device->CreateRootSignature(0, sigBlob->GetBufferPointer(),
        sigBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature_));
    ENGINE_ASSERT(SUCCEEDED(hr));
}

// ============================================================
//  CS ルートシグネチャ
// ============================================================

void ParticleManager::CreateCSRootSignature()
{
    ID3D12Device* device = dxCommon_->GetDevice();

    D3D12_ROOT_PARAMETER params[3] { };
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[0].Descriptor.ShaderRegister = 0; // b0
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[1].Descriptor.ShaderRegister = 0; // u0
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[2].Descriptor.ShaderRegister = 1; // u1

    D3D12_ROOT_SIGNATURE_DESC desc { };
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    desc.pParameters = params;
    desc.NumParameters = _countof(params);

    ComPtr<ID3DBlob> sigBlob, errBlob;
    HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1,
        &sigBlob, &errBlob);
    ENGINE_ASSERT(SUCCEEDED(hr));
    device->CreateRootSignature(0, sigBlob->GetBufferPointer(),
        sigBlob->GetBufferSize(), IID_PPV_ARGS(&csRootSignature_));
}

// ============================================================
//  CS パイプラインステート
// ============================================================

void ParticleManager::CreateQuadGeometry()
{
    struct Vertex {
        float pos[4];
        float uv[2];
        float nrm[3];
    };

    Vertex verts[4] = {
        { { -0.5f, 0.5f, 0.f, 1.f }, { 0.f, 0.f }, { 0.f, 0.f, -1.f } },
        { { 0.5f, 0.5f, 0.f, 1.f }, { 1.f, 0.f }, { 0.f, 0.f, -1.f } },
        { { 0.5f, -0.5f, 0.f, 1.f }, { 1.f, 1.f }, { 0.f, 0.f, -1.f } },
        { { -0.5f, -0.5f, 0.f, 1.f }, { 0.f, 1.f }, { 0.f, 0.f, -1.f } },
    };
    uint32_t indices[6] = { 0, 1, 2, 0, 2, 3 };

    quadVertexBuffer_ = dxCommon_->CreateBufferResource(sizeof(verts));
    void* mapped = nullptr;
    quadVertexBuffer_->Map(0, nullptr, &mapped);
    memcpy(mapped, verts, sizeof(verts));
    quadVertexBuffer_->Unmap(0, nullptr);

    quadVBV_.BufferLocation = quadVertexBuffer_->GetGPUVirtualAddress();
    quadVBV_.SizeInBytes = sizeof(verts);
    quadVBV_.StrideInBytes = sizeof(Vertex);

    quadIndexBuffer_ = dxCommon_->CreateBufferResource(sizeof(indices));
    quadIndexBuffer_->Map(0, nullptr, &mapped);
    memcpy(mapped, indices, sizeof(indices));
    quadIndexBuffer_->Unmap(0, nullptr);

    quadIBV_.BufferLocation = quadIndexBuffer_->GetGPUVirtualAddress();
    quadIBV_.SizeInBytes = sizeof(indices);
    quadIBV_.Format = DXGI_FORMAT_R32_UINT;
}

void ParticleManager::CreateCSPipelineState()
{
    Microsoft::WRL::ComPtr<IDxcBlob> csBlob = dxCommon_->CompileShader(
        L"Resources/shaders/Particle/ParticleUpdate.CS.hlsl", L"cs_6_0");
    ENGINE_ASSERT(csBlob);

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc { };
    psoDesc.pRootSignature = csRootSignature_.Get();
    psoDesc.CS = { csBlob->GetBufferPointer(), csBlob->GetBufferSize() };
    HRESULT hr = dxCommon_->GetDevice()->CreateComputePipelineState(
        &psoDesc, IID_PPV_ARGS(&csPipelineState_));
    ENGINE_ASSERT(SUCCEEDED(hr));
}

// ============================================================
//  EmitParticle CS ルートシグネチャ / パイプライン
// ============================================================

void ParticleManager::CreateCSEmitRootSignature()
{
    ID3D12Device* device = dxCommon_->GetDevice();

    D3D12_ROOT_PARAMETER params[2] { };
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[0].Descriptor.ShaderRegister = 0; // b0 : EmitConstants
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[1].Descriptor.ShaderRegister = 0; // u0 : gParticles

    D3D12_ROOT_SIGNATURE_DESC desc { };
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    desc.pParameters = params;
    desc.NumParameters = _countof(params);

    ComPtr<ID3DBlob> sigBlob, errBlob;
    HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1,
        &sigBlob, &errBlob);
    ENGINE_ASSERT(SUCCEEDED(hr));
    device->CreateRootSignature(0, sigBlob->GetBufferPointer(),
        sigBlob->GetBufferSize(), IID_PPV_ARGS(&csEmitRootSignature_));
}

void ParticleManager::CreateCSEmitPipelineState()
{
    Microsoft::WRL::ComPtr<IDxcBlob> csBlob = dxCommon_->CompileShader(
        L"Resources/shaders/Particle/EmitParticle.CS.hlsl", L"cs_6_0");
    ENGINE_ASSERT(csBlob);

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc { };
    psoDesc.pRootSignature = csEmitRootSignature_.Get();
    psoDesc.CS = { csBlob->GetBufferPointer(), csBlob->GetBufferSize() };
    HRESULT hr = dxCommon_->GetDevice()->CreateComputePipelineState(
        &psoDesc, IID_PPV_ARGS(&csEmitPipelineState_));
    ENGINE_ASSERT(SUCCEEDED(hr));
}

Emitter* ParticleManager::GetEmitter(const std::string& name)
{
    ENGINE_ASSERT(particleGroups_.contains(name));
    return particleGroups_[name].emitterData;
}

// ============================================================
//  グラフィックスパイプラインステート
// ============================================================

void ParticleManager::CreatePipelineState()
{
    ID3D12Device* device = dxCommon_->GetDevice();

    Microsoft::WRL::ComPtr<IDxcBlob> vsBlob = dxCommon_->CompileShader(
        L"Resources/shaders/Particle/Particle.VS.hlsl", L"vs_6_0");
    Microsoft::WRL::ComPtr<IDxcBlob> psBlob = dxCommon_->CompileShader(
        L"Resources/shaders/Particle/Particle.PS.hlsl", L"ps_6_0");

    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
            D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0,
            D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
            D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc { };
    psoDesc.pRootSignature = rootSignature_.Get();
    psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
    psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
    psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };

    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
    psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    // RT のアルファをパーティクルのフェードアウトで上書きしない（黒くなる原因の修正）
    psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ZERO;
    psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;

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

    HRESULT hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&graphicsPipelineState_));
    ENGINE_ASSERT(SUCCEEDED(hr));

    // Alpha blend variant: DestBlend を INV_SRC_ALPHA に変えるだけ
    psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&graphicsPipelineStateAlpha_));
    ENGINE_ASSERT(SUCCEEDED(hr));
}
