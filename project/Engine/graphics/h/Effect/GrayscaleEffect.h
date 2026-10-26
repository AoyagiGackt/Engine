/**
 * @file GrayscaleEffect.h
 * @brief 画面を白黒にするポストエフェクト（GrayscaleEffect）の宣言
 */
#pragma once
#include "PostEffectFullscreenPass.h"
namespace engine::graphics {

/** @brief 画面の彩度を落とすポストエフェクトを適用する */
class GrayscaleEffect : public PostEffectFullscreenPass {
public:
    /** @brief 共有インスタンスを取得する */
    static GrayscaleEffect* GetInstance() {
        static GrayscaleEffect sInstance;
        return &sInstance;
    }

    /**
     * @brief オフスクリーンRTV・定数バッファ・グレースケール変換用PSOを生成する
     * @param dxCommon DirectX共通基盤
     * @param srvManager SRVディスクリプタヒープ管理
     */
    void Initialize(engine::DirectXCommon* dxCommon, SrvManager* srvManager);
    /** @brief グレースケールの描画資源を解放する */
    void Finalize() {
        // オフスクリーンRTV・定数バッファ・PSOを解放する
        FinalizeCommon();
        cbData_ = nullptr;
    }

    /**
     * @brief グレースケール化の割合を設定する
     * @param amount 適用度（0で元のカラーのまま、1で完全にグレースケール）
     */
    void SetAmount(float amount);
    /**
     * @brief グレースケール化の割合を取得する
     * @return 現在設定されている適用度
     */
    float GetAmount() const;

private:
    /** @brief グレースケール描画資源が未確保の状態を作る */
    GrayscaleEffect() = default;

    /**
     * @brief グレースケール変換PS用の定数バッファに1:1で対応するパラメータ構造体
     */
    struct GrayscaleParams {
        float amount = 0.f;
        float pad[3] = { };
    };
    GrayscaleParams* cbData_ = nullptr;
};

} // namespace engine::graphics
