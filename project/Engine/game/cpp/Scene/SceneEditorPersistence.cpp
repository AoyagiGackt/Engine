/** @file SceneEditorPersistence.cpp
 * @brief カメラ設定とUIレイアウトの保存と復元、UI要素の編集履歴と画面上の編集枠
 */
#include "SceneEditor.h"
#include "GameConstants.h"
#include "JsonHelper.h"
#include <algorithm>
#include <cmath>
#include <utility>
#ifdef USE_IMGUI
#include <imgui.h>
#endif
using namespace engine::graphics;
namespace engine::game {
#ifdef USE_IMGUI
namespace {
// 画面上の編集枠の見た目と掴める範囲
constexpr float kGizmoHandleHalfSize = 5.0f; // 四角い掴みの半分の大きさ（ピクセル）
constexpr float kGizmoHandleGrabRadius = 12.0f; // 掴みを押せる距離（ピクセル）
constexpr float kGizmoThickness = 2.0f;
constexpr ImU32 kGizmoColor = IM_COL32(255, 200, 80, 255);
constexpr ImU32 kGizmoHandleColor = IM_COL32(255, 255, 255, 255);

/** @brief 掴みの位置（辺の向き）と、その時のカーソルの形 */
struct GizmoHandle {
    int x;
    int y;
    ImGuiMouseCursor cursor;
};
constexpr GizmoHandle kGizmoHandles[] = {
    { -1, -1, ImGuiMouseCursor_ResizeNWSE }, { 0, -1, ImGuiMouseCursor_ResizeNS }, { 1, -1, ImGuiMouseCursor_ResizeNESW },
    { -1, 0, ImGuiMouseCursor_ResizeEW }, { 1, 0, ImGuiMouseCursor_ResizeEW },
    { -1, 1, ImGuiMouseCursor_ResizeNESW }, { 0, 1, ImGuiMouseCursor_ResizeNS }, { 1, 1, ImGuiMouseCursor_ResizeNWSE },
};

/** @brief ImGuiの画面座標とゲーム画面の座標の対応（アスペクト比を保った拡大表示を含む） */
struct ScreenMapping {
    float scale;
    float offsetX;
    float offsetY;
};

/** @brief 今のウィンドウの大きさから座標の対応を求める */
ScreenMapping GetScreenMapping() {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float gameWidth = static_cast<float>(gameConstants::kScreenWidth);
    const float gameHeight = static_cast<float>(gameConstants::kScreenHeight);
    const float scale = (std::min)(display.x / gameWidth, display.y / gameHeight);
    return { scale, (display.x - gameWidth * scale) * 0.5f, (display.y - gameHeight * scale) * 0.5f };
}

/** @brief ゲーム画面の座標をImGuiの画面座標へ変換する */
ImVec2 ToImGui(const ScreenMapping& mapping, const Vector2& point) {
    return ImVec2(point.x * mapping.scale + mapping.offsetX, point.y * mapping.scale + mapping.offsetY);
}

/** @brief ImGuiの画面座標をゲーム画面の座標へ変換する */
Vector2 ToGame(const ScreenMapping& mapping, const ImVec2& point) {
    return { (point.x - mapping.offsetX) / mapping.scale, (point.y - mapping.offsetY) / mapping.scale };
}

/** @brief 点を原点まわりに回す */
Vector2 Rotate(const Vector2& point, float angle) {
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    return { point.x * c - point.y * s, point.x * s + point.y * c };
}

/** @brief スプライト上の点（-1〜1の辺の向き）のゲーム画面の座標を返す */
Vector2 SpritePoint(const Sprite& sprite, float sideX, float sideY) {
    const Vector2& position = sprite.GetPosition();
    const Vector2& size = sprite.GetSize();
    const Vector2& anchor = sprite.GetAnchorPoint();
    const Vector2 local = { size.x * ((sideX + 1.0f) * 0.5f - anchor.x), size.y * ((sideY + 1.0f) * 0.5f - anchor.y) };
    const Vector2 rotated = Rotate(local, sprite.GetRotation());
    return { position.x + rotated.x, position.y + rotated.y };
}

/** @brief 点がスプライトの中にあるか（回転も考慮する） */
bool SpriteContains(const Sprite& sprite, const Vector2& point) {
    const Vector2& position = sprite.GetPosition();
    const Vector2& size = sprite.GetSize();
    const Vector2& anchor = sprite.GetAnchorPoint();
    const Vector2 local = Rotate({ point.x - position.x, point.y - position.y }, -sprite.GetRotation());
    const float minX = -anchor.x * size.x;
    const float minY = -anchor.y * size.y;
    return local.x >= minX && local.x <= minX + size.x && local.y >= minY && local.y <= minY + size.y;
}

/** @brief 値を格子に合わせる */
float SnapToGrid(float value, float step) {
    return std::round(value / step) * step;
}
} // namespace
#endif

/** @brief UI要素を選択し、インスペクタを切り替える */
void SceneEditor::SelectUIElement(int index) {
    selection_ = Selection::kUIElement;
    selectionIndex_ = index;
    currentState_ = std::make_unique<UIElementState>();
}

/** @brief 全UI要素の編集値を取り出す */
std::vector<SceneEditor::UISpriteState> SceneEditor::CaptureUIState() const {
    std::vector<UISpriteState> states;
    states.reserve(uiElements_.size());
    for (const UIEntry& entry : uiElements_) {
        states.push_back({ entry.sprite->GetPosition(), entry.sprite->GetSize(), entry.sprite->GetRotation(), entry.sprite->GetColor() });
    }
    return states;
}

/** @brief 取り出しておいた編集値をUI要素へ書き戻す（数が違えば何もしない） */
void SceneEditor::RestoreUIState(const std::vector<UISpriteState>& states) {
    if (states.size() != uiElements_.size()) {
        return;
    }
    for (size_t i = 0; i < states.size(); ++i) {
        Sprite* sprite = uiElements_[i].sprite.get();
        sprite->SetPosition(states[i].position);
        sprite->SetSize(states[i].size);
        sprite->SetRotation(states[i].rotation);
        sprite->SetColor(states[i].color);
    }
    uiDirty_ = true;
}

/** @brief UI要素の編集を1つ戻す */
void SceneEditor::UndoUIEdit() {
    // ドラッグの途中は履歴の順番が崩れるので受け付けない
    if (uiHistory_.IsCapturing()) {
        return;
    }
    if (const auto snapshot = uiHistory_.Undo(CaptureUIState())) {
        RestoreUIState(*snapshot);
    }
}

/** @brief 戻したUI要素の編集をやり直す */
void SceneEditor::RedoUIEdit() {
    if (uiHistory_.IsCapturing()) {
        return;
    }
    if (const auto snapshot = uiHistory_.Redo(CaptureUIState())) {
        RestoreUIState(*snapshot);
    }
}

/** @brief 直前のImGui項目の操作開始・変更・終了に合わせて履歴を記録する */
bool SceneEditor::TrackUIEdit(bool changed) {
#ifdef USE_IMGUI
    if (ImGui::IsItemActivated()) {
        uiHistory_.Begin(CaptureUIState());
    }
    if (changed) {
        uiHistory_.MarkChanged();
        uiDirty_ = true;
    }
    if (ImGui::IsItemDeactivated()) {
        uiHistory_.Commit();
    }
#endif
    return changed;
}

/** @brief 掴んでいるUI要素をマウス位置に合わせて動かす */
void SceneEditor::UpdateUIDrag() {
#ifdef USE_IMGUI
    const ImGuiIO& io = ImGui::GetIO();
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) || uiDrag_.index < 0 || uiDrag_.index >= static_cast<int>(uiElements_.size())) {
        // 離したら1回分の履歴にする（カーソルがウィンドウの外へ出ていても押している間は続く）
        if (uiDrag_.moved) {
            uiHistory_.Commit();
        }
        uiDrag_ = UIDragState { };
        return;
    }
    // ゲーム側へクリックが抜けないようにする
    ImGui::SetNextFrameWantCaptureMouse(true);
    // しきい値を超えるまでは動かさない（クリックだけで位置がずれないように）
    if (!uiDrag_.moved) {
        if (!ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            return;
        }
        uiDrag_.moved = true;
        uiHistory_.Begin(CaptureUIState());
    }
    Sprite* sprite = uiElements_[uiDrag_.index].sprite.get();
    const Vector2 mouse = ToGame(GetScreenMapping(), io.MousePos);
    const Vector2 delta = { mouse.x - uiDrag_.startMouse.x, mouse.y - uiDrag_.startMouse.y };
    // スナップはチェックボックスの状態を Ctrl で一時的に反転する
    const bool snapping = uiSnapEnabled_ != io.KeyCtrl;
    const float step = static_cast<float>(uiSnapStep_);
    const auto snap = [&](float value) {
        return snapping ? SnapToGrid(value, step) : value;
    };

    if (uiDrag_.handleX == 0 && uiDrag_.handleY == 0) {
        // 移動
        sprite->SetPosition({ snap(uiDrag_.startPosition.x + delta.x), snap(uiDrag_.startPosition.y + delta.y) });
    } else {
        // リサイズ（スプライトの向きに沿った座標で、掴んだ辺だけを動かす）
        const float rotation = sprite->GetRotation();
        const Vector2& anchor = sprite->GetAnchorPoint();
        const Vector2 localDelta = Rotate(delta, -rotation);
        float minX = -anchor.x * uiDrag_.startSize.x;
        float minY = -anchor.y * uiDrag_.startSize.y;
        float maxX = minX + uiDrag_.startSize.x;
        float maxY = minY + uiDrag_.startSize.y;
        // 動かす辺は、向きに沿った座標のままゲーム画面の格子へ合わせる（反対の辺を押し越さない）
        if (uiDrag_.handleX < 0) {
            minX = (std::min)(snap(uiDrag_.startPosition.x + minX + localDelta.x) - uiDrag_.startPosition.x, maxX - kMinSpritePixels);
        } else if (uiDrag_.handleX > 0) {
            maxX = (std::max)(snap(uiDrag_.startPosition.x + maxX + localDelta.x) - uiDrag_.startPosition.x, minX + kMinSpritePixels);
        }
        if (uiDrag_.handleY < 0) {
            minY = (std::min)(snap(uiDrag_.startPosition.y + minY + localDelta.y) - uiDrag_.startPosition.y, maxY - kMinSpritePixels);
        } else if (uiDrag_.handleY > 0) {
            maxY = (std::max)(snap(uiDrag_.startPosition.y + maxY + localDelta.y) - uiDrag_.startPosition.y, minY + kMinSpritePixels);
        }
        // Shift+角は掴む前の縦横比を保つ（幅を基準に高さを決める）
        if (io.KeyShift && uiDrag_.handleX != 0 && uiDrag_.handleY != 0 && uiDrag_.startSize.x > 0.0f) {
            const float height = (maxX - minX) * uiDrag_.startSize.y / uiDrag_.startSize.x;
            if (uiDrag_.handleY < 0) {
                minY = maxY - height;
            } else {
                maxY = minY + height;
            }
        }
        const Vector2 newSize = { std::clamp(maxX - minX, kMinSpritePixels, kMaxSpritePixels), std::clamp(maxY - minY, kMinSpritePixels, kMaxSpritePixels) };
        // 動かさなかった辺が同じ場所に残るよう、基準点の位置を求め直す
        const Vector2 anchorOffset = Rotate({ minX + anchor.x * newSize.x, minY + anchor.y * newSize.y }, rotation);
        sprite->SetSize(newSize);
        sprite->SetPosition({ uiDrag_.startPosition.x + anchorOffset.x, uiDrag_.startPosition.y + anchorOffset.y });
    }
    uiHistory_.MarkChanged();
    uiDirty_ = true;
