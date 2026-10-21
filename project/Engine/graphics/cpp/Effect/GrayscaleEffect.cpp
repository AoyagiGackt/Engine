/**
 * @file GrayscaleEffect.cpp
 * @brief 画面を白黒にするポストエフェクト（GrayscaleEffect）の実装
 */
#include "GrayscaleEffect.h"
#include "GraphicsTuning.h"

namespace engine::graphics {
/** @brief オフスクリーンRTV・定数バッファ・グレースケール変換用PSOを生成する */
void GrayscaleEffect::Initialize(DirectXCommon* dxCommon, SrvManager* srvManager)
{
    cbData_ = static_cast<GrayscaleParams*>(
        InitializeCommon(dxCommon, srvManager, L"Resources/shaders/postprocess/GrayscalePS.hlsl"));
    *cbData_ = GrayscaleParams { };
    cbData_->amount = GraphicsTuning::Float("grayscale.amount", cbData_->amount, 0.0f, 1.0f);
}

/** @brief グレースケール化の割合を設定する */
void GrayscaleEffect::SetAmount(float amount)
{
    if (cbData_)
    {
        cbData_->amount = amount;
    }
}

/** @brief グレースケール化の割合を取得する */
float GrayscaleEffect::GetAmount() const
{
    return cbData_ ? cbData_->amount : 0.f;
}

} // namespace engine::graphics
