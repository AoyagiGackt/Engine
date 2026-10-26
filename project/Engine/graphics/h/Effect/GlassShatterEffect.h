/**
 * @file GlassShatterEffect.h
 * @brief ゲーム画面をガラスのように割り砕く演出エフェクトを定義するファイル
 */
#pragma once
#include "DirectXCommon.h"
#include <wrl/client.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>
namespace engine::graphics {

class SrvManager;

/**
 * @brief ゲーム画面をキャプチャし、Voronoi または斬線の破片で割り砕く演出を行うクラス
 * @note クリア演出開始のフレームで CaptureFrame() を呼び、以降は Apply() だけで再生できる
 */
class GlassShatterEffect {
public:
    /** @brief 画面を割る直線の通過点と、画面座標での傾きを保持する */
    struct CutLine {
        float u = 0.5f; ///< 線が通る点の横位置（画面の幅に対する0〜1）
        float v = 0.5f; ///< 線が通る点の縦位置（画面の高さに対する0〜1）
        float angleRadians = 0.0f; ///< 線の傾き（右向きが0、下向きが正）
    };
    static constexpr size_t kMaxCutLines = 32; ///< 斬線の上限（シェーダーの破片の識別が32ビットのため）
    /** @brief 斬線を設定し、空なら従来の割れ方に戻す。上限を超えた線は捨てる */
    void SetCutLines(const std::vector<CutLine>& lines);

    /** @brief 割れ始める点（画面に対する0〜1） */
    struct ImpactPoint {
        float u = 0.5f;
        float v = 0.5f;
    };
    static constexpr size_t kMaxImpacts = 16; ///< 割れ始める点の上限（シェーダーの配列の大きさ）
    /** @brief 割れ始める点を複数設定する（空なら SetImpactUV の1点。上限を超えた点は捨てる） */
    void SetImpactPoints(const std::vector<ImpactPoint>& points);
    /** @brief true なら割れて散った破片がガラスになって集まり直し、はまった所から後ろの画面を見せる */
    void SetReassemble(bool reassemble);

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

    /** @brief 演出の最中かを返す */
    bool IsActive() const {
        return active_;
    }
    /** @brief 演出が終わったかを返す */
    bool IsFinished() const {
        return finished_;
    }
    /** @brief 割る前の画面を撮り直す必要があるかを返す */
    bool NeedCapture() const {
        return captureNeeded_;
    }

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
    static constexpr size_t kCutLineComponents = 4; ///< 斬線1本ぶんの値の数（通る点 xy と法線 zw、シェーダーの float4）

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
        uint32_t cutLineCount = 0;
        float cutSeparateEnd = 0.25f;
        float cutSlide = 0.012f;
        float cutGlowWidth = 0.002f;
        float cutGlowStrength = 1.0f;
        float cutFallDelay = 0.3f;
        float cutSpread = 0.25f;
        std::array<std::array<float, kCutLineComponents>, kMaxCutLines> cutLines = {}; ///< 斬線ごとに通る点（xy）と法線（zw）
        uint32_t impactCount = 0; ///< 0 なら impactU/V の1点だけ
        uint32_t reassemble = 0; ///< 1 なら割れた後に集まり直す
        std::array<float, 2> padding = {}; ///< 次の配列を16バイト境界にそろえる
        std::array<std::array<float, kCutLineComponents>, kMaxImpacts> impacts = {}; ///< 割れ始める点（xy だけ使う）
    };

    // 定数バッファの配置がシェーダーの cbuffer と一致しているかを照合する
    static_assert(offsetof(ShatterParams, cutLineCount) == 20);
    static_assert(offsetof(ShatterParams, cutGlowWidth) == 32);
    static_assert(offsetof(ShatterParams, cutLines) == 48);
    static_assert(offsetof(ShatterParams, impactCount) == 560);
    static_assert(offsetof(ShatterParams, impacts) == 576);
    static_assert(sizeof(ShatterParams) == 832);
    std::vector<CutLine> cutLines_; ///< 設定された斬線（初期化前に設定されても、初期化で定数バッファへ送る）

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