#endif
}

/** @brief ゲーム画面の上でUI要素を選択・移動・リサイズする枠を処理して描く */
void SceneEditor::UpdateUIGizmo() {
#ifdef USE_IMGUI
    const ImGuiIO& io = ImGui::GetIO();
    const ScreenMapping mapping = GetScreenMapping();
    if (mapping.scale <= 0.0f) {
        return;
    }
    if (uiDrag_.active) {
        UpdateUIDrag();
    }

    const bool hasSelection = selection_ == Selection::kUIElement && selectionIndex_ >= 0 && selectionIndex_ < static_cast<int>(uiElements_.size());
    const Sprite* selected = hasSelection ? uiElements_[selectionIndex_].sprite.get() : nullptr;

    // ImGuiのウィンドウの上にカーソルがある時は枠の操作をしない
    if (!uiDrag_.active && !io.WantCaptureMouse) {
        // どのウィンドウにもフォーカスが無ければ、枠の操作の続きとしてキーで戻せるようにする
        if (hasSelection && !io.WantTextInput && io.KeyCtrl && !ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow)) {
            if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
                UndoUIEdit();
            } else if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) {
                RedoUIEdit();
            }
        }

        // 選択中の枠の掴みを最優先で探す
        const GizmoHandle* hovered = nullptr;
        if (selected != nullptr) {
            float nearest = kGizmoHandleGrabRadius;
            for (const GizmoHandle& handle : kGizmoHandles) {
                const ImVec2 screen = ToImGui(mapping, SpritePoint(*selected, static_cast<float>(handle.x), static_cast<float>(handle.y)));
                const float distance = std::hypot(screen.x - io.MousePos.x, screen.y - io.MousePos.y);
                if (distance <= nearest) {
                    nearest = distance;
                    hovered = &handle;
                }
            }
        }
        if (hovered != nullptr) {
            ImGui::SetMouseCursor(hovered->cursor);
        }
        // 掴みが無ければ、選択中→手前に描かれた物の順に押された要素を探す
        const Vector2 mouse = ToGame(mapping, io.MousePos);
        int pressedIndex = (selected != nullptr && (hovered != nullptr || SpriteContains(*selected, mouse))) ? selectionIndex_ : -1;
        for (int i = static_cast<int>(uiElements_.size()) - 1; i >= 0 && pressedIndex < 0; --i) {
            if (SpriteContains(*uiElements_[i].sprite, mouse)) {
                pressedIndex = i;
            }
        }
        if (pressedIndex >= 0 && hovered == nullptr) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        }
        if (pressedIndex >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (pressedIndex != selectionIndex_ || !hasSelection) {
                SelectUIElement(pressedIndex);
            }
            const Sprite* sprite = uiElements_[pressedIndex].sprite.get();
            uiDrag_ = UIDragState { };
            uiDrag_.active = true;
            uiDrag_.index = pressedIndex;
            uiDrag_.handleX = hovered != nullptr ? hovered->x : 0;
            uiDrag_.handleY = hovered != nullptr ? hovered->y : 0;
            uiDrag_.startMouse = mouse;
            uiDrag_.startPosition = sprite->GetPosition();
            uiDrag_.startSize = sprite->GetSize();
        }
    }

    // 枠と掴みを描く（ImGuiのウィンドウより奥、ゲーム画面より手前）
    const bool drawSelected = selection_ == Selection::kUIElement && selectionIndex_ >= 0 && selectionIndex_ < static_cast<int>(uiElements_.size());
    if (!drawSelected) {
        return;
    }
    const Sprite& sprite = *uiElements_[selectionIndex_].sprite;
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    drawList->AddQuad(ToImGui(mapping, SpritePoint(sprite, -1.0f, -1.0f)), ToImGui(mapping, SpritePoint(sprite, 1.0f, -1.0f)),
        ToImGui(mapping, SpritePoint(sprite, 1.0f, 1.0f)), ToImGui(mapping, SpritePoint(sprite, -1.0f, 1.0f)), kGizmoColor, kGizmoThickness);
    for (const GizmoHandle& handle : kGizmoHandles) {
        const ImVec2 center = ToImGui(mapping, SpritePoint(sprite, static_cast<float>(handle.x), static_cast<float>(handle.y)));
        drawList->AddRectFilled({ center.x - kGizmoHandleHalfSize, center.y - kGizmoHandleHalfSize },
            { center.x + kGizmoHandleHalfSize, center.y + kGizmoHandleHalfSize }, kGizmoHandleColor);
    }
