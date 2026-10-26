/**
 * @file StageEditorViewport.cpp
 * @brief ステージエディタの中央ビューと編集カメラ操作を実装するファイル
 */
#include "StageEditorViewport.h"
#include "StageEditor.h"
#include "StageEditorUiStyle.h"
#include "Camera.h"
#include "Input.h"
#include "Matrix4x4.h"
#include "WinApp.h"
#include <algorithm>
#include <cmath>
#ifdef USE_IMGUI
#include <imgui.h>
#endif

using namespace engine::graphics;

namespace engine::game {
namespace {
constexpr float kLeftPanelWidth = StageEditor::kLeftPanelWidth;
constexpr float kRightPanelWidth = StageEditor::kRightPanelWidth;
constexpr float kToolbarHeight = StageEditor::kToolbarHeight;
constexpr float kCameraSpeedPerSecond = 8.0f;
constexpr float kMinUnprojectW = 1e-8f; // 逆投影の w がこれより小さい時は0除算を避けてこの値にする
constexpr float kMinDepthDelta = 1e-6f; // 手前と奥の点の奥行きの差がこれより小さければ平面と交わらないとみなす
}

/** @brief 保持している参照とドラッグ状態を破棄する */
void StageEditorViewport::Reset() {
    camera_ = nullptr;
    imageMode_ = false;
}

/** @brief プレビュー画像の表示位置・大きさと、マウスが乗っているかを設定する */
void StageEditorViewport::SetImageRect(float x, float y, float width, float height, bool hovered) {
    imageMode_ = true;
    imageHovered_ = hovered;
    imageX_ = x; imageY_ = y;
    imageWidth_ = (std::max)(width, 1.0f);
    imageHeight_ = (std::max)(height, 1.0f);
}

/** @brief プレビュー画像上の座標を画面座標へ変換する */
Vector3 StageEditorViewport::ImageToScreen(float x, float y) const {
    if (!imageMode_) {
        return { x, y, 0.0f };
    }
    return { (x - imageX_) * WinApp::kClientWidth / imageWidth_,
        (y - imageY_) * WinApp::kClientHeight / imageHeight_, 0.0f };
}

/** @brief 画面座標をプレビュー画像上の座標へ変換する */
Vector3 StageEditorViewport::ScreenToImage(float x, float y) const {
    if (!imageMode_) {
        return { x, y, 0.0f };
    }
    return { imageX_ + x * imageWidth_ / WinApp::kClientWidth,
        imageY_ + y * imageHeight_ / WinApp::kClientHeight, 0.0f };
}

/** @brief エディタのビューポートの中心を返す */
Vector3 StageEditorViewport::ViewCenter() const {
    return ScreenToImage(WinApp::kClientWidth * 0.5f, WinApp::kClientHeight * 0.5f);
}

/** @brief キーボード入力で編集カメラを移動する */
void StageEditorViewport::UpdateCamera(Input* input, float deltaTime, bool focusMode) {
#ifdef USE_IMGUI
    // 文字入力中（ゲームビューの外）は飛行操作のキーを奪わない
    if (!camera_ || !input || (ImGui::GetIO().WantCaptureKeyboard && !imageHovered_)) {
        return;
    }

    // マウスがゲームビューの外にある間はカメラを動かさない
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    if (!Contains(mouse.x, mouse.y, focusMode)) {
        return;
    }

    Vector3 position = camera_->GetTranslate();

    // 中ボタンドラッグでカメラを平行移動（掴んだ地面が指に付いてくる速さになるよう奥行きで倍率を変える）
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        constexpr float kPanPerPixelPerDepth = 0.00064f;
        const ImVec2 delta = ImGui::GetIO().MouseDelta;
        const float panScale = (std::max)(1.0f, std::abs(position.z)) * kPanPerPixelPerDepth;
        position.x -= delta.x * panScale;
        position.y += delta.y * panScale;
    }

