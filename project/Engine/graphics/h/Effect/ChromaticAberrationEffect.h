/**
 * @file ChromaticAberrationEffect.h
 * @brief 色収差（クロマティックアベレーション）ポストエフェクト
 *
 * 【概要】
 *   実際のカメラレンズが波長ごとに屈折率が異なることで発生する「色ズレ」を再現する。
 *   シーンをオフスクリーンテクスチャに描いた後、ChromaticAberrationPS.hlsl で
 *   R/G/B チャンネルをわずかにズラして最終出力に合成する。
 *
 * 【使い方】
 *   // 初期化（GrayscaleEffect と同じタイミングで）
 *   ChromaticAberrationEffect::GetInstance()->Initialize(dxCommon_, srvManager_);
 *
 *   // 有効化と強度設定
 *   ChromaticAberrationEffect::GetInstance()->SetEnabled(true);
 *   ChromaticAberrationEffect::GetInstance()->SetStrength(0.02f); // 0.01〜0.05 程度が自然
 *
 *   // Game::Draw() 内（GrayscaleEffect と同じ位置に追加）
 *   auto* ca = ChromaticAberrationEffect::GetInstance();
 *   if (ca->IsEnabled()) {
 *       ca->BeginScene();   // 描画先をオフスクリーンへ
 *   }
 *   SceneManager::GetInstance()->Draw();
 *   if (ca->IsEnabled()) {
 *       ca->EndScene();     // オフスクリーン描画終了
 *       ca->Apply(srvManager); // 色収差を適用してバックバッファへ出力
 *   }
 */
#pragma once
#include "DirectXCommon.h"
#include "SrvManager.h"
#include <wrl/client.h>
namespace engine::graphics {

class ChromaticAberrationEffect {
public:
    /// @brief シングルトンインスタンスを取得する
    static ChromaticAberrationEffect* GetInstance()
    {
        static ChromaticAberrationEffect instance;
        return &instance;
    }

    // ---- 初期化 / 破棄 ----
    /**
     * @brief 初期化。オフスクリーンテクスチャ・ルートシグネチャ・PSO を生成する
     * @param dxCommon DirectX 共通クラスのポインタ
     * @param srvManager SRV 管理クラスのポインタ（オフスクリーン SRV 割り当て用）
     */
    void Initialize(engine::DirectXCommon* dxCommon, SrvManager* srvManager);

    /// @brief GPU リソースを解放する
    void Finalize();

    // ---- 描画パイプライン ----
    /**
     * @brief シーンの描画先をオフスクリーンテクスチャへ切り替える
     * @note SceneManager::Draw() の直前に呼ぶこと
     */
    void BeginScene();

    /**
     * @brief オフスクリーンテクスチャへの描画を終了し、SRV として使えるようにする
     * @note SceneManager::Draw() の直後に呼ぶこと
     */
    void EndScene();

    /**
     * @brief 色収差エフェクトをバックバッファへ適用する（フルスクリーン三角形描画）
     * @param srvManager SRV のディスクリプタハンドル取得に使う
     * @note EndScene() の直後に呼ぶこと
     */
    void Apply(SrvManager* srvManager);

    // ---- パラメータ ----
    /**
     * @brief 色収差の強度を設定する
     * @param strength 0.0=効果なし, 0.01〜0.05=自然な収差, 0.1=強め
     */
    void  SetStrength(float strength) { if (cbData_) { cbData_->strength = strength; } }

    /// @brief 現在の強度を取得する
    float GetStrength() const { return cbData_ ? cbData_->strength : 0.f; }

    /// @brief エフェクトの有効/無効を切り替える
    void SetEnabled(bool enabled) { enabled_ = enabled; }

    /// @brief エフェクトが有効かどうかを返す
    bool IsEnabled() const { return enabled_; }

    /// @brief オフスクリーン RTV ハンドルを取得する（他エフェクトとの組み合わせ用）
    D3D12_CPU_DESCRIPTOR_HANDLE GetSceneRTVHandle() const { return rtvHandle_; }

private:
    ChromaticAberrationEffect() = default;
    ~ChromaticAberrationEffect() = default;
    ChromaticAberrationEffect(const ChromaticAberrationEffect&) = delete;
    ChromaticAberrationEffect& operator=(const ChromaticAberrationEffect&) = delete;

    engine::DirectXCommon* dxCommon_ = nullptr;

    // --- オフスクリーンレンダーターゲット ---
    // TYPELESS にすることで同じバッファを RTV（UNORM_SRGB）と SRV（UNORM_SRGB）で共有できる
    Microsoft::WRL::ComPtr<ID3D12Resource>       sceneTexture_; ///< オフスクリーンテクスチャリソース
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap_;      ///< RTV 専用ディスクリプタヒープ
    D3D12_CPU_DESCRIPTOR_HANDLE                  rtvHandle_ = {};///< RTV ディスクリプタハンドル
    uint32_t                                     srvIndex_  = UINT32_MAX; ///< SrvManager での割り当てインデックス
    bool                                         isFirstFrame_ = true; ///< 初回はバリアをスキップするフラグ

    // --- フルスクリーン色収差 PSO ---
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_; ///< ルートシグネチャ
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_; ///< パイプラインステートオブジェクト

    // --- 定数バッファ（b0 に対応）---
    struct ChromaticParams {
        float strength = 0.f; ///< 色ズレの強さ
        float pad[3]   = {};  ///< 16 バイトアライン用パディング
    };
    Microsoft::WRL::ComPtr<ID3D12Resource> cbResource_; ///< 定数バッファ GPU リソース
    ChromaticParams*                       cbData_ = nullptr; ///< CPU からの書き込みポインタ（マップ済み）

    bool enabled_ = false; ///< エフェクト有効フラグ
};

} // namespace engine::graphics
