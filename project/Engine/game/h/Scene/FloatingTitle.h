/**
 * @file FloatingTitle.h
 * @brief タイトル画面の立体ロゴの描画と操作を担当するクラスを定義するファイル
 */
#pragma once
#include "Camera.h"
#include "Object3d.h"
#include "Object3dCommon.h"
#include <memory>
namespace engine { class DirectXCommon; class Input; }
namespace engine::graphics { class ModelCommon; class ShadowManager; }
namespace engine::game {
/** @brief 立体タイトルの専用描画範囲・漂い・マウス操作を所有する。 */
class FloatingTitle {
public:
    void Initialize(engine::DirectXCommon* graphics, engine::Input* input,
        engine::graphics::ModelCommon* models, engine::graphics::ShadowManager* shadows);
    void Update(bool allowInteraction);
    void Draw();
private:
    engine::DirectXCommon* dxCommon_ = nullptr;
    engine::Input* input_ = nullptr;
    engine::graphics::ModelCommon* modelCommon_ = nullptr;
    engine::graphics::ShadowManager* shadowManager_ = nullptr;
    std::unique_ptr<engine::graphics::Camera> camera_;
    std::unique_ptr<engine::graphics::Object3dCommon> lighting_;
    std::unique_ptr<engine::graphics::Object3d> object_;
    Vector3 center_ = {};
    Vector3 halfSize_ = {};
    float floatTime_ = 0.0f;
    float yaw_ = 0.0f, pitch_ = 0.0f;
    float yawVelocity_ = 0.0f, pitchVelocity_ = 0.0f;
    bool dragging_ = false;
    Vector2 lastMouse_ = {};
};
}
