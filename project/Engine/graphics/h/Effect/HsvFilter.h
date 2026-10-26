/**
 * @file HsvFilter.h
 * @brief 画面の色相・彩度・明度を調整するポストエフェクト（HsvFilter）の宣言
 */
#pragma once
#include "PostEffectFullscreenPass.h"
namespace engine::graphics {

/** @brief 画面の色相と彩度と明度を調整する */
class HsvFilter : public PostEffectFullscreenPass {
public:
    /** @brief 共有インスタンスを取得する */
    static HsvFilter* GetInstance() {
        static HsvFilter sInstance;
        return &sInstance;
    }

    /**
     * @brief オフスクリーンRTV・定数バッファ・HSV変換用PSOを生成する
     * @param dxCommon DirectX共通基盤
     * @param srvManager SRVディスクリプタヒープ管理
     */
    void Initialize(engine::DirectXCommon* dxCommon, SrvManager* srvManager);
    /** @brief HSV フィルターの描画資源を解放する */
    void Finalize() {
        // オフスクリーンRTV・定数バッファ・PSOを解放する
        FinalizeCommon();
        cbData_ = nullptr;
    }

    /**
     * @brief 色相のシフト量を設定する
     * @param degrees シフト角度（-180〜+180度）
     */
    void SetHueShift(float degrees);
    /**
     * @brief 色相のシフト量を取得する
     * @return 現在設定されているシフト角度（度）
     */
    float GetHueShift() const;
    /**
     * @brief 彩度の倍率を設定する
     * @param s 彩度（0でグレースケール、1で元の彩度のまま）
     */
    void SetSaturation(float s);
    /**
     * @brief 彩度の倍率を取得する
     * @return 現在設定されている彩度
     */
    float GetSaturation() const;
    /**
     * @brief 明度の倍率を設定する
     * @param v 明度（0で黒、1で元の明度のまま）
     */
    void SetValue(float v);
    /**
     * @brief 明度の倍率を取得する
     * @return 現在設定されている明度
     */
    float GetValue() const;

private:
    /** @brief 色調整フィルターの既定状態を作る */
    HsvFilter() = default;

    /**
     * @brief HSV変換PS用の定数バッファに1:1で対応するパラメータ構造体
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
