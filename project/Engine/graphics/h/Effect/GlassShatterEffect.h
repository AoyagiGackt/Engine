/**
 * @file GlassShatterEffect.h
 * @brief ゲーム画面をガラスのように割り砕く演出エフェクトを定義するファイル
 */
#pragma once
#include "DirectXCommon.h"
#include <wrl/client.h>
namespace engine::graphics {

class SrvManager;

/**
 * @brief ゲーム画面をキャプチャし、Voronoiシャードで割り砕く演出を行うクラス
 * @note クリア演出開始のフレームで CaptureFrame() を呼び、以降は Apply() だけで再生できる
 */
class GlassShatterEffect {
public:
    /** @brief PSO・定数バッファ等を初期化する */
    void Initialize(engine::DirectXCommon* dxCommon, SrvManager* srvManager);
    /** @brief GPUリソースを解放する */
    void Finalize();

    /** @brief 内部タイマーを進める（毎フレーム呼ぶ） @param dt 経過秒 */
    void Update(float dt);

    /** @brief ゲーム画面をキャプチャする（全シーン描画直後、Apply() の前に一度だけ呼ぶ） */
    void CaptureFrame();

    /** @brief バックバッファ上にエフェクトを重ねて描画する（シーン描画後に呼ぶ） */
    void Apply();

    /** @brief エフェクトを最初から再生する */
    void Start();

    /** @brief タイマーをリセットして非アクティブ状態に戻す */
    void Reset();

    bool IsActive() const { return active_; }
    bool IsFinished() const { return finished_; }
    bool NeedCapture() const { return captureNeeded_; }

    // パラメータ調整（Initialize 後に呼べる）
    void SetImpactUV(float u, float v);
    /**
     * @brief ひび割れ線の太さを設定する
     * @param w 太さ（UV空間、値が大きいほど割れ目が太くなる）
     */
    void SetCrackWidth(float w);
    /**
     * @brief 破片が飛び散る速さを設定する
     * @param s 速度係数（値が大きいほど破片が速く画面外へ移動する）
     */
    void SetShardSpeed(float s);
    /**
     * @brief 演出の再生時間を設定する
     * @param seconds 再生時間（秒、0.01秒未満は0.01秒にクランプされる）
     */
    void SetDuration(float seconds);

private:
    static constexpr float kDefaultDuration = 1.6f;

    engine::DirectXCommon* dxCommon_ = nullptr;
    SrvManager* srvManager_ = nullptr;

    // cbuffer のメモリレイアウト（HLSL の ShatterParams と一致させること）
    /**
     * @brief 割れ演出PS用の定数バッファに1:1で対応するパラメータ構造体
     */
    struct ShatterParams {
        float time = 0.0f;
        float crackWidth = 0.005f;
        float impactU = 0.5f;
        float impactV = 0.5f;
        float shardSpeed = 0.9f;
        float pad[3] = { };
    };

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
    Microsoft::WRL::ComPtr<ID3D12Resource> cbResource_;
    ShatterParams* cbData_ = nullptr;

    // フリーズテクスチャ（クリア直前のゲーム画面を保存する）
    Microsoft::WRL::ComPtr<ID3D12Resource> freezeTexture_;
    uint32_t freezeSrvIndex_ = UINT32_MAX;

    float timer_ = 0.0f;
    float duration_ = kDefaultDuration;
    bool active_ = false;
    bool finished_ = false;
    bool captureNeeded_ = false;
};

} // namespace engine::graphics
