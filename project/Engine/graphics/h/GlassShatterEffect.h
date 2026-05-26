#pragma once
#include "DirectXCommon.h"
#include <wrl/client.h>

class SrvManager;

// ガラス割れエフェクト ― ゲーム画面をキャプチャして Voronoi シャードで割り砕く。
// clearTriggered_ になったフレームで CaptureFrame() を呼び、以降は Apply() だけで動く。
class GlassShatterEffect {
public:
    void Initialize(DirectXCommon* dxCommon, SrvManager* srvManager);
    void Finalize();

    // dt [秒] で内部タイマーを進める。毎フレーム呼ぶ。
    void Update(float dt);

    // ゲーム画面をキャプチャする。全シーン描画の直後、Apply() の前に一度だけ呼ぶ。
    void CaptureFrame();

    // バックバッファ上にエフェクトを重ねて描画する。シーン描画後に呼ぶ。
    void Apply();

    // エフェクトを最初から再生する。
    void Start();

    // タイマーをリセットして非アクティブ状態に戻す。
    void Reset();

    bool IsActive()     const { return active_; }
    bool IsFinished()   const { return finished_; }
    bool NeedCapture()  const { return captureNeeded_; }

    // パラメータ調整（Initialize 後に呼べる）
    void SetImpactUV(float u, float v);
    void SetCrackWidth(float w);
    void SetShardSpeed(float s);
    void SetDuration(float seconds);

private:
    static constexpr float kDefaultDuration = 1.6f;

    DirectXCommon* dxCommon_   = nullptr;
    SrvManager*    srvManager_ = nullptr;

    // cbuffer のメモリレイアウト（HLSL の ShatterParams と一致させること）
    struct ShatterParams {
        float time       = 0.0f;
        float crackWidth = 0.005f;
        float impactU    = 0.5f;
        float impactV    = 0.5f;
        float shardSpeed = 0.9f;
        float pad[3]     = {};
    };

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
    Microsoft::WRL::ComPtr<ID3D12Resource>      cbResource_;
    ShatterParams* cbData_ = nullptr;

    // フリーズテクスチャ（クリア直前のゲーム画面を保存する）
    Microsoft::WRL::ComPtr<ID3D12Resource> freezeTexture_;
    uint32_t freezeSrvIndex_ = UINT32_MAX;

    float timer_          = 0.0f;
    float duration_       = kDefaultDuration;
    bool  active_         = false;
    bool  finished_       = false;
    bool  captureNeeded_  = false;
};
