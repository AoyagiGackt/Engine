/**
 * @file HsvFilter.cpp
 * @brief 画面の色相・彩度・明度を調整するポストエフェクト（HsvFilter）の実装
 */
#include "HsvFilter.h"
#include "GraphicsTuning.h"
namespace engine::graphics {
namespace {
constexpr float kHueLimit = 180.0f;
}

/** @brief オフスクリーンRTV・定数バッファ・HSV変換用PSOを生成する */
void HsvFilter::Initialize(DirectXCommon* dxCommon, SrvManager* srvManager)
{
    cbData_ = static_cast<HsvFilterParams*>(
        InitializeCommon(dxCommon, srvManager, L"Resources/shaders/postprocess/HsvFilterPS.hlsl"));
    *cbData_ = HsvFilterParams { };
    cbData_->hueShift = GraphicsTuning::Float("hsv.hueShift", cbData_->hueShift, -kHueLimit, kHueLimit);
    cbData_->saturation = GraphicsTuning::Float("hsv.saturation", cbData_->saturation);
    cbData_->value = GraphicsTuning::Float("hsv.value", cbData_->value);
}

/** @brief 色相のシフト量を設定する */
void HsvFilter::SetHueShift(float degrees)
{
    if (cbData_)
    {
        cbData_->hueShift = degrees;
    }
}
/** @brief 色相のシフト量を取得する */
float HsvFilter::GetHueShift() const {
    return cbData_ ? cbData_->hueShift : 0.0f;
}
/** @brief 彩度の倍率を設定する */
void HsvFilter::SetSaturation(float s)
{
    if (cbData_)
    {
        cbData_->saturation = s;
    }
}
/** @brief 彩度の倍率を取得する */
float HsvFilter::GetSaturation() const {
    return cbData_ ? cbData_->saturation : 1.0f;
}
/** @brief 明度の倍率を設定する */
void HsvFilter::SetValue(float v)
{
    if (cbData_)
    {
        cbData_->value = v;
    }
}
/** @brief 明度の倍率を取得する */
float HsvFilter::GetValue() const {
    return cbData_ ? cbData_->value : 1.0f;
}

} // namespace engine::graphics
