#include "BulletPool.h"
using namespace engine;
using namespace engine::graphics;
using namespace engine::game;

void BulletPool::Initialize(ModelCommon* modelCommon, Model* model)
{
    for (auto& s : slots_) {
        s.obj = std::make_unique<Object3d>();
        s.obj->Initialize(modelCommon);
        s.obj->SetModel(model);
        s.obj->SetEnableLighting(false);
        s.obj->SetScale({ 0.35f, 0.35f, 0.35f });
        s.obj->SetColor({ 1.0f, 0.85f, 0.1f, 1.0f });
        s.obj->SetPosition({ 0.0f, -999.0f, 0.0f });
        s.obj->Update();
    }
}

void BulletPool::Spawn(const Vector3& pos, const Vector3& vel)
{
    for (auto& s : slots_) {
        if (!s.active) {
            s.pos    = pos;
            s.vel    = vel;
            s.life   = 90.0f;
            s.active = true;
            s.obj->SetPosition(pos);
            s.obj->Update();
            return;
        }
    }
}

void BulletPool::Update()
{
    for (auto& s : slots_) {
        if (!s.active) { continue; }

        s.pos.x += s.vel.x;
        s.pos.y += s.vel.y;
        s.life  -= 1.0f;

        if (s.pos.x < 2.0f || s.pos.x > 28.0f ||
            s.pos.y < -1.0f || s.pos.y > 14.0f ||
            s.life <= 0.0f) {
            s.active = false;
            continue;
        }

        s.obj->SetPosition(s.pos);
        s.obj->Update();
    }
}

void BulletPool::Draw()
{
    for (const auto& s : slots_) {
        if (s.active) { s.obj->Draw(); }
    }
}
