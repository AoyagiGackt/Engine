#include "HsvFilter.h"
using namespace engine;
using namespace engine::graphics;

void HsvFilter::Initialize(DirectXCommon* dxCommon, SrvManager* srvManager)
{
    cbData_ = static_cast<HsvFilterParams*>(
        InitializeCommon(dxCommon, srvManager, L"Resources/shaders/postprocess/HsvFilterPS.hlsl"));
    *cbData_ = HsvFilterParams{};
}

void HsvFilter::SetHueShift(float degrees)   { if (cbData_) { cbData_->hueShift   = degrees; } }
float HsvFilter::GetHueShift()   const       { return cbData_ ? cbData_->hueShift   : 0.0f; }
void HsvFilter::SetSaturation(float s)       { if (cbData_) { cbData_->saturation = s; } }
float HsvFilter::GetSaturation() const       { return cbData_ ? cbData_->saturation : 1.0f; }
void HsvFilter::SetValue(float v)            { if (cbData_) { cbData_->value      = v; } }
float HsvFilter::GetValue()      const       { return cbData_ ? cbData_->value      : 1.0f; }
