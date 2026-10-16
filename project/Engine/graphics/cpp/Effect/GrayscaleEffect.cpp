/**
 * @file GrayscaleEffect.cpp
 * @brief GrayscaleEffectの画面効果の生成、更新、描画に関する具体的な処理を実装するファイル
 */
#include "GrayscaleEffect.h"
#include "GraphicsTuning.h"
using namespace engine;
using namespace engine::graphics;

void GrayscaleEffect::Initialize(DirectXCommon* dxCommon, SrvManager* srvManager)
{
    cbData_ = static_cast<GrayscaleParams*>(
        InitializeCommon(dxCommon, srvManager, L"Resources/shaders/postprocess/GrayscalePS.hlsl"));
    *cbData_ = GrayscaleParams { };
    cbData_->amount = GraphicsTuning::Float("grayscale.amount", cbData_->amount, 0.0f, 1.0f);
}

void GrayscaleEffect::SetAmount(float amount)
{
    if (cbData_) {
        cbData_->amount = amount;
    }
}

float GrayscaleEffect::GetAmount() const
{
    return cbData_ ? cbData_->amount : 0.f;
}
