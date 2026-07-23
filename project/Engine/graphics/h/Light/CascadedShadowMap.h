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
 * @brief 3カスケードの深度シャドウマップ（Texture2DArray）を生成・更新・バインドするシングルトン
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
     * @brief Texture2DArray シャドウマップ・DSV/SRV・カスケード定数バッファを確保する
     * @param dxCommon   デバイス取得に使う DirectX 基盤
     * @param srvManager シャドウマップ SRV の確保に使う SrvManager
     */
    void Initialize(engine::DirectXCommon* dxCommon, SrvManager* srvManager);

    // ライト方向からすべてのカスケード VP 行列を更新
    /**
     * @brief 各カスケードの固定フラスタムパラメータからライト視点の VP 行列を再計算する
     * @param lightDir ライトの進行方向（正規化済みでなくてもよい、シーン中心からの逆方向に光源を置く）
     */
    void Update(const Vector3& lightDir);

    // カスケード i のシャドウパス開始（DSV セット・クリア・バリア）
    /**
     * @brief 指定カスケードの DSV を深度書き込み用にセットし、クリアしてビューポートを設定する
     * @param cmd        コマンドを記録するコマンドリスト
     * @param cascadeIdx 対象カスケード番号（0=近距離 ～ kNumCascades-1=遠距離）
     */
    void BeginCascade(ID3D12GraphicsCommandList* cmd, uint32_t cascadeIdx);

    // カスケード i のシャドウパス終了（バリア遷移）
    /**
     * @brief BeginCascade で書き込み状態にした全カスケードを PIXEL_SHADER_RESOURCE 状態へ遷移する
     * @param cmd コマンドを記録するコマンドリスト
     */
    void EndCascade(ID3D12GraphicsCommandList* cmd);

    // スロット 4 (t1) に Texture2DArray SRV をバインド
    /**
     * @brief シャドウマップの Texture2DArray SRV をルートパラメータスロット 4 (t1) にバインドする
     * @param cmd        コマンドを記録するコマンドリスト
     * @param srvManager GPU ディスクリプタハンドル取得に使う SrvManager
     */
    void SetShadowMapSRV(ID3D12GraphicsCommandList* cmd, SrvManager* srvManager);

    // 指定スロットにカスケード定数バッファをバインド（ModelCommon=8, SkinCommon=9）
    /**
     * @brief カスケードVP行列・スプリット距離を格納した定数バッファを指定スロットへバインドする
     * @param cmd  コマンドを記録するコマンドリスト
     * @param slot バインド先のルートパラメータスロット（ModelCommon=8, SkinCommon=9）
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
     * @brief シェーダーに渡すカスケード定数バッファのレイアウト（256バイトアライン）
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
     * @brief 1カスケード分の正射影パラメータ（幅・高さ・near/far）とカメラ距離スプリット値
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
