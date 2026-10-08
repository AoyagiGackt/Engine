#include "FloatingTitle.h"
#include "DirectXCommon.h"
#include "GameConstants.h"
#include "Input.h"
#include "ModelManager.h"
#include "ShadowManager.h"
#include "SrvManager.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <numbers>
#ifdef USE_IMGUI
#include <imgui.h>
#endif
using namespace engine;
using namespace engine::graphics;
namespace engine::game {
namespace {
// ロゴ専用の描画範囲（画面上部中央）
constexpr float kViewportX = 320.0f;
constexpr float kViewportY = 20.0f;
constexpr float kViewportWidth = 640.0f;
constexpr float kViewportHeight = 240.0f;
constexpr float kViewportAspect = kViewportWidth / kViewportHeight;

// カメラとライト
constexpr float kCameraFovY = 0.45f;
constexpr Vector3 kLightDirection = { 0.35f, -0.45f, 0.82f };
constexpr Vector4 kLightColor = { 1.0f, 0.94f, 0.82f, 1.0f };
constexpr float kLightIntensity = 0.85f;
constexpr float kAmbientIntensity = 0.55f;

// ロゴの材質
constexpr Vector4 kLogoColor = { 0.95f, 0.64f, 0.20f, 1.0f };
constexpr Vector3 kLogoSpecularColor = { 1.0f, 0.93f, 0.68f };
constexpr float kLogoShininess = 80.0f;
constexpr Vector3 kLogoRimColor = { 1.0f, 0.78f, 0.35f };
constexpr float kLogoRimIntensity = 0.18f;

// マウスでの回転
constexpr float kDragRadiansPerPixel = 0.008f;
constexpr float kMaxSpinVelocity = 4.0f; // 指を離した後に残す回転速度の上限（ラジアン/秒）
constexpr float kSpinDamping = 3.0f; // 慣性回転の減衰率（1/秒）

// 操作していない間の漂い（周期の異なる2つの揺れを重ねる）
constexpr float kDriftPitchFreqA = 0.63f;
constexpr float kDriftPitchAmpA = 0.12f;
constexpr float kDriftPitchFreqB = 1.07f;
constexpr float kDriftPitchAmpB = 0.035f;
constexpr float kDriftYawFreqA = 0.47f;
constexpr float kDriftYawAmpA = 0.20f;
constexpr float kDriftYawFreqB = 0.83f;
constexpr float kDriftYawAmpB = 0.06f;
constexpr float kDriftRollFreqA = 0.39f;
constexpr float kDriftRollAmpA = 0.04f;
constexpr float kDriftRollFreqB = 0.91f;
constexpr float kDriftRollAmpB = 0.009f;
constexpr float kFloatFreqA = 0.78f;
constexpr float kFloatAmpA = 0.10f;
constexpr float kFloatFreqB = 1.31f;
constexpr float kFloatAmpB = 0.03f;

// カメラ距離の算出
constexpr float kMaxFloatOffset = kFloatAmpA + kFloatAmpB; // 上下の漂いぶんの余白
constexpr float kFramingMargin = 1.18f; // ロゴの周囲に残す余白の倍率
constexpr float kRestingDepthRatio = 0.25f; // 正面向き時に横幅比で足す奥行きの余裕
}

void FloatingTitle::Initialize(DirectXCommon* graphics, Input* input, ModelCommon* models, ShadowManager* shadows)
{
    dxCommon_ = graphics; input_ = input; modelCommon_ = models; shadowManager_ = shadows;

    Model* model = ModelManager::GetInstance()->GetOrLoad(modelCommon_,
        "Resources/title/title.obj", "Resources/white.png");
    Vector3 minimum = { FLT_MAX, FLT_MAX, FLT_MAX };
    Vector3 maximum = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
    for (const auto& vertex : model->GetVertices()) {
        const auto& p = vertex.position;
        minimum.x = (std::min)(minimum.x, p.x); maximum.x = (std::max)(maximum.x, p.x);
        minimum.y = (std::min)(minimum.y, p.y); maximum.y = (std::max)(maximum.y, p.y);
        minimum.z = (std::min)(minimum.z, p.z); maximum.z = (std::max)(maximum.z, p.z);
    }
    center_ = (minimum + maximum) * 0.5f;
    halfSize_ = { (maximum.x - minimum.x) * 0.5f,
        (maximum.y - minimum.y) * 0.5f, (maximum.z - minimum.z) * 0.5f };
    camera_ = std::make_unique<Camera>();
    camera_->SetAspectRatio(kViewportAspect);
    camera_->SetFovY(kCameraFovY);
    lighting_ = std::make_unique<Object3dCommon>();
    lighting_->Initialize(dxCommon_);
    lighting_->SetManualLightOverride(true);
    lighting_->SetLightDirection(kLightDirection);
    lighting_->SetLightColor(kLightColor);
    lighting_->SetLightIntensity(kLightIntensity);
    lighting_->SetAmbientIntensity(kAmbientIntensity);
    object_ = std::make_unique<Object3d>();
    object_->Initialize(modelCommon_);
    object_->SetModel(model);
    object_->SetCamera(camera_.get());
    object_->SetUseTexture(false);
    object_->SetColor(kLogoColor);
    object_->SetSpecularColor(kLogoSpecularColor);
    object_->SetShininess(kLogoShininess);
    object_->SetEnableRim(true);
    object_->SetRimColor(kLogoRimColor);
    object_->SetRimIntensity(kLogoRimIntensity);
    Update(false);
}
void FloatingTitle::Update(bool allowInteraction)
{
    const float dt = GameConstants::kFrameDeltaTime;
    floatTime_ += dt;
    const POINT clientMouse = input_->GetMouseClientPosition();
    const auto client = dxCommon_->GetCenteredClientViewport();
    const Vector2 mouse = {
        (clientMouse.x - client.TopLeftX) * GameConstants::kScreenWidth / client.Width,
        (clientMouse.y - client.TopLeftY) * GameConstants::kScreenHeight / client.Height
    };
    const float x = mouse.x;
    const float y = mouse.y;
    const bool hovered = x >= kViewportX && x <= kViewportX + kViewportWidth
        && y >= kViewportY && y <= kViewportY + kViewportHeight;
    bool canDrag = allowInteraction;
#ifdef USE_IMGUI
    canDrag = canDrag && !ImGui::GetIO().WantCaptureMouse;
#endif
    if (canDrag && hovered && input_->TriggerMouseButton(0)) {
        dragging_ = true;
        lastMouse_ = mouse;
        yawVelocity_ = pitchVelocity_ = 0.0f;
    }
    if (!canDrag || !input_->PushMouseButton(0)) { dragging_ = false; }
    if (dragging_) {
        const float yawDelta = (mouse.x - lastMouse_.x) * kDragRadiansPerPixel;
        const float pitchDelta = (mouse.y - lastMouse_.y) * kDragRadiansPerPixel;
        yaw_ += yawDelta;
        pitch_ += pitchDelta;
        yawVelocity_ = std::clamp(yawDelta / dt, -kMaxSpinVelocity, kMaxSpinVelocity);
        pitchVelocity_ = std::clamp(pitchDelta / dt, -kMaxSpinVelocity, kMaxSpinVelocity);
        lastMouse_ = mouse;
    } else {
        yaw_ += yawVelocity_ * dt;
        pitch_ += pitchVelocity_ * dt;
        const float damping = std::exp(-kSpinDamping * dt);
        yawVelocity_ *= damping;
        pitchVelocity_ *= damping;
    }
    yaw_ = std::remainder(yaw_, GameConstants::kTwoPi);
    pitch_ = std::remainder(pitch_, GameConstants::kTwoPi);
    // 異なる周期の揺れを重ね、操作していない間も滑らかな漂いを続ける。
    const float driftPitch = std::sin(floatTime_ * kDriftPitchFreqA) * kDriftPitchAmpA
        + std::sin(floatTime_ * kDriftPitchFreqB) * kDriftPitchAmpB;
    const float driftYaw = std::sin(floatTime_ * kDriftYawFreqA + 0.8f) * kDriftYawAmpA
        + std::sin(floatTime_ * kDriftYawFreqB) * kDriftYawAmpB;
    const float driftRoll = std::sin(floatTime_ * kDriftRollFreqA + 1.6f) * kDriftRollAmpA
        + std::sin(floatTime_ * kDriftRollFreqB) * kDriftRollAmpB;
    const float floatOffset = std::sin(floatTime_ * kFloatFreqA + 0.4f) * kFloatAmpA
        + std::sin(floatTime_ * kFloatFreqB) * kFloatAmpB;
    const Vector3 rotation = { pitch_ + driftPitch, yaw_ + driftYaw, driftRoll };
    Matrix4x4 local = MakeAffineMatrix({ 1.0f, 1.0f, 1.0f }, rotation, {});
    // モデルの中心を回転軸にして、マウスで回しても画面上の定位置から飛び出さないようにする。
    local.m[3][0] = -(center_.x * local.m[0][0] + center_.y * local.m[1][0] + center_.z * local.m[2][0]);
    local.m[3][1] = -(center_.x * local.m[0][1] + center_.y * local.m[1][1] + center_.z * local.m[2][1])
        + floatOffset;
    local.m[3][2] = -(center_.x * local.m[0][2] + center_.y * local.m[1][2] + center_.z * local.m[2][2]);
    // 向きに応じて収まる距離を計算し、縦向きに回した場合もロゴ全体を表示する。
    Vector3 extent = {};
    extent.x = std::abs(local.m[0][0]) * halfSize_.x + std::abs(local.m[1][0]) * halfSize_.y + std::abs(local.m[2][0]) * halfSize_.z;
    extent.y = std::abs(local.m[0][1]) * halfSize_.x + std::abs(local.m[1][1]) * halfSize_.y + std::abs(local.m[2][1]) * halfSize_.z;
    extent.z = std::abs(local.m[0][2]) * halfSize_.x + std::abs(local.m[1][2]) * halfSize_.y + std::abs(local.m[2][2]) * halfSize_.z;
    const float tangent = std::tan(kCameraFovY * 0.5f);
    const float distance = extent.z
        + (std::max)(extent.x / (tangent * kViewportAspect), (extent.y + kMaxFloatOffset) / tangent) * kFramingMargin;
    // 通常の揺れではカメラ距離を固定し、ロゴが不自然に拡大・縮小するのを防ぐ。
    const float restingDistance = halfSize_.z + halfSize_.x * kRestingDepthRatio
        + (std::max)(halfSize_.x / (tangent * kViewportAspect), (halfSize_.y + kMaxFloatOffset) / tangent) * kFramingMargin;
    camera_->SetTranslate({ 0.0f, 0.0f, -(std::max)(distance, restingDistance) });
    object_->SetLocalMatrix(local);
    object_->Update();
}
void FloatingTitle::Draw()
{
    auto* cmd = dxCommon_->GetCommandList();
    const auto client = dxCommon_->GetCenteredClientViewport();
    const float scaleX = client.Width / GameConstants::kScreenWidth;
    const float scaleY = client.Height / GameConstants::kScreenHeight;
    const D3D12_VIEWPORT viewport = { client.TopLeftX + kViewportX * scaleX,
        client.TopLeftY + kViewportY * scaleY,
        kViewportWidth * scaleX, kViewportHeight * scaleY, 0.0f, 1.0f };
    const D3D12_RECT scissor = { static_cast<LONG>(viewport.TopLeftX), static_cast<LONG>(viewport.TopLeftY),
        static_cast<LONG>(viewport.TopLeftX + viewport.Width), static_cast<LONG>(viewport.TopLeftY + viewport.Height) };
    // ロゴの専用範囲だけ深度を消し、背景デモに隠れず常に同じ場所へ表示する。
    cmd->ClearDepthStencilView(dxCommon_->GetBackBufferDsvHandle(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 1, &scissor);
    cmd->RSSetViewports(1, &viewport);
    cmd->RSSetScissorRects(1, &scissor);
    modelCommon_->CommonDrawSettings();
    lighting_->SetDefaultLight(cmd);
    shadowManager_->SetShadowMap(cmd, SrvManager::GetInstance());
    object_->Draw();
    const auto fullScissor = dxCommon_->GetCenteredClientScissorRect();
    cmd->RSSetViewports(1, &client);
    cmd->RSSetScissorRects(1, &fullScissor);
}
}
