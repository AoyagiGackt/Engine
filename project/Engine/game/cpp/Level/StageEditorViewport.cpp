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
#include "DiagnosticsDraw.h"
#include "UILayout.h"
#include <cstdio>
#include <string>
#include <imgui.h>
#endif

using namespace engine::graphics;

namespace engine::game {
namespace {
constexpr float kLeftPanelWidth = StageEditor::kLeftPanelWidth;
constexpr float kRightPanelWidth = StageEditor::kRightPanelWidth;
constexpr float kToolbarHeight = StageEditor::kToolbarHeight;
constexpr float kCameraMinSpeedPerSecond = 4.0f; // 地面に近い時の飛行速度
constexpr float kCameraSpeedPerDistance = 0.6f; // カメラと地面の距離1あたりの飛行速度
constexpr float kCameraFastMultiplier = 3.0f; // Shiftを押している間の飛行速度の倍率
constexpr float kPanPerPixelPerDepth = 0.00064f; // ゲーム画面1px・奥行き1あたりの平行移動量（掴んだ地面が指に付いてくる速さ）
constexpr float kWheelMinStep = 0.5f; // ホイール1回で動く最小の距離
constexpr float kWheelStepPerDistance = 0.12f; // ホイール1回で動く、カメラと地面の距離に対する割合
constexpr float kWheelMinDistance = 1.0f; // ホイールで地面へ寄れる限界の距離（これ以上は寄らない）
constexpr float kMinUnprojectW = 1e-8f; // 逆投影の w がこれより小さい時は0除算を避けてこの値にする
constexpr float kMinDepthDelta = 1e-6f; // 手前と奥の点の奥行きの差がこれより小さければ平面と交わらないとみなす
}

/** @brief 保持している参照とドラッグ状態を破棄する */
void StageEditorViewport::Reset() {
    camera_ = nullptr;
    imageMode_ = false;
    panning_ = false;
    flying_ = false;
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
    if (!camera_ || !input) {
        return;
    }

    // ビューの中で押した中ボタン・右ボタンは、離すまでビューの外へ出てもカメラ操作を続ける
    const ImGuiIO& io = ImGui::GetIO();
    const bool inside = Contains(io.MousePos.x, io.MousePos.y, focusMode);
    panning_ = ImGui::IsMouseDown(ImGuiMouseButton_Middle)
        && (panning_ || (inside && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)));
    // 文字入力中に右ボタンを押した時は飛行操作のキーを奪わない
    flying_ = ImGui::IsMouseDown(ImGuiMouseButton_Right)
        && (flying_ || (inside && !io.WantTextInput && ImGui::IsMouseClicked(ImGuiMouseButton_Right)));
    if (!panning_ && !flying_) {
        return;
    }

    Vector3 position = camera_->GetTranslate();
    const float distance = std::abs(position.z);

    // 中ボタンドラッグでカメラを平行移動（掴んだ地面が指に付いてくる速さになるよう奥行きで倍率を変える）
    if (panning_) {
        const float panScale = (std::max)(1.0f, distance) * kPanPerPixelPerDepth * ImagePixelScale();
        position.x -= io.MouseDelta.x * panScale;
        position.y += io.MouseDelta.y * panScale;
    }

    // WASD/QEの飛行は右ボタンを押している間だけ（W/E/Rを変形ツールの切替に使うため）
    if (!flying_) {
        camera_->SetTranslate(position);
        return;
    }
    // カメラが遠いほど速く動かす（Shiftでさらに速く）
    const float speed = (std::max)(kCameraMinSpeedPerSecond, distance * kCameraSpeedPerDistance)
        * (io.KeyShift ? kCameraFastMultiplier : 1.0f);
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
            position.*fly.axis += fly.sign * speed * deltaTime;
        }
    }
    camera_->SetTranslate(position);
#else
    (void)input;
    (void)deltaTime;
    (void)focusMode;
