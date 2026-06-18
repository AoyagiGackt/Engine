#pragma once
#include "Model.h"
#include "ModelCommon.h"
#include "Object3d.h"
#include <array>
#include <memory>

class AfterImageRenderer {
public:
    void Initialize(ModelCommon* modelCommon, Model* model);
    // active: 残像を生成するか　dense: 乱舞中（高頻度スポーン）
    void Update(bool active, bool dense, const Vector3& pos, float yaw, float spinZ);
    void Draw();

private:
    struct AfterImage {
        Vector3 pos;
        float   yaw;
        float   spinZ;
        float   alpha;
    };

    static constexpr int   kMaxImages    = 10;
    static constexpr float kFastInterval = 0.03f;
    static constexpr float kSlowInterval = 0.05f;

    std::array<AfterImage, kMaxImages> images_{};
    int   idx_   = 0;
    float timer_ = 0.0f;

    std::unique_ptr<Object3d> object_;
};
