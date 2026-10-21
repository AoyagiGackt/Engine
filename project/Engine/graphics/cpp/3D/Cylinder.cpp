/**
 * @file Cylinder.cpp
 * @brief 円柱メッシュ（Cylinder）の頂点生成とパイプラインの実装
 */
#include "Cylinder.h"
#include "DirectXCommon.h"
#include <cmath>
#include <numbers>

namespace engine::graphics {
namespace {

// 分割あたりの頂点数

}

/** @brief 円柱のメッシュとパイプラインを用意する */
void Cylinder::Initialize(DirectXCommon* dxCommon, const std::string& textureFilePath, int divisions)
{
    SetupMaterialBuffers(dxCommon, textureFilePath, divisions, materialData_);
    materialData_->alphaReference = 0.0f;

    RebuildVertices();
    CreatePipeline();
}

/** @brief 今の形の設定から頂点を作り直す */
void Cylinder::RebuildVertices()
{
    if (!vertexData_)
    {
        return;
    }

    for (int index = 0; index < divisions_; ++index)
    {
        const SegmentAngles a = SegmentAt(index);
        // -sinでX方向を時計回り、側面法線はXZ平面外向き。上面側を V=0、底面側を V=1 にする
        const Vector4 topCur = { -a.s * topRadius_, height_, a.c * topRadius_, 1.0f };
        const Vector4 topNext = { -a.sNext * topRadius_, height_, a.cNext * topRadius_, 1.0f };
        const Vector4 botCur = { -a.s * bottomRadius_, 0.0f, a.c * bottomRadius_, 1.0f };
        const Vector4 botNext = { -a.sNext * bottomRadius_, 0.0f, a.cNext * bottomRadius_, 1.0f };
        WriteSegmentQuad(index, a, topCur, topNext, botCur, botNext,
            { -a.s, 0.0f, a.c }, { -a.sNext, 0.0f, a.cNext });
    }
}

/** @brief 円柱用のパイプラインを生成する */
void Cylinder::CreatePipeline()
{
    CreatePipelineCommon(L"Resources/shaders/cylinder/Cylinder.VS.hlsl",
        L"Resources/shaders/cylinder/Cylinder.PS.hlsl",
        D3D12_TEXTURE_ADDRESS_MODE_WRAP);
}

} // namespace engine::graphics
