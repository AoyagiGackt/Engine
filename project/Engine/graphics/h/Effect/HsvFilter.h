/**
 * @file HsvFilter.h
 * @brief HsvFilterの画面効果の生成、更新、描画に関する公開型と操作インターフェースを定義するファイル
 */
#pragma once
#include "PostEffectFullscreenPass.h"
namespace engine::graphics {

class HsvFilter : public PostEffectFullscreenPass {
public:
    static HsvFilter* GetInstance()
    {
        static HsvFilter instance;
        return &instance;
    }

    /**
     * @brief Initialize に対応する処理を開始する
     * @param dxCommon 処理に使用する値
     * @param srvManager 処理に使用する値
     * @return なし
     */
    void Initialize(engine::DirectXCommon* dxCommon, SrvManager* srvManager);
    void Finalize()
    {
        /**
         * @brief FinalizeCommon に対応する終了処理を行う
         * @return 処理結果
         */
        FinalizeCommon();
        cbData_ = nullptr;
    }

    /**
     * @brief SetHueShift に対応する状態を設定する
     * @param degrees 処理に使用する値
     * @return なし
     */
    void SetHueShift(float degrees);
    /**
     * @brief GetHueShift の結果を取得する
     * @return 処理結果
     */
    float GetHueShift() const;
    /**
     * @brief SetSaturation に対応する状態を設定する
     * @param s 処理に使用する値
     * @return なし
     */
    void SetSaturation(float s);
    /**
     * @brief GetSaturation の結果を取得する
     * @return 処理結果
     */
    float GetSaturation() const;
    /**
     * @brief SetValue に対応する状態を設定する
     * @param v 処理に使用する値
     * @return なし
     */
    void SetValue(float v);
    /**
     * @brief GetValue の結果を取得する
     * @return 処理結果
     */
    float GetValue() const;

private:
    HsvFilter() = default;

    /**
     * @brief HsvFilterParams に関する型を提供する
     * @details HsvFilterParams が扱うデータと操作の責務をまとめる
     */
    struct HsvFilterParams {
        float hueShift = 0.0f; // -180 〜 +180 度
        float saturation = 1.0f; // 0=グレー, 1=そのまま
        float value = 1.0f; // 0=黒, 1=そのまま
        float pad = 0.0f;
    };
    HsvFilterParams* cbData_ = nullptr;
};

} // namespace engine::graphics
