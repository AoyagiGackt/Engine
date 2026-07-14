#include "Cylinder.h"
#include "DirectXCommon.h"
#include <cmath>
#include <numbers>
using namespace engine;
using namespace engine::graphics;

void Cylinder::Initialize(DirectXCommon* dxCommon, const std::string& textureFilePath, int divisions)
{
    SetupCommonBuffers(dxCommon, textureFilePath, divisions, sizeof(MaterialCB),
        reinterpret_cast<void**>(&materialData_));
    materialData_->WVP = MakeIdentity4x4();
    materialData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    materialData_->alphaReference = 0.0f;

    RebuildVertices();
    CreatePipeline();
}

void Cylinder::RebuildVertices()
{
    if (!vertexData_) {
        return;
    }

    const float radianPerDivide = 2.0f * std::numbers::pi_v<float> / float(divisions_);

    for (int index = 0; index < divisions_; ++index) {
        float s = std::sin(float(index) * radianPerDivide);
        float c = std::cos(float(index) * radianPerDivide);
        float sNext = std::sin(float(index + 1) * radianPerDivide);
        float cNext = std::cos(float(index + 1) * radianPerDivide);
        float u = float(index) / float(divisions_);
        float uNext = float(index + 1) / float(divisions_);

        // -sinでX方向を時計回り、側面法線はXZ平面外向き
        Vector4 topCur = { -s * topRadius_, height_, c * topRadius_, 1.0f };
        Vector4 topNext = { -sNext * topRadius_, height_, cNext * topRadius_, 1.0f };
        Vector4 botCur = { -s * bottomRadius_, 0.0f, c * bottomRadius_, 1.0f };
        Vector4 botNext = { -sNext * bottomRadius_, 0.0f, cNext * bottomRadius_, 1.0f };
        Vector3 nCur = { -s, 0.0f, c };
        Vector3 nNext = { -sNext, 0.0f, cNext };

        int idx = index * 6;
        // 三角形1: topCur, topNext, botCur
        vertexData_[idx + 0] = { topCur, { u, 0.0f }, nCur };
        vertexData_[idx + 1] = { topNext, { uNext, 0.0f }, nNext };
        vertexData_[idx + 2] = { botCur, { u, 1.0f }, nCur };
        // 三角形2: botCur, topNext, botNext
        vertexData_[idx + 3] = { botCur, { u, 1.0f }, nCur };
        vertexData_[idx + 4] = { topNext, { uNext, 0.0f }, nNext };
        vertexData_[idx + 5] = { botNext, { uNext, 1.0f }, nNext };
    }
}

void Cylinder::CreatePipeline()
{
    CreatePipelineCommon(L"Resources/shaders/cylinder/Cylinder.VS.hlsl",
        L"Resources/shaders/cylinder/Cylinder.PS.hlsl",
        D3D12_TEXTURE_ADDRESS_MODE_WRAP);
}