    // WASD/QEの飛行は右ボタンを押している間だけ（W/E/Rを変形ツールの切替に使うため）
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        camera_->SetTranslate(position);
        return;
    }
    /** @brief 飛行操作のキーと、押している間に動かす軸と向き */
    struct FlyKey {
        BYTE key;
        float Vector3::* axis;
        float sign;
    };
    static constexpr FlyKey kFlyKeys[] = {
        { DIK_A, &Vector3::x, -1.0f },
        { DIK_D, &Vector3::x, 1.0f },
        { DIK_W, &Vector3::y, 1.0f },
        { DIK_S, &Vector3::y, -1.0f },
        { DIK_Q, &Vector3::z, -1.0f },
        { DIK_E, &Vector3::z, 1.0f },
    };
    // 押しているキーの向きへ、1秒あたり一定の速さで動かす（同時押しはそれぞれ足す）
    for (const FlyKey& fly : kFlyKeys) {
        if (input->PushKey(fly.key)) {
            position.*fly.axis += fly.sign * kCameraSpeedPerSecond * deltaTime;
        }
    }
    camera_->SetTranslate(position);
#else
    (void)input;
    (void)deltaTime;
    (void)focusMode;
#endif
}

/** @brief マウス座標が操作可能な中央ビュー内にあるか返す */
bool StageEditorViewport::Contains(float mouseX, float mouseY, bool focusMode) const {
    if (imageMode_) {
        return imageHovered_ && mouseX >= imageX_ && mouseX < imageX_ + imageWidth_
            && mouseY >= imageY_ && mouseY < imageY_ + imageHeight_;
    }
    if (focusMode) {
        return mouseX >= 0.0f && mouseX < static_cast<float>(WinApp::kClientWidth)
            && mouseY >= 0.0f && mouseY < static_cast<float>(WinApp::kClientHeight);
    }
    return mouseX >= kLeftPanelWidth
        && mouseX < static_cast<float>(WinApp::kClientWidth) - kRightPanelWidth
        && mouseY >= kToolbarHeight
        && mouseY < static_cast<float>(WinApp::kClientHeight);
}

