#pragma once
#include "PostEffectFullscreenPass.h"
namespace engine::graphics {

class GrayscaleEffect : public PostEffectFullscreenPass {
public:
    static GrayscaleEffect* GetInstance()
    {
        static GrayscaleEffect instance;
        return &instance;
    }

    void Initialize(engine::DirectXCommon* dxCommon, SrvManager* srvManager);
    void Finalize() { FinalizeCommon(); cbData_ = nullptr; }

    void  SetAmount(float amount);
    float GetAmount() const;

private:
    GrayscaleEffect() = default;

    struct GrayscaleParams {
        float amount = 0.f;
        float pad[3] = {};
    };
    GrayscaleParams* cbData_ = nullptr;
};

} // namespace engine::graphics
