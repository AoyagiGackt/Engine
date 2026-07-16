#pragma once
#include "RevolvedMeshBase.h"

namespace engine::graphics {

class Ring : public RevolvedMeshBase {
public:
    void Initialize(engine::DirectXCommon* dxCommon,
        const std::string& textureFilePath = "Resources/gradationLine.png",
        int divisions = 32);

    void SetColor(const Vector4& color) { materialData_->color = color; }
    void SetInnerRadius(float r)
    {
        innerRadius_ = r;
        RebuildVertices();
    }
    void SetOuterRadius(float R)
    {
        outerRadius_ = R;
        RebuildVertices();
    }

protected:
    void SetWVP(const Matrix4x4& wvp) override { materialData_->WVP = wvp; }

private:
    void CreatePipeline();
    void RebuildVertices();

    struct MaterialCB {
        Matrix4x4 WVP;
        Vector4 color;
    };

    MaterialCB* materialData_ = nullptr;

    float innerRadius_ = 1.0f;
    float outerRadius_ = 2.0f;
};

} // namespace engine::graphics
