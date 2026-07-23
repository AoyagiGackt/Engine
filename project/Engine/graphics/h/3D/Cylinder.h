/**
 * @file Cylinder.h
 * @brief Cylinderの描画資源とGPU処理の管理に関する公開型と操作インターフェースを定義するファイル
 */
#pragma once
#include "RevolvedMeshBase.h"

namespace engine::graphics {

class Cylinder : public RevolvedMeshBase {
public:
    void Initialize(engine::DirectXCommon* dxCommon,
        const std::string& textureFilePath = "Resources/gradationLine.png",
        int divisions = 32);

    void SetColor(const Vector4& color) { materialData_->color = color; }
    void SetTopRadius(float r)
    {
        topRadius_ = r;
        // 形状変更を反映して頂点データを再生成する
        RebuildVertices();
    }
    void SetBottomRadius(float r)
    {
        bottomRadius_ = r;
        // 形状変更を反映して頂点データを再生成する
        RebuildVertices();
    }
    void SetHeight(float h)
    {
        height_ = h;
        // 形状変更を反映して頂点データを再生成する
        RebuildVertices();
    }
    void SetAlphaReference(float a) { materialData_->alphaReference = a; }

protected:
    void SetWVP(const Matrix4x4& wvp) override { materialData_->WVP = wvp; }

private:
    void CreatePipeline();
    void RebuildVertices();

    /**
     * @brief Cylinder描画PS/VS用の定数バッファに1:1で対応するパラメータ構造体
     */
    struct MaterialCB {
        Matrix4x4 WVP;
        Vector4 color;
        float alphaReference;
        float _pad[3];
    };

    MaterialCB* materialData_ = nullptr;

    float topRadius_ = 1.0f;
    float bottomRadius_ = 1.0f;
    float height_ = 3.0f;
};

} // namespace engine::graphics
