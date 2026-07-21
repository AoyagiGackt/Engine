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
     * @brief SetAmount に対応する状態を設定する
     * @param amount 処理に使用する値
     * @return なし
     */
    void SetAmount(float amount);
    /**
     * @brief GetAmount の結果を取得する
     * @return 処理結果
     */
    float GetAmount() const;

private:
    GrayscaleEffect() = default;

    /**
     * @brief GrayscaleParams に関する型を提供する
     * @details GrayscaleParams が扱うデータと操作の責務をまとめる
     */
    struct GrayscaleParams {
        float amount = 0.f;
        float pad[3] = { };
    };
    GrayscaleParams* cbData_ = nullptr;
};

} // namespace engine::graphics