#endif
}

/** @brief マウスホイールでカーソル位置へ寄る（カメラが遠いほど1回の移動量を増やす） */
void StageEditorViewport::ZoomAtCursor(float wheel, float mouseX, float mouseY) {
    if (!camera_ || wheel == 0.0f) {
        return;
    }
    Vector3 position = camera_->GetTranslate();
    float step = (std::max)(kWheelMinStep, std::abs(position.z) * kWheelStepPerDistance) * wheel;
    // 寄る時は地面を突き抜けないよう手前で止める（カメラは z の負の側にある）
    if (step > 0.0f && position.z < 0.0f) {
        step = (std::min)(step, (std::max)(-position.z - kWheelMinDistance, 0.0f));
    }
    // カーソルの下の地面へ向かう線に沿って動かすと、その点が画面上で動かない
    Vector3 ground;
    if (std::abs(position.z) > std::abs(step) && ScreenToGround(mouseX, mouseY, ground)) {
        const float ratio = step / (ground.z - position.z);
        position.x += (ground.x - position.x) * ratio;
        position.y += (ground.y - position.y) * ratio;
    }
    position.z += step;
    camera_->SetTranslate(position);
}

/** @brief プレビュー画像の1ピクセルがゲーム画面の何ピクセルにあたるかを返す */
float StageEditorViewport::ImagePixelScale() const {
    return imageMode_ ? static_cast<float>(WinApp::kClientHeight) / imageHeight_ : 1.0f;
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
constexpr float kScalePerPixel = 0.01f; // 拡縮ドラッグ1pxあたりの倍率の変化（指数で効かせる）
constexpr float kMinScale = 0.05f;
constexpr float kScaleSnapStep = 0.1f; // Ctrlを押している間に揃える拡縮の間隔
constexpr float kRotateSnapRadians = 0.261799f; // Ctrlを押している間に揃える回転の間隔（15度）
constexpr float kRotateMinRadiusPx = 6.0f; // ハンドルの中心にこれより近い間は角度が暴れるので回さない
constexpr float kPi = 3.14159265f;
constexpr float kRadiansToDegrees = 180.0f / kPi;
constexpr float Vector3::* kScaleComponents[] = { &Vector3::x, &Vector3::y, &Vector3::z };

// 画面UIの印
constexpr float kUIHandleHalfSize = 7.0f; // 四角い印の半分の大きさ（表示上のpx）
constexpr float kUIHandleHoverHalfSize = 10.0f; // マウスが乗っている印の半分の大きさ
constexpr float kUIHandlePickRadiusPx = 15.0f; // これより近ければ印を掴める
constexpr float kUIHandleLabelGapX = 6.0f;
constexpr float kUIHandleLineThickness = 2.0f;
constexpr float kUISnapStepPx = 10.0f; // Ctrl+ドラッグで揃える間隔（ゲーム画面のpx）
constexpr float kUINudgePx = 1.0f; // 矢印キーで動かす量
constexpr float kUINudgeFastPx = 10.0f; // Shift+矢印キーで動かす量
constexpr int kUIHoverLabelMaxLines = 6; // マウスの下に重なった印の名前を並べる上限
constexpr ImU32 kUIHandleColor = IM_COL32(255, 150, 40, 200);
constexpr ImU32 kUIHandleHoverColor = IM_COL32(255, 255, 255, 255);
constexpr ImU32 kUIHandleSelectedColor = IM_COL32(255, 230, 60, 255);
constexpr ImU32 kUIHandleFill = IM_COL32(255, 150, 40, 70);
constexpr ImU32 kUIHandleOverlapLabelColor = IM_COL32(255, 190, 110, 255);
constexpr ImU32 kUIHandleHintColor = IM_COL32(190, 190, 190, 255);

// カーソル付近の数値表示
constexpr float kReadoutOffsetX = 18.0f;
constexpr float kReadoutOffsetY = 20.0f;
constexpr float kLabelPadding = 3.0f; // 文字の背景の余白
constexpr float kLabelRounding = 3.0f;
constexpr int kReadoutCapacity = 160;
constexpr ImU32 kLabelBackground = IM_COL32(0, 0, 0, 190);
constexpr ImU32 kReadoutTextColor = IM_COL32(255, 255, 255, 255);

/** @brief 読みやすいよう暗い背景を敷いて文字を描く */
void DrawLabelWithBackground(ImDrawList* drawList, const ImVec2& position, ImU32 color, const char* text) {
    const ImVec2 size = ImGui::CalcTextSize(text);
    drawList->AddRectFilled({ position.x - kLabelPadding, position.y - kLabelPadding },
        { position.x + size.x + kLabelPadding, position.y + size.y + kLabelPadding }, kLabelBackground, kLabelRounding);
    drawList->AddText(position, color, text);
}
}

