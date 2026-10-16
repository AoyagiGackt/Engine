/**
 * @file HsvFilter.cpp
 * @brief HsvFilterの画面効果の生成、更新、描画に関する具体的な処理を実装するファイル
 */
#include "HsvFilter.h"
#include "GraphicsTuning.h"
namespace {
constexpr float kHueLimit = 180.0f;
}
using namespace engine;
using namespace engine::graphics;

void HsvFilter::Initialize(DirectXCommon* dxCommon, SrvManager* srvManager)
{
    cbData_ = static_cast<HsvFilterParams*>(
        InitializeCommon(dxCommon, srvManager, L"Resources/shaders/postprocess/HsvFilterPS.hlsl"));
    *cbData_ = HsvFilterParams { };
    cbData_->hueShift = GraphicsTuning::Float("hsv.hueShift", cbData_->hueShift, -kHueLimit, kHueLimit);
    cbData_->saturation = GraphicsTuning::Float("hsv.saturation", cbData_->saturation);
    cbData_->value = GraphicsTuning::Float("hsv.value", cbData_->value);
}

void HsvFilter::SetHueShift(float degrees)
{
    if (cbData_) {
        cbData_->hueShift = degrees;
    }
}
float HsvFilter::GetHueShift() const { return cbData_ ? cbData_->hueShift : 0.0f; }
void HsvFilter::SetSaturation(float s)
{
    if (cbData_) {
        cbData_->saturation = s;
    }
}
float HsvFilter::GetSaturation() const { return cbData_ ? cbData_->saturation : 1.0f; }
void HsvFilter::SetValue(float v)
{
    if (cbData_) {
        cbData_->value = v;
    }
}
float HsvFilter::GetValue() const { return cbData_ ? cbData_->value : 1.0f; }
