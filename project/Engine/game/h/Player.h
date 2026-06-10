#pragma once
#include "Model.h"
#include "Object3d.h"
#include <memory>

class Input;
class ModelCommon;

class Player {
public:
    void Initialize(ModelCommon* modelCommon);
    void Update(Input* input);
    void Draw();

    const Vector3& GetPosition() const { return pos_; }
    bool IsOnGround() const { return onGround_; }
    bool JustJumped() const { return justJumped_; }
    bool JustLanded() const { return justLanded_; }

private:
    static constexpr float kGroundY_   = 0.4f;
    static constexpr float kCeilingY_  = 12.0f;
    static constexpr float kMinX_      = 3.0f;
    static constexpr float kMaxX_      = 27.0f;
    static constexpr float kGravity_   = 0.012f;
    static constexpr float kJumpPower_ = 0.4f;
    static constexpr float kSpeed_     = 0.15f;

    Vector3 pos_          = { 8.0f, 0.4f, 0.0f };
    float   velocityY_    = 0.0f;
    bool    onGround_     = true;
    bool    prevOnGround_ = true;
    bool    justJumped_   = false;
    bool    justLanded_   = false;

    std::unique_ptr<Model>    model_;
    std::unique_ptr<Object3d> object_;
};
