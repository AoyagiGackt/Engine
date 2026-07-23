/**
 * @file VignetteEffect.h
 * @brief 画面周辺を暗くするビネットエフェクトを適用するファイル
 */
#pragma once
#include "DirectXCommon.h"
#include <wrl/client.h>
namespace engine::graphics {

/**
 * @brief バックバッファに周辺減光（ビネット）を合成するシングルトンクラス
 * @note シーン描画後に Apply() を呼ぶことでオーバーレイとして重ねる
 */
class VignetteEffect {
public:
    static VignetteEffect* GetInstance()
    {
        static VignetteEffect instance;
        return &instance;
    }

    /**
     * @brief 定数バッファ・ルートシグネチャ・PSOを生成する
     * @param dxCommon DirectX共通基盤
     */
    void Initialize(engine::DirectXCommon* dxCommon);
    /**
     * @brief 生成したリソース（定数バッファ・PSO・ルートシグネチャ）を解放する
     */
    void Finalize();

    // バックバッファ上にビネットオーバーレイを描画するシーン描画後に呼ぶ
    /**
     * @brief バックバッファに周辺減光をアルファブレンドで重ね描きする
     * @note enabled_ が false の場合は何もしない
     */
    void Apply();

    void SetEnabled(bool enabled) { enabled_ = enabled; }
    bool IsEnabled() const { return enabled_; }
    /**
     * @brief 暗転の強さを設定する
     * @param v 減光の強度（0で無効、値が大きいほど周辺が濃く暗くなる）
     */
    void SetIntensity(float v);
    /**
     * @brief 暗転の強さを取得する
     * @return 現在設定されている減光の強度
     */
    float GetIntensity() const;
    /**
     * @brief 減光が始まる画面中心からの半径を設定する
     * @param v 半径（0〜1、画面中心からの正規化距離）
     */
    void SetRadius(float v);
    /**
     * @brief 減光が始まる半径を取得する
     * @return 現在設定されている半径（正規化距離）
     */
    float GetRadius() const;
    /**
     * @brief 明部から暗部への遷移のぼかし幅を設定する
     * @param v 遷移幅（値が大きいほど境界が滑らかになる）
     */
    void SetSoftness(float v);
    /**
     * @brief 遷移のぼかし幅を取得する
     * @return 現在設定されている遷移幅
     */
    float GetSoftness() const;

private:
    VignetteEffect() = default;
    ~VignetteEffect() = default;
    VignetteEffect(const VignetteEffect&) = delete;
    VignetteEffect& operator=(const VignetteEffect&) = delete;

    engine::DirectXCommon* dxCommon_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;

    /**
     * @brief ビネットPS用の定数バッファに1:1で対応するパラメータ構造体
     */
    struct VignetteParams {
        float intensity = 1.0f;
        float radius = 0.3f;
        float softness = 0.4f;
        float pad = 0.0f;
    };
    Microsoft::WRL::ComPtr<ID3D12Resource> cbResource_;
    VignetteParams* cbData_ = nullptr;

    bool enabled_ = false;
};

} // namespace engine::graphics
