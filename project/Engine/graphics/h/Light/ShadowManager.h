/**
 * @file ShadowManager.h
 * @brief シャドウマップの生成・管理を担当するクラス
 *
 * Object3dCommon からシャドウ関連処理を分離
 * ライト方向を受け取り LightVP 行列を計算し、
 * シャドウパスの開始/終了/SRVセットを提供します
 */
#pragma once
#include "DirectXCommon.h"
#include "MakeAffine.h"
#include <functional>
#include <wrl/client.h>
namespace engine::graphics {

class SrvManager;
class ModelCommon;

/**
 * @brief シャドウマップの生成・管理を担当するクラス
 * @note ライト方向から平行投影の LightVP 行列を計算し、
 * シャドウパスの開始/終了および SRV のセットを提供する
 * Object3dCommon からシャドウ関連処理を分離した責務単一クラス
 */
class ShadowManager {
public:
    /** @brief 未初期化の影管理を作る */
    ShadowManager() = default;
    /** @brief 確保したSRVを返却する */
    ~ShadowManager();
    /** @brief 所有権の複製を禁止する */
    ShadowManager(const ShadowManager&) = delete;
    /** @brief 所有権の代入を禁止する */
    ShadowManager& operator=(const ShadowManager&) = delete;
    static const UINT kShadowMapSize = 2048; ///< シャドウマップ解像度
    /** @brief 影の投影が中心にするワールド座標を値で取得する */
    static Vector3 SceneCenter();
    /** @brief 共通のシーン中心と距離からライト位置を計算する */
    static Vector3 LightPosition(const Vector3& lightDirection);

    /**
     * @brief 初期化（シャドウマップリソース・DSV・SRV を作成）
     * @param dxCommon engine::DirectXCommon
     * @param srvManager SRV 管理クラス
     */
    void Initialize(engine::DirectXCommon* dxCommon, SrvManager* srvManager);

    /**
     * @brief ライト方向からライト空間行列を更新
     * @param lightDir 正規化済みライト方向ベクトル（Object3dCommon から取得）
     */
    void Update(const Vector3& lightDir);

    /**
     * @brief シャドウパス開始（バリア遷移・DSVクリア・ビューポート設定）
     */
    void BeginShadowPass(ID3D12GraphicsCommandList* commandList);

    /**
     * @brief シャドウパス終了（バリア遷移: DEPTH_WRITE → SRV）
     */
    void EndShadowPass(ID3D12GraphicsCommandList* commandList);

    /**
     * @brief シャドウパスを1回分まとめて行う（開始 → モデル側の影用設定 → 影を落とす物の描画 → 終了）
     * @param commandList 描画を積むコマンドリスト
     * @param modelCommon 影用パイプラインへ切り替えるモデル描画の共通設定
     * @param drawCasters 影を落とす物を描く処理（空なら状態の切り替えだけ行う）
     */
    void RunShadowPass(ID3D12GraphicsCommandList* commandList, ModelCommon* modelCommon,
        const std::function<void()>& drawCasters = { });

    /**
     * @brief シャドウマップ SRV をスロット 4 にセット
     */
    void SetShadowMap(ID3D12GraphicsCommandList* commandList, SrvManager* srvManager);

    /** @brief ライト空間行列を取得 */
    const Matrix4x4& GetLightViewProjection() const {
        return lightVP_;
    }

private:
    engine::DirectXCommon* dxCommon_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> shadowMapResource_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> shadowDsvHeap_;
    uint32_t shadowSrvIndex_ = 0;
    SrvManager* srvManager_ = nullptr;

    Matrix4x4 lightVP_ = MakeIdentity4x4();
    bool shadowMapInDepthWrite_ = true; ///< リソース初期状態は DEPTH_WRITE
};

} // namespace engine::graphics
