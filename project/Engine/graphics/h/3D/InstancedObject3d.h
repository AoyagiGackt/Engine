/**
 * @file InstancedObject3d.h
 * @brief GPUインスタンシングによる複数オブジェクトの一括描画を行うファイル
 */
#pragma once
#include "DirectXCommon.h"
#include "MakeAffine.h"
#include "Model.h"
#include "SrvManager.h"
#include <vector>
#include <wrl/client.h>
namespace engine::graphics {

/**
 * @brief 同一モデルを1回のドローコールでまとめて描画するクラス
 * @note インスタンスごとのワールド行列をGPUバッファに書き込み、GPU Instancingで描画する
 */
class InstancedObject3d {
public:
    static const uint32_t kMaxInstances = 1024;

    void Initialize(engine::DirectXCommon* dxCommon, SrvManager* srvManager,
        const std::string& modelPath, uint32_t maxInstances = kMaxInstances);

    void SetInstanceCount(uint32_t count) { instanceCount_ = count; }
    void SetInstanceTransform(uint32_t i, const Transform& t);
    void SetInstanceMatrix(uint32_t i, const Matrix4x4& world);

    // カメラ情報を更新（毎フレーム）
    void Update(const Matrix4x4& viewProj, const Matrix4x4& lightVP, const Vector3& cameraPos);

    void Draw();

private:
    struct CameraVPLayout {
        Matrix4x4 viewProjection;
        Matrix4x4 lightVP;
        Vector3 cameraWorldPos;
        float _pad;
    };

    void CreateRootSignatureAndPSO();

    engine::DirectXCommon* dxCommon_ = nullptr;
    SrvManager* srvManager_ = nullptr;
    Model* model_ = nullptr;

    uint32_t maxInstances_ = 0;
    uint32_t instanceCount_ = 0;

    // インスタンスワールド行列バッファ (StructuredBuffer → SRV)
    // ダブルバッファリング: CPU が次フレームを書き込む間 GPU が前フレームを読む
    static constexpr UINT kFrameLatency = 2;
    Microsoft::WRL::ComPtr<ID3D12Resource> instanceBuf_[kFrameLatency];
    Matrix4x4* instanceBufData_[kFrameLatency] = { };
    uint32_t instanceSrvIndex_[kFrameLatency] = { UINT32_MAX, UINT32_MAX };
    uint32_t frameIdx_ = 0;

    // カメラ VP 定数バッファ
    Microsoft::WRL::ComPtr<ID3D12Resource> cameraVPBuf_;
    CameraVPLayout* cameraVPData_ = nullptr;

    // マテリアル定数バッファ
    Microsoft::WRL::ComPtr<ID3D12Resource> materialBuf_;
    void* materialBufData_ = nullptr;

    // ダミーバッファ（使用しないスロット用）
    Microsoft::WRL::ComPtr<ID3D12Resource> dummyBuf_;

    // 1×1×6 fallback TextureCube for slot 5 (shader declares TextureCube t2; useCubemap=0)
    Microsoft::WRL::ComPtr<ID3D12Resource> fallbackCubemap_;
    uint32_t fallbackCubemapSrvIdx_ = UINT32_MAX;

    // PSO
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rs_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pso_;
};

} // namespace engine::graphics
