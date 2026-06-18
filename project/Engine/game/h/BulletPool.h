#pragma once
#include "Model.h"
#include "ModelCommon.h"
#include "Object3d.h"
#include <array>
#include <memory>

class BulletPool {
public:
    static constexpr int kMaxBullets = 48;

    void Initialize(ModelCommon* modelCommon, Model* model);
    void Spawn(const Vector3& pos, const Vector3& vel);
    // 移動・境界チェックのみ。衝突判定は呼び出し側で行い Kill() で無効化する
    void Update();
    void Kill(int index) { slots_[index].active = false; }
    void Draw();

    bool           IsActive(int i)  const { return slots_[i].active; }
    const Vector3& GetPos(int i)    const { return slots_[i].pos; }
    const Vector3& GetVel(int i)    const { return slots_[i].vel; }

private:
    struct Slot {
        Vector3 pos    = {};
        Vector3 vel    = {};
        float   life   = 0.0f;
        bool    active = false;
        std::unique_ptr<Object3d> obj;
    };

    std::array<Slot, kMaxBullets> slots_;
};
