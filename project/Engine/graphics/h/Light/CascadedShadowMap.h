/**
 * @file CascadedShadowMap.h
 * @brief CascadedShadowMapの描画資源とGPU処理の管理に関する公開型と操作インターフェースを定義するファイル
 */
#pragma once
#include "DirectXCommon.h"
#include "MakeAffine.h"
#include "SrvManager.h"
#include <wrl/client.h>
namespace engine::graphics {

// Cascaded Shadow Maps (CSM) - 3カスケード深度シャドウ
// 使い方
//   csm->Initialize(dxCommon, srvManager);
//   // 毎フレーム
//   csm->Update(lightDir);                        // 3カスケードの VP 行列を更新
//   Object3d::SetLightViewProjection(csm->GetCascadeVP(0)); // VS の LightVP をカスケード 0 に統一
//   for (uint32_t i = 0; i < CascadedShadowMap::kNumCascades; ++i) {
//       csm->BeginCascade(cmd, i);
//       /* 全オブジェクトの DrawShadow() */
//       csm->EndCascade(cmd);
//   }
//   csm->SetShadowMapSRV(cmd, srvManager);        // スロット 4 に Texture2DArray をバインド
//   csm->SetCascadeDataCBV(cmd, slotIdx);          // スロット 8(or 9) にカスケード定数をバインド
/**
 * @brief CascadedShadowMap に関する型を提供する
 * @details CascadedShadowMap が扱うデータと操作の責務をまとめる
 */
class CascadedShadowMap {
public:
    static CascadedShadowMap* GetInstance()
    {
        static CascadedShadowMap inst;
        return &inst;
    }

    static const uint32_t kNumCascades = 3;
    static const uint32_t kShadowMapSize = 2048;

    /**
     * @brief Initialize に対応する処理を開始する
     * @param dxCommon 処理に使用する値
     * @param srvManager 処理に使用する値
     * @return なし
     */
    void Initialize(engine::DirectXCommon* dxCommon, SrvManager* srvManager);

    // ライト方向からすべてのカスケード VP 行列を更新
    /**
     * @brief Update に対応する状態を更新する
     * @param lightDir 処理に使用する値
     * @return なし
     */
    void Update(const Vector3& lightDir);

    // カスケード i のシャドウパス開始（DSV セット・クリア・バリア）
    /**
     * @brief BeginCascade に対応する処理を実行する
     * @param cmd 処理に使用する値
     * @param cascadeIdx 処理に使用する値
     * @return なし
     */
    void BeginCascade(ID3D12GraphicsCommandList* cmd, uint32_t cascadeIdx);

    // カスケード i のシャドウパス終了（バリア遷移）
    /**
     * @brief EndCascade に対応する処理を実行する
     * @param cmd 処理に使用する値
     * @return なし
     */
    void EndCascade(ID3D12GraphicsCommandList* cmd);

    // スロット 4 (t1) に Texture2DArray SRV をバインド
    /**
     * @brief SetShadowMapSRV に対応する状態を設定する
     * @param cmd 処理に使用する値
     * @param srvManager 処理に使用する値
     * @return なし
     */
    void SetShadowMapSRV(ID3D12GraphicsCommandList* cmd, SrvManager* srvManager);

    // 指定スロットにカスケード定数バッファをバインド（ModelCommon=8, SkinCommon=9）
    /**
     * @brief SetCascadeDataCBV に対応する状態を設定する
     * @param cmd 処理に使用する値
     * @param slot 処理に使用する値
     * @return なし
     */
    void SetCascadeDataCBV(ID3D12GraphicsCommandList* cmd, uint32_t slot);

    Matrix4x4 GetCascadeVP(uint32_t i) const { return cascadeVP_[i]; }

    // ダミーバッファアドレス（SpriteCommon 等のスロットバインド用）
    D3D12_GPU_VIRTUAL_ADDRESS GetDummyCBVAddress() const
    {
        return dummyCBRes_ ? dummyCBRes_->GetGPUVirtualAddress() : 0;
    }

private:
    CascadedShadowMap() = default;
    ~CascadedShadowMap() = default;
    CascadedShadowMap(const CascadedShadowMap&) = delete;
    CascadedShadowMap& operator=(const CascadedShadowMap&) = delete;

    // カスケード i の VP 行列を計算（固定フラスタム fitting）
    Matrix4x4 ComputeCascadeVP(const Vector3& lightDir, uint32_t cascadeIdx);

    /**
     * @brief CascadeDataLayout に関する型を提供する
     * @details CascadeDataLayout が扱うデータと操作の責務をまとめる
     */
    struct CascadeDataLayout {
        Matrix4x4 cascadeVP[kNumCascades];
        float splitDist[kNumCascades]; // カメラ距離スプリット (m)
        float numCascades; // 有効カスケード数（float のため 16 バイトアライン）
        float _pad[3];
    };

    engine::DirectXCommon* dxCommon_ = nullptr;
    SrvManager* srvManager_ = nullptr;

    // Texture2DArray[3] (R32_TYPELESS)  DSV=D32_FLOAT, SRV=R32_FLOAT
    Microsoft::WRL::ComPtr<ID3D12Resource> shadowTex_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap_; // 3 DSV descriptors
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandles_[kNumCascades] = { };
    uint32_t shadowSrvIndex_ = UINT32_MAX;
    bool cascadeInDepthWrite_[kNumCascades] = { };

    // 定数バッファ
    Microsoft::WRL::ComPtr<ID3D12Resource> cascadeCBRes_;
    CascadeDataLayout* cascadeCBData_ = nullptr;

    // ダミー定数バッファ（SpriteCommon 等が b3 を使わない場合のスロット埋め用）
    Microsoft::WRL::ComPtr<ID3D12Resource> dummyCBRes_;

    Matrix4x4 cascadeVP_[kNumCascades] = { };

    // 各カスケードの固定正射影パラメータ
    /**
     * @brief CascadeConfig に関する型を提供する
     * @details CascadeConfig が扱うデータと操作の責務をまとめる
     */
    struct CascadeConfig {
        float orthoWidth;
        float orthoHeight;
        float nearZ;
        float farZ;
        float splitDist;
    };
    static constexpr CascadeConfig kCascadeConfigs[kNumCascades] = {
        { 14.0f, 10.0f, 0.1f, 30.0f, 12.0f }, // near cascade
        { 28.0f, 18.0f, 0.1f, 55.0f, 30.0f }, // mid cascade
        { 42.0f, 27.0f, 0.1f, 85.0f, 80.0f }, // far cascade
    };
};

} // namespace engine::graphics
