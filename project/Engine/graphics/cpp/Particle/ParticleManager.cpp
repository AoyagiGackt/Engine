/**
 * @file ParticleManager.cpp
 * @brief GPUで更新するパーティクル（ParticleManager）の資源と更新の実装
 */
#include "ParticleManager.h"
#include "PipelineHelper.h"
#include "ResourceAlignment.h"
#include "EngineAssert.h"
#include "EngineConstants.h"
#include "TextureManager.h"
#include <cmath>
#include <d3dx12.h>
#include <numbers>
#include <random>

namespace engine::graphics {
namespace {

// GPUパーティクルの処理単位と四角形インデックス
constexpr unsigned int kComputeThreadsPerGroup = 64;
constexpr unsigned int kQuadIndexCount = 6;

}

using namespace Microsoft::WRL;

// ══════════════════════════════════════════════════════
// 初期化とグループ管理
// ══════════════════════════════════════════════════════

/** @brief ParticleManager の唯一のインスタンスを返す */
ParticleManager* ParticleManager::GetInstance() {
    static ParticleManager sInstance;
    return &sInstance;
}

//  初期化 / 終了

/** @brief 描画用とコンピュート用のルートシグネチャ・PSO・板ポリゴンを作成して初期化する */
void ParticleManager::Initialize(DirectXCommon* dxCommon) {
    ENGINE_ASSERT(dxCommon);
    dxCommon_ = dxCommon;

    CreateRootSignature();
    CreatePipelineState();
    CreateCSRootSignature();
    CreateCSPipelineState();
    CreateCSEmitRootSignature();
    CreateCSEmitPipelineState();

    // CS 定数バッファ (UPLOAD heap, 256 bytes, 常時マップ)
    csConstantsBuffer_ = dxCommon_->CreateBufferResource(kConstantBufferAlignment);
    csConstantsBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&csConstantsData_));

    CreateQuadGeometry();
}

