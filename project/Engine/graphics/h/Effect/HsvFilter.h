/**
 * @file HsvFilter.h
 * @brief HsvFilterの画面効果の生成、更新、描画に関する公開型と操作インターフェースを定義するファイル
 */
#pragma once
#include "PostEffectFullscreenPass.h"
namespace engine::graphics {

class HsvFilter : public PostEffectFullscreenPass {
public:
    static HsvFilter* GetInstance()
    {
        static HsvFilter instance;
        return &instance;
    }

    void Initialize(engine::DirectXCommon* dxCommon, SrvManager* srvManager);
    void Finalize()
    {
        FinalizeCommon();
        cbData_ = nullptr;
    }

    void SetHueShift(float degrees);
    float GetHueShift() const;
    void SetSaturation(float s);
    float GetSaturation() const;
    void SetValue(float v);
    float GetValue() const;

private:
    HsvFilter() = default;

    struct HsvFilterParams {
        float hueShift = 0.0f; // -180 〜 +180 度
        float saturation = 1.0f; // 0=グレー, 1=そのまま
        float value = 1.0f; // 0=黒, 1=そのまま
        float pad = 0.0f;
    };
    HsvFilterParams* cbData_ = nullptr;
};

} // namespace engine::graphics