/** @brief 回転/拡縮ツールのドラッグを反映する（ドラッグ開始時の値へ、開始からの累積量を掛ける） */
void StageEditor::UpdateRotateScaleDrag(float mouseX, float mouseY) {
    if (selKind_ != SelKind::kObject) {
        return;
    }
    const float totalX = mouseX - dragStartMouseX_;
    const float totalY = mouseY - dragStartMouseY_;
    const bool snap = ImGui::GetIO().KeyCtrl;

    // 回転: ハンドルの中心の周りをマウスが回った角度を足していく（画面の時計回りはZの負の向き）
    Vector3 world;
    ImVec2 center;
    if (rotateDragging_ && SelectionWorldPosition(world) && diagnosticsDraw::WorldToScreen(world, center)) {
        const float offsetX = mouseX - center.x;
        const float offsetY = mouseY - center.y;
        if (offsetX * offsetX + offsetY * offsetY >= kRotateMinRadiusPx * kRotateMinRadiusPx) {
            const float angle = std::atan2(offsetY, offsetX);
            if (rotateAngleValid_) {
                // 角度が -π と π の境目をまたいでも、差が短い向きになるよう直す
                float delta = angle - rotateLastAngle_;
                if (delta > kPi) {
                    delta -= kPi * 2.0f;
                } else if (delta < -kPi) {
                    delta += kPi * 2.0f;
                }
                rotateAccumRadians_ -= delta;
            }
            rotateLastAngle_ = angle;
            rotateAngleValid_ = true;
        }
    }

    // 拡縮: 全軸同時なら右か上へ、軸ハンドルなら矢印の向きへ動かすと大きくなる
    const bool uniform = scaleUniform_ || activeDragAxis_ == stageEditorUiStyle::kGizmoAxisFree;
    const float scalePixels = uniform ? totalX - totalY : AxisDragPixels(activeDragAxis_, totalX, totalY);
    const float factor = std::exp(scalePixels * kScalePerPixel);
    // Ctrl中は間隔に揃え、0や負の大きさにならないよう下限で止める
    const auto finishScale = [snap](float value) {
        const float snapped = snap ? std::round(value / kScaleSnapStep) * kScaleSnapStep : value;
        return (std::max)(snapped, kMinScale);
    };

    // 複数選択中の物すべてに同じ量だけ回転・拡縮をかける
    for (const DragStartTransform& start : dragStarts_) {
        if (start.index < 0 || start.index >= static_cast<int>(objects_.size())) {
            continue;
        }
        ObjectEntry& entry = objects_[start.index];
        ObjectDesc& desc = entry.desc;
        if (rotateDragging_) {
            const float rotated = start.rotation.z + rotateAccumRadians_;
            desc.rotation.z = snap ? std::round(rotated / kRotateSnapRadians) * kRotateSnapRadians : rotated;
        } else if (uniform) {
            desc.scale = { finishScale(start.scale.x * factor), finishScale(start.scale.y * factor), finishScale(start.scale.z * factor) };
        } else {
            float Vector3::* component = kScaleComponents[activeDragAxis_ - stageEditorUiStyle::kGizmoAxisX];
            desc.scale.*component = finishScale(start.scale.*component * factor);
        }
        if (ObjectKind::Of(desc.kind).IsVisual()) {
            RefreshTransforms(entry);
        }
    }
    MarkUndoDirty();
}