#endif
}

/** @brief カメラの目標位置・角度・スムージングフレーム数をJSONへ書き出す */
void SceneEditor::SaveCameraParams(const EditContext& ctx) {
    const Vector3& pos = *ctx.cameraTargetPos;
    const Vector3& rot = *ctx.cameraTargetRot;
    nlohmann::json j;
    j["camera_pos"] = { pos.x, pos.y, pos.z };
    j["camera_rot"] = { rot.x, rot.y, rot.z };
    j["camera_smooth_frames"] = *ctx.cameraSmoothFrames;
    JsonHelper::Save("Resources/editor_camera.json", j);
}

// カメラの位置・角度・スムージングフレーム数を JSON ファイルから読み込む
void SceneEditor::LoadCameraParams(const EditContext& ctx) {
    auto j = JsonHelper::Load("Resources/editor_camera.json");
    if (j.empty()) {
        return;
    }

    Vector3& pos = *ctx.cameraTargetPos;
    Vector3& rot = *ctx.cameraTargetRot;

    if (j.contains("camera_pos") && j["camera_pos"].is_array()) {
        pos = { j["camera_pos"][0], j["camera_pos"][1], j["camera_pos"][2] };
    }
    if (j.contains("camera_rot") && j["camera_rot"].is_array()) {
        rot = { j["camera_rot"][0], j["camera_rot"][1], j["camera_rot"][2] };
    }
    *ctx.cameraSmoothFrames = j.value("camera_smooth_frames", *ctx.cameraSmoothFrames);

    ctx.camera->SetTranslate(pos);
    ctx.camera->SetRotate(rot);
    ctx.cameraPosHistory->clear();
    ctx.cameraRotHistory->clear();
}

