/**
 * @file Cylinder.h
 * @brief Cylinderが公開する型とAPIを定義するファイル
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
        RebuildVertices();
    }
    void SetBottomRadius(float r)
    {
        bottomRadius_ = r;
        RebuildVertices();
    }
    void SetHeight(float h)
    {
        height_ = h;
        RebuildVertices();
    }
    void SetAlphaReference(float a) { materialData_->alphaReference = a; }

protected:
    void SetWVP(const Matrix4x4& wvp) override { materialData_->WVP = wvp; }

private:
    void CreatePipeline();
    void RebuildVertices();

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
