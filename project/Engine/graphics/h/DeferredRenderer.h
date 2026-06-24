#pragma once
#include "DirectXCommon.h"
#include "SrvManager.h"
#include "Camera.h"
#include "WinApp.h"
#include "MakeAffine.h"
#include <wrl/client.h>

// ディファードレンダリング
//
// G-Buffer (Albedo/Normal/Material + 既存深度バッファ) にジオメトリを描画し、
// 全画面 Lighting パスでライティングを合成する。
//
// 使い方:
//   // 初期化
//   deferred->Initialize(dxCommon, srvManager);
//
//   // 毎フレーム
//   deferred->BeginGBuffer();          // G-Buffer RT に切り替え
//   /* 全オブジェクト描画 (GBuffer PSO) */
//   deferred->EndGBuffer();
//   deferred->ApplyLighting(camera);   // ライティングパス → バックバッファ
class DeferredRenderer {
public:
    static DeferredRenderer* GetInstance() {
        static DeferredRenderer inst;
        return &inst;
    }

    void Initialize(DirectXCommon* dxCommon, SrvManager* srvManager);

    void BeginGBuffer(); // G-Buffer RT に切り替え
    void EndGBuffer();   // G-Buffer RT → SRV へバリア遷移

    // ライティングパス（バックバッファへ書き出し）
    // cascadeShadowSrvIndex: CascadedShadowMap の Texture2DArray SRV インデックス
    void ApplyLighting(Camera* camera, uint32_t cascadeShadowSrvIndex,
                       const Matrix4x4& cascadeVP0,
                       const Matrix4x4& cascadeVP1,
                       const Matrix4x4& cascadeVP2,
                       float splitDist0, float splitDist1, float splitDist2,
                       float numCascades);

    bool  IsEnabled()    const { return enabled_; }
    void  SetEnabled(bool e)   { enabled_ = e; }

    // G-Buffer PSO 用ルートシグネチャを取得（外部 PSO 作成に使用）
    ID3D12RootSignature* GetGBufferRootSignature() const { return gbufferRS_.Get(); }
    ID3D12PipelineState* GetGBufferPSO()           const { return gbufferPSO_.Get(); }

    uint32_t GetAlbedoSrvIndex()   const { return albedoSrvIndex_; }
    uint32_t GetNormalSrvIndex()   const { return normalSrvIndex_; }
    uint32_t GetMaterialSrvIndex() const { return materialSrvIndex_; }

private:
    DeferredRenderer()  = default;
    ~DeferredRenderer() = default;
    DeferredRenderer(const DeferredRenderer&)            = delete;
    DeferredRenderer& operator=(const DeferredRenderer&) = delete;

    void CreateRT(UINT w, UINT h, DXGI_FORMAT fmt,
                  Microsoft::WRL::ComPtr<ID3D12Resource>& res,
                  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>& rtvHeap,
                  D3D12_CPU_DESCRIPTOR_HANDLE& rtvHandle,
                  uint32_t& srvIndex);

    void Barrier(ID3D12Resource* res,
                 D3D12_RESOURCE_STATES before,
                 D3D12_RESOURCE_STATES after);

    struct LightingParams {
        // 平行光源
        Vector4 lightColor;
        Vector3 lightDirection;
        float   lightIntensity;
        Vector3 ambientColor;
        float   ambientIntensity;
        // カメラ
        Vector3 cameraWorldPos;
        float   _pad0;
        // CSM
        Matrix4x4 cascadeVP[3];
        float     cascadeSplits[3];
        float     numCascades;
        float     _csmPad[3];
        // プロジェクション逆行列
        Matrix4x4 invViewProjection;
        float     screenW;
        float     screenH;
        float     _pad1[2];
    };

    DirectXCommon* dxCommon_   = nullptr;
    SrvManager*    srvManager_ = nullptr;
    bool           enabled_    = true;

    // G-Buffer レンダーターゲット
    Microsoft::WRL::ComPtr<ID3D12Resource>       albedoTex_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> albedoRtvHeap_;
    D3D12_CPU_DESCRIPTOR_HANDLE                  albedoRtvHandle_ = {};
    uint32_t                                     albedoSrvIndex_  = UINT32_MAX;

    Microsoft::WRL::ComPtr<ID3D12Resource>       normalTex_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> normalRtvHeap_;
    D3D12_CPU_DESCRIPTOR_HANDLE                  normalRtvHandle_ = {};
    uint32_t                                     normalSrvIndex_  = UINT32_MAX;

    Microsoft::WRL::ComPtr<ID3D12Resource>       materialTex_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> materialRtvHeap_;
    D3D12_CPU_DESCRIPTOR_HANDLE                  materialRtvHandle_ = {};
    uint32_t                                     materialSrvIndex_  = UINT32_MAX;

    // 深度バッファ SRV (DirectXCommon の既存深度バッファを共有)
    uint32_t depthSrvIndex_ = UINT32_MAX;

    // G-Buffer PSO
    Microsoft::WRL::ComPtr<ID3D12RootSignature> gbufferRS_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> gbufferPSO_;

    // Lighting PSO
    Microsoft::WRL::ComPtr<ID3D12RootSignature> lightingRS_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> lightingPSO_;

    // Lighting 定数バッファ
    Microsoft::WRL::ComPtr<ID3D12Resource> lightingCBRes_;
    LightingParams*                        lightingCBData_ = nullptr;

    // G-Buffer MRT 用 RTV ヒープ（3つ同時描画用）
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> gbufferRtvHeap_;
};
