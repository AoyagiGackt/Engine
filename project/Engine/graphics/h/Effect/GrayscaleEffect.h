/**
 * @file GrayscaleEffect.h
 * @brief GrayscaleEffectの画面効果の生成、更新、描画に関する公開型と操作インターフェースを定義するファイル
 */
#pragma once
#include "PostEffectFullscreenPass.h"
namespace engine::graphics {

class GrayscaleEffect : public PostEffectFullscreenPass {
public:
    static GrayscaleEffect* GetInstance()
    {
        static GrayscaleEffect instance;
        return &instance;
    }

    /**
     * @brief オフスクリーンRTV・定数バッファ・グレースケール変換用PSOを生成する
     * @param dxCommon DirectX共通基盤
     * @param srvManager SRVディスクリプタヒープ管理
     */
    void Initialize(engine::DirectXCommon* dxCommon, SrvManager* srvManager);
    void Finalize()
    {
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
