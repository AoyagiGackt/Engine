/**
 * @file StageEditorViewport.cpp
 * @brief ステージエディタの中央ビューと編集カメラ操作を実装するファイル
 */
#include "StageEditorViewport.h"
#include "Camera.h"
#include "Input.h"
#include "Matrix4x4.h"
#include "WinApp.h"
#include <cmath>
#ifdef USE_IMGUI
#include <imgui.h>
#endif

using namespace engine;
using namespace engine::game;
using namespace engine::graphics;

namespace {
constexpr float kLeftPanelWidth = 280.0f;
constexpr float kRightPanelWidth = 300.0f;
constexpr float kToolbarHeight = 42.0f;
constexpr float kCameraSpeedPerSecond = 8.0f;
}

void StageEditorViewport::Reset()
{
    camera_ = nullptr;
}

void StageEditorViewport::UpdateCamera(Input* input, float deltaTime, bool focusMode)
{
#ifdef USE_IMGUI
    if (!camera_ || !input || ImGui::GetIO().WantCaptureKeyboard) {
        return;
    }

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    if (!Contains(mouse.x, mouse.y, focusMode)) {
        return;
    }

    Vector3& position = camera_->GetTranslate();
    if (input->PushKey(DIK_A)) {
        position.x -= kCameraSpeedPerSecond * deltaTime;
    }
    if (input->PushKey(DIK_D)) {
        position.x += kCameraSpeedPerSecond * deltaTime;
    }
    if (input->PushKey(DIK_W)) {
        position.y += kCameraSpeedPerSecond * deltaTime;
    }
    if (input->PushKey(DIK_S)) {
        position.y -= kCameraSpeedPerSecond * deltaTime;
    }
    if (input->PushKey(DIK_Q)) {
        position.z -= kCameraSpeedPerSecond * deltaTime;
    }
    if (input->PushKey(DIK_E)) {
        position.z += kCameraSpeedPerSecond * deltaTime;
    }
#else
    (void)input;
    (void)deltaTime;
    (void)focusMode;
#endif
}

bool StageEditorViewport::Contains(float mouseX, float mouseY, bool focusMode) const
{
    if (focusMode) {
        return mouseX >= 0.0f && mouseX < static_cast<float>(WinApp::kClientWidth)
            && mouseY >= 0.0f && mouseY < static_cast<float>(WinApp::kClientHeight);
    }
    return mouseX >= kLeftPanelWidth
        && mouseX < static_cast<float>(WinApp::kClientWidth) - kRightPanelWidth
        && mouseY >= kToolbarHeight
        && mouseY < static_cast<float>(WinApp::kClientHeight);
}

bool StageEditorViewport::ScreenToGround(float mouseX, float mouseY, Vector3& outWorld) const
{
    if (!camera_) {
        return false;
    }

    // 画面のレイを逆ビュー射影行列で復元し、ゲーム平面との交点を求める
    const Matrix4x4 inverseViewProjection = Inverse(camera_->GetViewProjectionMatrix());
    const float ndcX = mouseX / static_cast<float>(WinApp::kClientWidth) * 2.0f - 1.0f;
    const float ndcY = 1.0f - mouseY / static_cast<float>(WinApp::kClientHeight) * 2.0f;

    auto unproject = [&](float ndcZ) -> Vector3 {
        float x = ndcX * inverseViewProjection.m[0][0] + ndcY * inverseViewProjection.m[1][0]
            + ndcZ * inverseViewProjection.m[2][0] + inverseViewProjection.m[3][0];
        float y = ndcX * inverseViewProjection.m[0][1] + ndcY * inverseViewProjection.m[1][1]
            + ndcZ * inverseViewProjection.m[2][1] + inverseViewProjection.m[3][1];
        float z = ndcX * inverseViewProjection.m[0][2] + ndcY * inverseViewProjection.m[1][2]
            + ndcZ * inverseViewProjection.m[2][2] + inverseViewProjection.m[3][2];
        float w = ndcX * inverseViewProjection.m[0][3] + ndcY * inverseViewProjection.m[1][3]
            + ndcZ * inverseViewProjection.m[2][3] + inverseViewProjection.m[3][3];
        if (std::abs(w) < 1e-8f) {
            w = 1e-8f;
        }
        return { x / w, y / w, z / w };
    };

    const Vector3 nearPoint = unproject(0.0f);
    const Vector3 farPoint = unproject(1.0f);
    const float depthDelta = farPoint.z - nearPoint.z;
    if (std::abs(depthDelta) < 1e-6f) {
        return false;
    }

    const float intersectionDistance = -nearPoint.z / depthDelta;
    if (intersectionDistance < 0.0f) {
        return false;
    }
    outWorld = {
        nearPoint.x + (farPoint.x - nearPoint.x) * intersectionDistance,
        nearPoint.y + (farPoint.y - nearPoint.y) * intersectionDistance,
        0.0f
    };
    return true;
}
