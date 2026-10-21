/**
 * @file Camera.cpp
 * @brief カメラ（Camera）のビュー・プロジェクション行列の計算の実装
 */
#include "Camera.h"
#include "WinApp.h"

namespace engine::graphics {
namespace {

// カメラの初期位置
constexpr float kDefaultCameraZ = -10.0f;

// カメラの初期投影
constexpr float kDefaultFovY = 0.45f;
constexpr float kDefaultNearClip = 0.1f;
constexpr float kDefaultFarClip = 100.0f;

}

/** @brief カメラを既定の位置と画角で初期化する */
Camera::Camera()
{
    transform_ = { { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, kDefaultCameraZ } };
    fovY_ = kDefaultFovY;
    aspectRatio_ = (float)WinApp::kClientWidth / (float)WinApp::kClientHeight;
    nearClip_ = kDefaultNearClip;
    farClip_ = kDefaultFarClip;
    viewMatrix_ = MakeIdentity4x4();
    projectionMatrix_ = MakeIdentity4x4();
}

/** @brief ビュー行列とプロジェクション行列を再計算する */
void Camera::RecalcMatrices() const
{
    Matrix4x4 cameraWorldMatrix = MakeAffineMatrix({ 1.0f, 1.0f, 1.0f }, transform_.rotate, transform_.translate);
    viewMatrix_ = Inverse(cameraWorldMatrix);
    projectionMatrix_ = MakePerspectiveFovMatrix(fovY_, aspectRatio_, nearClip_, farClip_);
    isDirty_ = false;
}

} // namespace engine::graphics