/** @brief 全グループを破棄し GPU リソースを解放する */
void ParticleManager::Finalize() {
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

/** @brief 名前とテクスチャを指定してパーティクルグループを作る（同じ名前は作れない） */
void ParticleManager::CreateParticleGroup(const std::string& name,
    const std::string& textureFilePath) {
    ENGINE_ASSERT(!particleGroups_.contains(name));

    ParticleGroup& group = particleGroups_[name];
    group.textureFilePath = textureFilePath;

    TextureManager::GetInstance()->LoadTexture(textureFilePath);

    CreateParticleStateBuffers(group);
    CreateParticleInstancingResource(group);
    InitParticleGroupState(group);
}

/** @brief パーティクルの状態を持つバッファを生成する */
void ParticleManager::CreateParticleStateBuffers(ParticleGroup& group) {
    ID3D12Device* device = dxCommon_->GetDevice();
    const UINT64 stateSize = sizeof(GPUParticleState) * ParticleGroup::kNumMaxInstance;

    // particleStateBuffer: DEFAULT heap, UAV
    group.particleStateBuffer = pipelineHelper::CreateUnorderedAccessBuffer(device, stateSize);

    // particleUploadBuffer: UPLOAD heap, 常時マップ
    {
        group.particleUploadBuffer = pipelineHelper::CreateUploadBuffer(device, stateSize);
        const HRESULT hr = group.particleUploadBuffer->Map(0, nullptr,
            reinterpret_cast<void**>(&group.particleUploadData));
        ENGINE_ASSERT(SUCCEEDED(hr));
        memset(group.particleUploadData, 0, static_cast<size_t>(stateSize));
    }
}

/** @brief パーティクルのインスタンス描画用のリソースを生成する */
void ParticleManager::CreateParticleInstancingResource(ParticleGroup& group) {
    ID3D12Device* device = dxCommon_->GetDevice();
    const UINT64 instancSize = sizeof(ParticleForGPU) * ParticleGroup::kNumMaxInstance;

    // instancingResource: DEFAULT heap, UAV (CS が書く)
    group.instancingResource = pipelineHelper::CreateUnorderedAccessBuffer(device, instancSize);

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

/** @brief 1つのグループのパーティクルの状態とインスタンス用のリソースを初期化する */
void ParticleManager::InitParticleGroupState(ParticleGroup& group) {
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
    group.emitterBuffer = dxCommon_->CreateBufferResource(kConstantBufferAlignment);
    group.emitterBuffer->Map(0, nullptr, reinterpret_cast<void**>(&group.emitterData));
    memset(group.emitterData, 0, sizeof(Emitter));
    group.emitterData->lifeTime = 1.0f; // デフォルト 1 秒
}

//  スロット管理

/** @brief グループの空きスロットを1つ取り出す（空きが無ければ UINT32_MAX） */
uint32_t ParticleManager::AllocateSlot(ParticleGroup& group) {
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

/** @brief カメラからビルボード行列を作り、コンピュートシェーダー用の定数を更新する */
void ParticleManager::UpdateCSConstants(Camera* camera, float dt) {
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

/** @brief インスタンス用のリソースを UAV の状態へ遷移させる */
void ParticleManager::TransitionInstancingToUAV(ParticleGroup& group, ID3D12GraphicsCommandList* cmd) {
    if (!group.instancingInSRV) {
        return;
    }
    DirectXCommon::TransitionBarrier(cmd, group.instancingResource.Get(),
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    group.instancingInSRV = false;
}

/** @brief 寿命の尽きた枠を空け、出し直す枠を割り当てる */
void ParticleManager::ExpireAndRespawnSlots(ParticleGroup& group) {
    // 自動で出し直さないグループは、寿命を過ぎたスロットを空き一覧へ戻すだけ
    if (!group.autoRespawn) {
        for (uint32_t i = 0; i < ParticleGroup::kNumMaxInstance; ++i) {
            // 期限 0 は使っていない枠の印
            if (group.slotExpiry[i] > 0.0f && group.groupTime >= group.slotExpiry[i]) {
                group.freeList.push_back(i);
                group.slotExpiry[i] = 0.0f;
                group.aliveCount--;
            }
        }
        return;
    }

    // 出し直す位置・寿命・速度のばらつき（ループ開始時の設定どおり）
    static std::mt19937 sRng { std::random_device { }() };
    const ParticleGroup::RespawnConfig& cfg = group.respawnConfig;
    std::uniform_real_distribution<float> xzDist(-cfg.radius, cfg.radius);
    std::uniform_real_distribution<float> yDist(0.0f, kScatterHeight);
    std::uniform_real_distribution<float> lifeDist(cfg.lifeTimeMin, cfg.lifeTimeMax);
    std::uniform_real_distribution<float> velXZDist(-kScatterDriftXZ, kScatterDriftXZ);
    std::uniform_real_distribution<float> velYDist(kScatterRiseMin, kScatterRiseMax);

    // 寿命を過ぎたスロットだけを新しい粒で埋め、GPU へ送る対象にする
    for (uint32_t i = 0; i < cfg.count; ++i) {
        // 使っていない枠と、まだ寿命が残っている枠は触らない
        if (group.slotExpiry[i] <= 0.0f || group.groupTime < group.slotExpiry[i]) {
            continue;
        }
        float lifeTime = lifeDist(sRng);
        GPUParticleState& p = group.particleUploadData[i];
        // 中心のまわりの円柱状の範囲に置き、横に少し流れながら上へ昇らせる
        p.position = { cfg.center.x + xzDist(sRng), cfg.center.y + yDist(sRng), cfg.center.z + xzDist(sRng) };
        p.lifeTime = lifeTime;
        p.velocity = { velXZDist(sRng), velYDist(sRng), velXZDist(sRng) };
        // 経過時間を 0 に戻し、色と大きさは設定どおり、回転なし・直線の動きで生き返らせる
        p.currentTime = 0.0f;
        p.color = cfg.color;
        p.scale = { cfg.scale, cfg.scale, cfg.scale };
        p.rotateZ = 0.0f;
        p.alive = 1;
        p.curveFlag = static_cast<uint32_t>(ParticleMotion::kLinear);
        // GPU 側の消滅処理を待てるよう、寿命に少し余裕を足して次の期限にする
        group.slotExpiry[i] = group.groupTime + lifeTime + kSlotExpiryMargin;
        group.pendingSlots.push_back(i);
    }
}

/** @brief CPUで更新した枠の内容をGPUへ転送する */
void ParticleManager::FlushPendingSlotsToGPU(ParticleGroup& group, ID3D12GraphicsCommandList* cmd) {
    if (!group.needsInit && group.pendingSlots.empty()) {
        return;
    }

    // バッファは COMMON で作成される（D3D12 #1328 対策）
    PrepareParticleCopy(group, cmd);

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

/** @brief 発生頻度(frequency)に応じて発射タイミングを判定し、来ていればスロットの有効期限を延長した上で */
void ParticleManager::DispatchEmitCS(ParticleGroup& group, ID3D12GraphicsCommandList* cmd, float dt) {
    // 発生間隔ごとに発射の合図を立てる（間隔0なら一度きりの発射）
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
    const float maxExpiry = group.groupTime + e->lifeTime + kSlotExpiryMargin;
    for (uint32_t i = 0; i < count; ++i) {
        group.slotExpiry[i] = maxExpiry;
    }

    // 発射前にステートバッファ全体をアップロード（GPU キャッシュをフラッシュ）
    PrepareParticleCopy(group, cmd);

    cmd->CopyBufferRegion(group.particleStateBuffer.Get(), 0,
        group.particleUploadBuffer.Get(), 0,
        sizeof(GPUParticleState) * ParticleGroup::kNumMaxInstance);

    DirectXCommon::TransitionBarrier(cmd, group.particleStateBuffer.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    // 発射用のコンピュートシェーダーで新しい粒を書き込む
    cmd->SetComputeRootSignature(csEmitRootSignature_.Get());
    cmd->SetPipelineState(csEmitPipelineState_.Get());
    cmd->SetComputeRootConstantBufferView(0, group.emitterBuffer->GetGPUVirtualAddress());
    cmd->SetComputeRootUnorderedAccessView(1, group.particleStateBuffer->GetGPUVirtualAddress());
    cmd->Dispatch(1, 1, 1);

    // 書き込みが終わってから更新処理が読むよう UAV バリアを挟む
    D3D12_RESOURCE_BARRIER uavB { };
    uavB.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavB.UAV.pResource = group.particleStateBuffer.Get();
    cmd->ResourceBarrier(1, &uavB);

    // one-shot (frequency == 0) はディスパッチ後にリセット
    if (e->frequency == 0.0f) {
        e->emit = 0;
    }
}

/** @brief パーティクルの更新用のコンピュートシェーダーを実行する */
void ParticleManager::DispatchUpdateCS(ParticleGroup& group, ID3D12GraphicsCommandList* cmd) {
    cmd->SetComputeRootSignature(csRootSignature_.Get());
    cmd->SetPipelineState(csPipelineState_.Get());
    cmd->SetComputeRootConstantBufferView(0, csConstantsBuffer_->GetGPUVirtualAddress());
    cmd->SetComputeRootUnorderedAccessView(1, group.particleStateBuffer->GetGPUVirtualAddress());
    cmd->SetComputeRootUnorderedAccessView(2, group.instancingResource->GetGPUVirtualAddress());

    const UINT threadGroups = (ParticleGroup::kNumMaxInstance + kComputeThreadsPerGroup - 1) / kComputeThreadsPerGroup;
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

/** @brief 全グループの寿命切れと再生成を処理し、コンピュートシェーダーで粒子を進める */
void ParticleManager::Update(Camera* camera) {
    const float deltaSeconds = engineConstants::kFrameDeltaTime;
    UpdateCSConstants(camera, deltaSeconds);

    auto* cmd = dxCommon_->GetCommandList();
    for (auto& [name, group] : particleGroups_) {
        group.groupTime += deltaSeconds;
        TransitionInstancingToUAV(group, cmd);
        ExpireAndRespawnSlots(group);
        FlushPendingSlotsToGPU(group, cmd);
        DispatchEmitCS(group, cmd, deltaSeconds);
        DispatchUpdateCS(group, cmd);
    }
}

//  Draw: インスタンシング描画

/** @brief 全グループの粒子をインスタンシングで描画する */
void ParticleManager::Draw(Camera* camera) {
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
            pipelineHelper::particleRootSlot::kInstances, SrvManager::GetInstance()->GetGPUDescriptorHandle(group.srvIndex));

        D3D12_GPU_DESCRIPTOR_HANDLE texH = TextureManager::GetInstance()->GetSrvHandleGPU(group.textureFilePath);
        cmd->SetGraphicsRootDescriptorTable(pipelineHelper::particleRootSlot::kTexture, texH);

        // instancingResource のスロットは非連続なため instance count を aliveCount に
        // 減らすには CS 側で alive スロットを詰める変更が必要現状は全 1024 を渡し、
        // シェーダー側で alive=0 の instance を early-out する
        cmd->DrawIndexedInstanced(kQuadIndexCount, ParticleGroup::kNumMaxInstance, 0, 0, 0);
    }
}

//  テクスチャ変更

/** @brief 指定グループのテクスチャを差し替える */
void ParticleManager::SetTexture(const std::string& groupName,
    const std::string& textureFilePath) {
    if (!particleGroups_.contains(groupName)) {
        return;
    }

    ParticleGroup& group = particleGroups_[groupName];
    group.textureFilePath = textureFilePath;
    TextureManager::GetInstance()->LoadTexture(textureFilePath);
}

/** @brief グループの加算合成・アルファ合成を切り替える */
void ParticleManager::SetAdditiveBlend(const std::string& name, bool additive) {
    if (particleGroups_.contains(name)) {
        particleGroups_[name].additiveBlend = additive;
    }
}

/** @brief 指定グループに生存中のパーティクルが1粒以上存在するかを返す */
bool ParticleManager::IsGroupAlive(const std::string& name) const {
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

/** @brief 粒子の状態バッファをコピー先へ遷移し、初回の印を下ろす */
void ParticleManager::PrepareParticleCopy(ParticleGroup& group, ID3D12GraphicsCommandList* cmd) {
    // 初回だけ COMMON から、次回以降は UAV の状態からコピー先へ遷移する
    DirectXCommon::TransitionBarrier(cmd, group.particleStateBuffer.Get(),
        group.particleStateFresh ? D3D12_RESOURCE_STATE_COMMON : D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_DEST);
    group.particleStateFresh = false;
}

} // namespace engine::graphics
