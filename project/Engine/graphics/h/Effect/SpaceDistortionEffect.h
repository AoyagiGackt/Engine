/**
 * @file SpaceDistortionEffect.h
 * @brief 画面をキャプチャし、画面全体をレンズのように中心へ吸い込み・ねじる演出を描画するファイル
 */
#pragma once
#include "DirectXCommon.h"
#include <wrl/client.h>

namespace engine::graphics {

class SrvManager;

/**
 * @brief 空間歪み演出
 * @note AddImpulse() で歪みエネルギーを加えると、画面全体が中心点へ吸い込まれるように
 *       歪んで脈動し、時間経過で減衰する大技の「空間そのものが切り裂かれる」表現用
 *       バックバッファ直描き時のみ有効（オフスクリーンフィルタ使用中は呼ばないこと）
 */
class SpaceDistortionEffect {
public:
    void Initialize(engine::DirectXCommon* dxCommon, SrvManager* srvManager);
    void Finalize();

    /** @brief 歪みの中心を画面UV（0〜1）で設定する */
    void SetCenterUV(float u, float v);

    /**
     * @brief 歪みエネルギーを加える（合計は1にクランプ）
     * @param amount 加算量（斬撃1発=0.1程度、解放=1.0）
     */
    void AddImpulse(float amount);

    /** @brief エネルギー減衰と脈動時間を進める毎フレーム呼ぶ */
    void Update(float dt);

    /** @brief バックバッファをキャプチャし、歪みを重ねて描画する3D描画の後に呼ぶ */
    void CaptureAndApply();

    /** @brief エネルギーを消して非アクティブに戻す */
    void Reset();

    bool IsActive() const { return energy_ > kMinEnergy; }

private:
    static constexpr float kDecayTau  = 0.55f;  ///< エネルギー減衰の時定数（秒）
    static constexpr float kMinEnergy = 0.01f;  ///< これ未満は非アクティブ扱い

    // cbuffer のメモリレイアウト（HLSL の WarpParams と一致させること）
    struct WarpParams {
        float centerU  = 0.5f;
        float centerV  = 0.5f;
        float strength = 0.0f;
        float time     = 0.0f;
        float aspect   = 16.0f / 9.0f;
        float radius   = 0.2f; // 未使用（画面全体に効くためカット不要レイアウト互換のため維持）
        float pad[2]   = {};
    };

    void CreatePipeline();

    engine::DirectXCommon* dxCommon_   = nullptr;
    SrvManager*            srvManager_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
    Microsoft::WRL::ComPtr<ID3D12Resource>      cbResource_;
    WarpParams* cbData_ = nullptr;

    // キャプチャテクスチャ（歪ませる元画面）
    Microsoft::WRL::ComPtr<ID3D12Resource> captureTexture_;
    uint32_t captureSrvIndex_ = UINT32_MAX;

    float energy_ = 0.0f;
    float time_   = 0.0f;
};

} // namespace engine::graphics