/** @brief スクリーン座標をゲーム平面上のワールド座標へ変換する */
bool StageEditorViewport::ScreenToGround(float mouseX, float mouseY, Vector3& outWorld) const {
    if (!camera_) {
        return false;
    }

    // 画面のレイを逆ビュー射影行列で復元し、ゲーム平面との交点を求める
    const Matrix4x4 inverseViewProjection = Inverse(camera_->GetViewProjectionMatrix());
    const Vector3 screen = ImageToScreen(mouseX, mouseY);
    const float ndcX = screen.x / static_cast<float>(WinApp::kClientWidth) * 2.0f - 1.0f;
    const float ndcY = 1.0f - screen.y / static_cast<float>(WinApp::kClientHeight) * 2.0f;

    // NDC の点を逆ビュー射影でワールドへ戻す（w が0に近い時は割り算が飛ばないよう下限を置く）
    auto unproject = [&](float ndcZ) -> Vector3 {
        float x = ndcX * inverseViewProjection.m[0][0] + ndcY * inverseViewProjection.m[1][0]
            + ndcZ * inverseViewProjection.m[2][0] + inverseViewProjection.m[3][0];
        float y = ndcX * inverseViewProjection.m[0][1] + ndcY * inverseViewProjection.m[1][1]
            + ndcZ * inverseViewProjection.m[2][1] + inverseViewProjection.m[3][1];
        float z = ndcX * inverseViewProjection.m[0][2] + ndcY * inverseViewProjection.m[1][2]
            + ndcZ * inverseViewProjection.m[2][2] + inverseViewProjection.m[3][2];
        float w = ndcX * inverseViewProjection.m[0][3] + ndcY * inverseViewProjection.m[1][3]
            + ndcZ * inverseViewProjection.m[2][3] + inverseViewProjection.m[3][3];
        if (std::abs(w) < kMinUnprojectW) {
            w = kMinUnprojectW;
        }
        return { x / w, y / w, z / w };
    };

    // 手前と奥の2点を結ぶ線が z=0 の平面と交わる位置を求める（平行ならあきらめる）
    const Vector3 nearPoint = unproject(0.0f);
    const Vector3 farPoint = unproject(1.0f);
    const float depthDelta = farPoint.z - nearPoint.z;
    if (std::abs(depthDelta) < kMinDepthDelta) {
        return false;
    }

    // 交点がカメラの後ろなら地面を指していない
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


#ifdef USE_IMGUI
namespace {
constexpr float kRotatePerPixel = 0.01f; // 回転ドラッグ1pxあたりのラジアン
constexpr float kScalePerPixel = 0.01f; // 拡縮ドラッグ1pxあたりの倍率変化
constexpr float kMinScale = 0.05f;
}

/** @brief 回転/拡縮ツールのドラッグを1フレームぶん反映する（マウス移動量のピクセル） */
void StageEditor::UpdateRotateScaleDrag(float deltaX, float deltaY) {
    if (selKind_ != SelKind::kObject || (deltaX == 0.0f && deltaY == 0.0f)) {
        return;
    }
    // 複数選択中の物すべてに同じ量だけ回転・拡縮をかける
    for (int index : selectedObjectIndices_) {
        if (index < 0 || index >= static_cast<int>(objects_.size())) {
            continue;
        }
        ObjectEntry& entry = objects_[index];
        ObjectDesc& desc = entry.desc;
        if (rotateDragging_) {
            // 右へ動かすと反時計回り。Ctrlを押していれば15度刻みに揃える
            constexpr float kRotateSnapRadians = 0.261799f;
            desc.rotation.z += -deltaX * kRotatePerPixel;
            if (ImGui::GetIO().KeyCtrl) {
                desc.rotation.z = std::round(desc.rotation.z / kRotateSnapRadians) * kRotateSnapRadians;
            }
        } else if (scaleDragging_) {
            // 右か上へ動かすと大きく、左か下へ動かすと小さくする（軸ハンドルならその軸だけ）
            const float factor = 1.0f + (deltaX - deltaY) * kScalePerPixel;
            if (scaleUniform_ || activeDragAxis_ == stageEditorUiStyle::kGizmoAxisFree) {
                desc.scale = desc.scale * factor;
            } else {
                constexpr float Vector3::* kComponents[] = { &Vector3::x, &Vector3::y, &Vector3::z };
                const int axis = activeDragAxis_ >= stageEditorUiStyle::kGizmoAxisX
                        && activeDragAxis_ <= stageEditorUiStyle::kGizmoAxisZ
                    ? activeDragAxis_ : stageEditorUiStyle::kGizmoAxisZ;
                desc.scale.*kComponents[axis - stageEditorUiStyle::kGizmoAxisX] *= factor;
            }
            // 0や負の大きさにならないよう下限で止める
            desc.scale.x = (std::max)(desc.scale.x, kMinScale);
            desc.scale.y = (std::max)(desc.scale.y, kMinScale);
            desc.scale.z = (std::max)(desc.scale.z, kMinScale);
        }
        if (ObjectKind::Of(desc.kind).IsVisual()) {
            RefreshTransforms(entry);
        }
    }
    MarkUndoDirty();
}

/** @brief WASD(+QEで奥/手前)でカメラを移動するImGuiのテキスト入力中は無効化する */
void StageEditor::UpdateFreeCamera(Input* input, float dt) {
    if (playTestMode_) {
        return;
    }
    viewport_.UpdateCamera(input, dt, viewportFocusMode_);
}

/** @brief マウススクリーン座標をゲーム平面(z=0)上のワールド座標へ変換する */
bool StageEditor::MouseToGround(float mouseX, float mouseY, Vector3& outWorld) const {
    return viewport_.ScreenToGround(mouseX, mouseY, outWorld);
}

/** @brief 選択物のワールド位置を返す（配置物/トリガー/外部エンティティ共通） */
bool StageEditor::SelectionWorldPosition(Vector3& outWorld) const {
    const ISelectionKind& selected = SelectionKindOf(selKind_);
    return !selected.IsScreenAnchor(*this, selIndex_) && selected.WorldPosition(*this, selIndex_, outWorld);
}
#endif

} // namespace engine::game
