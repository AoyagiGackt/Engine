/**
 * @file Cylinder.h
 * @brief 円柱メッシュ（Cylinder）の宣言
 */
#pragma once
#include "RevolvedMeshBase.h"

namespace engine::graphics {

/** @brief 上下の半径と高さから円柱メッシュを構築する */
class Cylinder : public RevolvedMeshBase {
public:
    void Initialize(engine::DirectXCommon* dxCommon,
        const std::string& textureFilePath = "Resources/Effects/gradationLine.png",
        int divisions = kDefaultDivisions);

    /** @brief 色を設定する */
    void SetColor(const Vector4& color) {
        materialData_->color = color;
    }
    /** @brief 上面の半径を設定する（頂点を作り直す） */
    void SetTopRadius(float r) {
        topRadius_ = r;
        // 形状変更を反映して頂点データを再生成する
        RebuildVertices();
    }
    /** @brief 底面の半径を設定する（頂点を作り直す） */
    void SetBottomRadius(float r) {
        bottomRadius_ = r;
        // 形状変更を反映して頂点データを再生成する
        RebuildVertices();
    }
    /** @brief 高さを設定する（頂点を作り直す） */
    void SetHeight(float h) {
        height_ = h;
        // 形状変更を反映して頂点データを再生成する
        RebuildVertices();
    }
    /** @brief アルファテストのしきい値を設定する */
    void SetAlphaReference(float a) {
        materialData_->alphaReference = a;
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
     * @brief Cylinder描画PS/VS用の定数バッファに1:1で対応するパラメータ構造体
     */
    struct MaterialCB {
        Matrix4x4 wvp;
        Vector4 color;
        float alphaReference;
        float pad[3];
    };

    MaterialCB* materialData_ = nullptr;

    float topRadius_ = 1.0f;
    float bottomRadius_ = 1.0f;
    float height_ = 3.0f;
};

} // namespace engine::graphics
