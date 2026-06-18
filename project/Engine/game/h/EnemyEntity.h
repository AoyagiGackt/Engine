#pragma once
#include "Model.h"
#include "ModelCommon.h"
#include "Object3d.h"
#include <memory>

class EnemyEntity {
public:
    void Initialize(ModelCommon* modelCommon, const Vector3& startPos);
    void Update();
    void Draw();

    void Launch(float velY);

    bool           JustLanded()  const { return justLanded_; }
    bool           IsLaunched()  const { return isLaunched_; }
    const Vector3& GetPosition() const { return pos_; }

private:
    static constexpr float kGroundY_  = 0.4f;
    static constexpr float kCeilingY_ = 12.5f;
    static constexpr float kGravity_  = 0.015f;

    std::unique_ptr<Model>    model_;
    std::unique_ptr<Object3d> object_;

    Vector3 pos_        = {};
    float   velY_       = 0.0f;
    bool    isLaunched_ = false;
    bool    justLanded_ = false;
};