/** @brief ドラッグ中の移動量や数値をカーソルの近くに出す */
void StageEditor::DrawDragReadout() {
    if (!dragThresholdPassed_ || !IsViewportDragActive() || boxSelecting_ || !ImGui::IsMousePosValid()) {
        return;
    }
    const ImGuiIO& io = ImGui::GetIO();
    char text[kReadoutCapacity] = { };
    if (uiDragLayout_) {
        // 画面UIの印: ゲーム画面のpx座標と、掴んだ位置からの移動量
        UILayout::PositionHandle handle;
        if (UILayout::FindSelectedHandle(handle)) {
            std::snprintf(text, sizeof(text), "画面UI (%.0f, %.0f)  移動 (%+.0f, %+.0f)%s",
                handle.position.x, handle.position.y,
                handle.position.x - uiDragStartX_, handle.position.y - uiDragStartY_,
                io.KeyCtrl ? "  格子に揃え中" : "");
        }
    } else if (dragStarts_.empty()) {
        return;
    } else if (rotateDragging_ || scaleDragging_) {
        const DragStartTransform& start = dragStarts_.front();
        if (start.index < 0 || start.index >= static_cast<int>(objects_.size())) {
            return;
        }
        const ObjectDesc& desc = objects_[start.index].desc;
        if (rotateDragging_) {
            std::snprintf(text, sizeof(text), "回転 %.1f 度  変化 %+.1f 度%s",
                desc.rotation.z * kRadiansToDegrees, (desc.rotation.z - start.rotation.z) * kRadiansToDegrees,
                io.KeyCtrl ? "  15度刻み" : "");
        } else {
            std::snprintf(text, sizeof(text), "拡縮 (%.2f, %.2f, %.2f)%s",
                desc.scale.x, desc.scale.y, desc.scale.z, io.KeyCtrl ? "  0.1刻み" : "");
        }
    } else {
        const DragStartTransform& start = dragStarts_.front();
        if (SelectionKindOf(selKind_).IsScreenAnchor(*this, selIndex_)) {
            const Vector3& position = objects_[selIndex_].desc.position;
            std::snprintf(text, sizeof(text), "画面座標 (%.0f, %.0f)", position.x, position.y);
        } else {
            Vector3 world;
            if (!SelectionWorldPosition(world)) {
                return;
            }
            std::snprintf(text, sizeof(text), "位置 (%.2f, %.2f, %.2f)  移動 (%+.2f, %+.2f, %+.2f)%s",
                world.x, world.y, world.z, world.x - start.world.x, world.y - start.world.y, world.z - start.world.z,
                snapEnabled_ || io.KeyCtrl ? "  揃え中" : "");
        }
    }
    if (text[0] == '\0') {
        return;
    }
    // カーソルの右下に出す（ウィンドウの右端や下端では文字が切れないよう内側へ寄せる）
    const ImVec2 size = ImGui::CalcTextSize(text);
    const ImVec2 position = {
        (std::min)(io.MousePos.x + kReadoutOffsetX, io.DisplaySize.x - size.x - kLabelPadding),
        (std::min)(io.MousePos.y + kReadoutOffsetY, io.DisplaySize.y - size.y - kLabelPadding),
    };
    DrawLabelWithBackground(ImGui::GetForegroundDrawList(), position, kReadoutTextColor, text);
}