// UI スプライトのレイアウト（位置・サイズ・色・テクスチャパスなど）を JSON に書き出す
void SceneEditor::SaveUILayout() {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : uiElements_) {
        Sprite* sp = e.sprite.get();
        Vector2 pos = sp->GetPosition();
        Vector2 sz = sp->GetSize();
        Vector4 col = sp->GetColor();
        float rot = sp->GetRotation();
        arr.push_back({ { "name", e.name },
            { "tex", e.texPath },
            { "pos", { pos.x, pos.y } },
            { "size", { sz.x, sz.y } },
            { "rot", rot },
            { "color", { col.x, col.y, col.z, col.w } } });
    }
    JsonHelper::Save("Resources/Config/editor_ui.json", arr);
    uiDirty_ = false;
}

// 起動時に1度だけ呼ばれ、カメラパラメータと UI レイアウトを一括で読み込む
void SceneEditor::LoadAll(const EditContext& ctx) {
    LoadCameraParams(ctx);
    LoadUILayout(ctx);
}

// UI スプライトのレイアウトを JSON ファイルから復元する
void SceneEditor::LoadUILayout(const EditContext& ctx) {
    auto j = JsonHelper::Load("Resources/Config/editor_ui.json");
    if (!j.is_array()) {
        return;
    }

    uiElements_.clear();
    ChangeState(Selection::kNone);
    // 読み込むと要素が作り直されるので、履歴と掴んでいる状態も消す
    uiHistory_.Clear();
    uiDrag_ = UIDragState { };
    uiDirty_ = false;

    for (const auto& item : j) {
        UIEntry entry;
        entry.name = item.value("name", std::string("UI Element"));
        entry.texPath = item.value("tex", std::string(""));

        entry.sprite = std::make_unique<Sprite>();
        entry.sprite->Initialize(ctx.spriteCommon, entry.texPath);

        if (item.contains("pos") && item["pos"].is_array()) {
            entry.sprite->SetPosition({ item["pos"][0], item["pos"][1] });
        }
        if (item.contains("size") && item["size"].is_array()) {
            entry.sprite->SetSize({ item["size"][0], item["size"][1] });
        }
        entry.sprite->SetRotation(item.value("rot", 0.0f));
        if (item.contains("color") && item["color"].is_array()) {
            const auto& c = item["color"];
            entry.sprite->SetColor({ c[0], c[1], c[2], c[3] });
        }

        uiElements_.push_back(std::move(entry));
    }
}

}
