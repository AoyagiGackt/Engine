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

    // HP / ダメージ
    void TakeDamage(int dmg = 1) {
        if (defeated_) return;
        hp_ -= dmg;
        if (hp_ <= 0) { hp_ = 0; defeated_ = true; }
    }
    void SetMaxHp(int v)   { maxHp_ = v; hp_ = v; defeated_ = false; }
    bool IsDefeated() const { return defeated_; }
    int  GetHp()      const { return hp_; }
    int  GetMaxHp()   const { return maxHp_; }

    bool           JustLanded()  const { return justLanded_; }
    bool           IsLaunched()  const { return isLaunched_; }
    const Vector3& GetPosition() const { return pos_; }

private:
    static constexpr float kGroundY_  = 0.4f;
    static constexpr float kCeilingY_ = 12.5f;
    static constexpr float kGravity_  = 0.015f;

    std::unique_ptr<Model>    model_;
    std::unique_ptr<Object3d> object_;

    int     maxHp_      = 20;
    int     hp_         = 20;
    bool    defeated_   = false;

    Vector3 pos_        = {};
    float   velY_       = 0.0f;
    bool    isLaunched_ = false;
    bool    justLanded_ = false;
};
