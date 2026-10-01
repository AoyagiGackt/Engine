/**
 * @file BulletPool.cpp
 * @brief BulletPoolのプレイヤーの操作、戦闘、状態遷移に関する具体的な処理を実装するファイル
 */
#include "BulletPool.h"
using namespace engine;
using namespace engine::graphics;
using namespace engine::game;

namespace {
constexpr float kBulletScale = 0.35f;
constexpr Vector4 kBulletColor = { 1.0f, 0.85f, 0.1f, 1.0f };
constexpr Vector3 kHiddenPosition = { 0.0f, -999.0f, 0.0f }; // 未使用の弾を画面外へ退避させる位置
constexpr float kBulletLifeFrames = 90.0f;
// この範囲を出た弾は消す（ステージの外枠）
constexpr float kBoundsMinX = 2.0f;
constexpr float kBoundsMaxX = 28.0f;
constexpr float kBoundsMinY = -1.0f;
constexpr float kBoundsMaxY = 14.0f;
}

void BulletPool::Initialize(ModelCommon* modelCommon, Model* model)
{
    for (auto& s : slots_) {
        s.obj = std::make_unique<Object3d>();
        s.obj->Initialize(modelCommon);
        s.obj->SetModel(model);
        s.obj->SetEnableLighting(false);
        s.obj->SetScale({ kBulletScale, kBulletScale, kBulletScale });
        s.obj->SetColor(kBulletColor);
        s.obj->SetPosition(kHiddenPosition);
        s.obj->Update();
    }
}

void BulletPool::Spawn(const Vector3& pos, const Vector3& vel)
{
    for (auto& s : slots_) {
        if (!s.active) {
            s.pos = pos;
            s.vel = vel;
            s.life = kBulletLifeFrames;
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
        if (!s.active) {
            continue;
        }

        s.pos.x += s.vel.x;
        s.pos.y += s.vel.y;
        s.life -= 1.0f;

        if (s.pos.x < kBoundsMinX || s.pos.x > kBoundsMaxX || s.pos.y < kBoundsMinY || s.pos.y > kBoundsMaxY || s.life <= 0.0f) {
            s.active = false;
            continue;
        }

        s.obj->SetPosition(s.pos);
        s.obj->Update();
    }
}

void BulletPool::RefreshVisualTransforms()
{
    for (auto& s : slots_) {
        if (s.active) {
            s.obj->Update();
        }
    }
}

void BulletPool::Draw()
{
    for (const auto& s : slots_) {
        if (s.active) {
            s.obj->Draw();
        }
    }
}