/** @brief 画面UI（UILayoutの位置項目）の印をクリック位置から探す */
bool StageEditor::PickUILayoutHandle(float mouseX, float mouseY, UILayout*& outLayout, size_t& outIndex) const {
    // 印を隠している間と、Altを押している間は下の配置物を選べるよう印を無視する
    if (!UILayout::IsEditingEnabled() || ImGui::GetIO().KeyAlt) {
        return false;
    }
    float bestDistanceSq = kUIHandlePickRadiusPx * kUIHandlePickRadiusPx;
    bool found = false;
    for (const UILayout::PositionHandle& handle : UILayout::CollectPositionHandles()) {
        const Vector3 image = viewport_.ScreenToImage(handle.position.x, handle.position.y);
        const float dx = image.x - mouseX;
        const float dy = image.y - mouseY;
        const float distanceSq = dx * dx + dy * dy;
        if (distanceSq <= bestDistanceSq) {
            bestDistanceSq = distanceSq;
            outLayout = handle.layout;
            outIndex = handle.index;
            found = true;
        }
    }
    return found;
}

/** @brief 画面UIの位置項目を四角い印で描く（名前はマウスが乗った印と選択中の印にだけ出す。DrawGizmosから呼ぶ） */
void StageEditor::DrawUILayoutHandles() {
    if (!UILayout::IsEditingEnabled()) {
        return;
    }
    ImDrawList* dl = diagnosticsDraw::GetDrawList();
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const std::vector<UILayout::PositionHandle> handles = UILayout::CollectPositionHandles();

    // 印を描く
    for (const UILayout::PositionHandle& handle : handles) {
        const bool selected = UILayout::IsSelectedHandle(handle.layout, handle.index);
        const bool hovered = uiHoverLayout_ == handle.layout && uiHoverIndex_ == handle.index;
        const ImU32 color = hovered ? kUIHandleHoverColor : (selected ? kUIHandleSelectedColor : kUIHandleColor);
        const float half = hovered ? kUIHandleHoverHalfSize : kUIHandleHalfSize;
        const Vector3 image = viewport_.ScreenToImage(handle.position.x, handle.position.y);
        const ImVec2 minCorner = { image.x - half, image.y - half };
        const ImVec2 maxCorner = { image.x + half, image.y + half };
        dl->AddRectFilled(minCorner, maxCorner, kUIHandleFill);
        dl->AddRect(minCorner, maxCorner, color, 0.0f, 0, kUIHandleLineThickness);
    }

    // 選択中の印には名前と座標を出す
    const float lineHeight = ImGui::GetTextLineHeightWithSpacing() + kLabelPadding;
    for (const UILayout::PositionHandle& handle : handles) {
        if (UILayout::IsSelectedHandle(handle.layout, handle.index)) {
            const Vector3 image = viewport_.ScreenToImage(handle.position.x, handle.position.y);
            const std::string label = handle.label + "  (" + std::to_string(static_cast<int>(std::round(handle.position.x)))
                + ", " + std::to_string(static_cast<int>(std::round(handle.position.y))) + ")";
            DrawLabelWithBackground(dl, { image.x + kUIHandleHoverHalfSize + kUIHandleLabelGapX, image.y - lineHeight * 0.5f },
                kUIHandleSelectedColor, label.c_str());
        }
    }

    // マウスの下に重なった印の名前を縦に並べる（一番近い印を白で示す）
    if (!uiHoverLayout_ || IsViewportDragActive()) {
        return;
    }
    ImVec2 labelPosition = { mouse.x + kReadoutOffsetX, mouse.y + kReadoutOffsetY };
    int lineCount = 0;
    for (const UILayout::PositionHandle& handle : handles) {
        const Vector3 image = viewport_.ScreenToImage(handle.position.x, handle.position.y);
        const float dx = image.x - mouse.x;
        const float dy = image.y - mouse.y;
        if (dx * dx + dy * dy > kUIHandlePickRadiusPx * kUIHandlePickRadiusPx || lineCount >= kUIHoverLabelMaxLines) {
            continue;
        }
        const bool nearest = uiHoverLayout_ == handle.layout && uiHoverIndex_ == handle.index;
        DrawLabelWithBackground(dl, labelPosition, nearest ? kUIHandleHoverColor : kUIHandleOverlapLabelColor, handle.label.c_str());
        labelPosition.y += lineHeight;
        ++lineCount;
    }
    DrawLabelWithBackground(dl, labelPosition, kUIHandleHintColor, "ドラッグで移動 / Altで下の物を選ぶ / Uで印を隠す");
}

