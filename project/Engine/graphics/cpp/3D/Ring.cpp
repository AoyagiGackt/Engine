/**
 * @file Ring.cpp
 * @brief リングメッシュ（Ring）の頂点生成とパイプラインの実装
 */
#include "Ring.h"
#include "DirectXCommon.h"
#include <cmath>
#include <numbers>

namespace engine::graphics {
namespace {

// 分割あたりの頂点数

}

/** @brief リングのメッシュとパイプラインを用意する */
void Ring::Initialize(DirectXCommon* dxCommon, const std::string& textureFilePath, int divisions)
{
    SetupMaterialBuffers(dxCommon, textureFilePath, divisions, materialData_);

    RebuildVertices();
    CreatePipeline();
}

/** @brief 今の形の設定から頂点を作り直す */
void Ring::RebuildVertices()
{
    if (!vertexData_)
    {
        return;
    }

    const Vector3 normal = { 0.0f, 0.0f, -1.0f }; // XY平面の面法線

    for (int index = 0; index < divisions_; ++index)
    {
        const SegmentAngles a = SegmentAt(index);
        // XY平面上（Z=0）、-sinでX方向を時計回りに。外周を V=0、内周を V=1 にする
        const Vector4 outerCur = { -a.s * outerRadius_, a.c * outerRadius_, 0.0f, 1.0f };
        const Vector4 outerNext = { -a.sNext * outerRadius_, a.cNext * outerRadius_, 0.0f, 1.0f };
        const Vector4 innerCur = { -a.s * innerRadius_, a.c * innerRadius_, 0.0f, 1.0f };
        const Vector4 innerNext = { -a.sNext * innerRadius_, a.cNext * innerRadius_, 0.0f, 1.0f };
        WriteSegmentQuad(index, a, outerCur, outerNext, innerCur, innerNext, normal, normal);
    }
}

/** @brief リング用のパイプラインを生成する */
void Ring::CreatePipeline()
{
    CreatePipelineCommon(L"Resources/shaders/ring/Ring.VS.hlsl",
        L"Resources/shaders/ring/Ring.PS.hlsl",
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
}

} // namespace engine::graphics
