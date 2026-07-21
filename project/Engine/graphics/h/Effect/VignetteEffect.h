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
     * @brief Initialize に対応する処理を開始する
     * @param dxCommon 処理に使用する値
     * @return なし
     */
    void Initialize(engine::DirectXCommon* dxCommon);
    /**
     * @brief Finalize に対応する終了処理を行う
     * @return なし
     */
    void Finalize();

    // バックバッファ上にビネットオーバーレイを描画するシーン描画後に呼ぶ
    /**
     * @brief Apply に対応する状態を設定する
     * @return なし
     */
    void Apply();

    void SetEnabled(bool enabled) { enabled_ = enabled; }
    bool IsEnabled() const { return enabled_; }
    /**
     * @brief SetIntensity に対応する状態を設定する
     * @param v 処理に使用する値
     * @return なし
     */
    void SetIntensity(float v);
    /**
     * @brief GetIntensity の結果を取得する
     * @return 処理結果
     */
    float GetIntensity() const;
    /**
     * @brief SetRadius に対応する状態を設定する
     * @param v 処理に使用する値
     * @return なし
     */
    void SetRadius(float v);
    /**
     * @brief GetRadius の結果を取得する
     * @return 処理結果
     */
    float GetRadius() const;
    /**
     * @brief SetSoftness に対応する状態を設定する
     * @param v 処理に使用する値
     * @return なし
     */
    void SetSoftness(float v);
    /**
     * @brief GetSoftness の結果を取得する
     * @return 処理結果
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
     * @brief VignetteParams に関する型を提供する
     * @details VignetteParams が扱うデータと操作の責務をまとめる
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