/** @brief 画面UIの印のドラッグを始める */
void StageEditor::BeginUILayoutDrag(UILayout* layout, size_t index, float mouseX, float mouseY) {
    // 矢印キーの対象が印へ切り替わるよう、配置物の選択を外してから印を選ぶ
    ClearSelection();
    UILayout::SetSelectedHandle(layout, index);
    BeginUndoCapture();
    CaptureDragStart(mouseX, mouseY);
    // 掴んだ点と印のずれを覚えて、掴んだ瞬間に印が飛ばないようにする
    UILayout::PositionHandle handle;
    if (UILayout::FindSelectedHandle(handle)) {
        const Vector3 logicalMouse = viewport_.ImageToScreen(mouseX, mouseY);
        uiDragGrabOffsetX_ = handle.position.x - logicalMouse.x;
        uiDragGrabOffsetY_ = handle.position.y - logicalMouse.y;
        uiDragStartX_ = handle.position.x;
        uiDragStartY_ = handle.position.y;
    }
    uiDragLayout_ = layout;
    uiDragIndex_ = index;
}

/** @brief 画面UIの印をマウスへ追従させる（Ctrlで格子に揃え、Shiftで動きの大きい軸だけ動かす） */
void StageEditor::UpdateUILayoutDrag(float mouseX, float mouseY) {
    const ImGuiIO& io = ImGui::GetIO();
    const Vector3 logicalMouse = viewport_.ImageToScreen(mouseX, mouseY);
    float x = logicalMouse.x + uiDragGrabOffsetX_;
    float y = logicalMouse.y + uiDragGrabOffsetY_;
    if (io.KeyShift) {
        if (std::abs(x - uiDragStartX_) >= std::abs(y - uiDragStartY_)) {
            y = uiDragStartY_;
        } else {
            x = uiDragStartX_;
        }
    }
    const float step = io.KeyCtrl ? kUISnapStepPx : 1.0f;
    uiDragLayout_->SetPosition(uiDragIndex_, { std::round(x / step) * step, std::round(y / step) * step });
    MarkUndoDirty();
}

/** @brief 選択中の画面UIの印を矢印キーで動かす */
bool StageEditor::NudgeSelectedUILayoutHandle(float dirX, float dirY) {
    // 配置物を選んでいる間は配置物の微調整を優先する
    if (selKind_ != SelKind::kNone || !UILayout::IsEditingEnabled()) {
        return false;
    }
    UILayout::PositionHandle handle;
    if (!UILayout::FindSelectedHandle(handle)) {
        return false;
    }
    const float step = ImGui::GetIO().KeyShift ? kUINudgeFastPx : kUINudgePx;
    RecordUndoSnapshotNow();
    handle.layout->SetPosition(handle.index, { handle.position.x + dirX * step, handle.position.y - dirY * step });
    return true;
}

/** @brief 画面UIの印とパネルの表示を切り替える（Uキー） */
void StageEditor::ToggleUILayoutEditing() {
    const bool enabled = !UILayout::IsEditingEnabled();
    UILayout::SetEditingEnabled(enabled);
    if (!enabled) {
        UILayout::SetSelectedHandle(nullptr, 0);
    }
    ClearViewportHover();
    statusMessage_ = enabled ? "画面UIの印を表示しました (U)" : "画面UIの印を隠しました (U)";
    statusTimer_ = StageEditor::kStatusShortSeconds;
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
