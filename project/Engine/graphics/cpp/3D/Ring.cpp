/**
 * @file Ring.cpp
 * @brief Ringの描画資源とGPU処理の管理に関する具体的な処理を実装するファイル
 */
#include "Ring.h"
#include "DirectXCommon.h"
#include <cmath>
#include <numbers>
using namespace engine;
using namespace engine::graphics;

void Ring::Initialize(DirectXCommon* dxCommon, const std::string& textureFilePath, int divisions)
{
    SetupCommonBuffers(dxCommon, textureFilePath, divisions, sizeof(MaterialCB),
        reinterpret_cast<void**>(&materialData_));
    materialData_->WVP = MakeIdentity4x4();
    materialData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };

    RebuildVertices();
    CreatePipeline();
}

void Ring::RebuildVertices()
{
    if (!vertexData_) {
        return;
    }

    const float radianPerDivide = 2.0f * std::numbers::pi_v<float> / float(divisions_);
    const Vector3 normal = { 0.0f, 0.0f, -1.0f }; // XY平面の面法線

    for (int index = 0; index < divisions_; ++index) {
        float s = std::sin(float(index) * radianPerDivide);
        float c = std::cos(float(index) * radianPerDivide);
        float sNext = std::sin(float(index + 1) * radianPerDivide);
        float cNext = std::cos(float(index + 1) * radianPerDivide);
        float u = float(index) / float(divisions_);
        float uNext = float(index + 1) / float(divisions_);

        // XY平面上（Z=0）、-sinでX方向を時計回りに
        Vector4 outerCur = { -s * outerRadius_, c * outerRadius_, 0.0f, 1.0f };
        Vector4 outerNext = { -sNext * outerRadius_, cNext * outerRadius_, 0.0f, 1.0f };
        Vector4 innerCur = { -s * innerRadius_, c * innerRadius_, 0.0f, 1.0f };
        Vector4 innerNext = { -sNext * innerRadius_, cNext * innerRadius_, 0.0f, 1.0f };

        int idx = index * 6;
        vertexData_[idx + 0] = { outerCur, { u, 0.0f }, normal };
        vertexData_[idx + 1] = { outerNext, { uNext, 0.0f }, normal };
        vertexData_[idx + 2] = { innerCur, { u, 1.0f }, normal };
        vertexData_[idx + 3] = { innerCur, { u, 1.0f }, normal };
        vertexData_[idx + 4] = { outerNext, { uNext, 0.0f }, normal };
        vertexData_[idx + 5] = { innerNext, { uNext, 1.0f }, normal };
    }
}

void Ring::CreatePipeline()
{
    CreatePipelineCommon(L"Resources/shaders/ring/Ring.VS.hlsl",
        L"Resources/shaders/ring/Ring.PS.hlsl",
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
}
