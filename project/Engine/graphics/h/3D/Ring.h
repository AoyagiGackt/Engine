/**
 * @file Ring.h
 * @brief リングメッシュ（Ring）の宣言
 */
#pragma once
#include "RevolvedMeshBase.h"

namespace engine::graphics {

/** @brief 内径と外径から輪状メッシュを構築する */
class Ring : public RevolvedMeshBase {
public:
    void Initialize(engine::DirectXCommon* dxCommon,
        const std::string& textureFilePath = "Resources/Effects/gradationLine.png",
        int divisions = kDefaultDivisions);

    /** @brief 色を設定する */
    void SetColor(const Vector4& color) {
        materialData_->color = color;
    }
    /** @brief 内側の半径を設定する（頂点を作り直す） */
    void SetInnerRadius(float r) {
        innerRadius_ = r;
        // 半径変更を反映して頂点データを再生成する
        RebuildVertices();
    }
    /** @brief 外側の半径を設定する（頂点を作り直す） */
    void SetOuterRadius(float R) {
        outerRadius_ = R;
        // 半径変更を反映して頂点データを再生成する
        RebuildVertices();
    }

protected:
    /** @brief ワールド・ビュー・プロジェクション行列を設定する */
    void SetWVP(const Matrix4x4& wvp) override {
        materialData_->wvp = wvp;
    }

private:
    static constexpr int kDefaultDivisions = 32;
    void CreatePipeline();
    void RebuildVertices();

    /**
     * @brief Ring描画PS/VS用の定数バッファに1:1で対応するパラメータ構造体
     */
    struct MaterialCB {
        Matrix4x4 wvp;
        Vector4 color;
    };

    /** @brief 描画ごとに転送するCPU側のマテリアル */
    MaterialCB material_ {};
    /** @brief 質感設定の編集先 */
    MaterialCB* materialData_ = &material_;

    float innerRadius_ = 1.0f;
    float outerRadius_ = 2.0f;
};

} // namespace engine::graphics
