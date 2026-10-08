/**
 * @file StageEditorGameplayInspector.cpp
 * @brief 配置物のゲーム動作・文章UIの編集
 */
#ifdef USE_IMGUI
#include "StageEditorPanels.h"
#include "Camera.h"
#include "EditorUI.h"
#include "EnemyEntity.h"
#include "GameFlags.h"
#include "KnightEnemy.h"
#include "SceneShared.h"
#include "StageEditor.h"
#include "StageEditorUiStyle.h"
#include "WinApp.h"
#include <algorithm>
#include <commdlg.h>
#include <cstring>
#include <imgui.h>
#pragma comment(lib, "comdlg32.lib")

namespace {
namespace EditorUi = engine::game::StageEditorUiStyle;

constexpr float kTextBoxHeight = 140.0f;
constexpr float kMaxTextScale = 10.0f;

std::string OpenFileDialog(const char* filter, const char* initialDirectory)
{
    char path[MAX_PATH] = { };
    OPENFILENAMEA dialog = { };
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = path;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrInitialDir = initialDirectory;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameA(&dialog) ? std::string(path) : std::string { };
}

std::string ToProjectRelativePath(const std::string& absolutePath)
{
    std::string path = absolutePath;
    std::replace(path.begin(), path.end(), '\\', '/');
    const size_t resourcesPosition = path.find("Resources/");
    return resourcesPosition == std::string::npos ? path : path.substr(resourcesPosition);
}

} // namespace

namespace engine::game {
using namespace engine::graphics;

// screen座標のui_textが編集パネル（ツールバー/左カラム/右インスペクタ）の下に隠れて
// 3Dビュー上でドラッグできない場合に警告し、見える位置へ逃がすボタンを出す
void StageEditorInspectorPanel::RenderObjectGameplay(StageEditor& editor, bool& structuralDirty)
{
    auto& desc = editor.objects_[editor.selIndex_].desc;
    auto captureItemUndo = [&](bool changed) {
        if (ImGui::IsItemActivated()) {
            editor.BeginUndoCapture();
        }
        if (changed) {
            editor.MarkUndoDirty();
        }
        if (ImGui::IsItemDeactivated()) {
            editor.CommitUndoCapture();
        }
        return changed;
    };
    const ObjectKind& kind = ObjectKind::Of(desc.kind);
    kind.DrawGameplayInspector(editor, editor.selIndex_, desc, structuralDirty, captureItemUndo);

    // テスト中に今どうなっているかを確認し、必要ならその場で初期状態へ戻す
    if (ImGui::CollapsingHeader("ゲーム中の状態")) {
        auto& entry = editor.objects_[editor.selIndex_];
        ImGui::Text("有効: %s", entry.runtimeActive ? "はい" : "いいえ");
        if (!desc.activationFlag.empty()) {
            ImGui::Text("有効化フラグ %s: %s", desc.activationFlag.c_str(),
                GameFlags::GetInstance()->GetFlag(desc.activationFlag) ? "ON" : "OFF");
        }
        if (entry.enabledOverride >= 0) {
            ImGui::TextColored(EditorUi::kWarningColor, "グラフによる上書き: %s", entry.enabledOverride == 1 ? "有効" : "無効");
        }
        kind.DrawRuntimeStatus(editor, editor.selIndex_);
        if (entry.enemy) {
            ImGui::Text("敵HP: %d / %d  %s", entry.enemy->GetHp(), entry.enemy->GetMaxHp(), entry.enemy->IsDefeated() ? "(撃破済み)" : "");
        }
        if (entry.knight) {
            ImGui::Text("ナイトHP: %d / %d  %s", entry.knight->GetHp(), entry.knight->GetMaxHp(), entry.knight->IsAlive() ? "" : "(撃破済み)");
        }
        if (ImGui::Button("この配置物を初期状態に戻す", ImVec2(-1.0f, 0.0f))) {
            editor.RegenerateInstances(entry);
        }
        EditorUI::HelpMarker("回収済み・破壊済み・敵のHP・グラフによる上書きをリセットして実体を作り直します");
    }
}

void StageEditorInspectorPanel::RenderObjectText(StageEditor& editor)
{
    auto& desc = editor.objects_[editor.selIndex_].desc;
    if (desc.kind != ObjectKindName::kUIText) {
        return;
    }
    ImGui::PushID(desc.name.c_str());
    auto captureItemUndo = [&](bool changed) {
        if (ImGui::IsItemActivated()) {
            editor.BeginUndoCapture();
        }
        if (changed) {
            editor.MarkUndoDirty();
        }
        if (ImGui::IsItemDeactivated()) {
            editor.CommitUndoCapture();
        }
        return changed;
    };

    ImGui::SeparatorText("文章UI");
    ImGui::TextWrapped("文章を入力するとゲーム画面に反映されます。Enterで改行できます。");
    // 長い文章を編集したときも、固定長のバッファで末尾を切り捨てない。
    std::vector<char> textBuf(desc.text.size() + 4096, '\0');
    std::copy(desc.text.begin(), desc.text.end(), textBuf.begin());
    if (editor.focusTextEditor_) {
        ImGui::SetKeyboardFocusHere();
        editor.focusTextEditor_ = false;
    }
    if (captureItemUndo(ImGui::InputTextMultiline("##文章内容", textBuf.data(), textBuf.size(), ImVec2(-1.0f, kTextBoxHeight)))) {
        desc.text = textBuf.data();
    }
    ImGui::TextDisabled("画面の十字をドラッグして配置 / Ctrl+Sで保存");

    captureItemUndo(ImGui::ColorEdit4("色", &desc.textColor.x));
    bool bold = desc.textBold;
    if (ImGui::Checkbox("太字", &bold)) {
        editor.RecordUndoSnapshotNow();
        desc.textBold = bold;
    }
    ImGui::SameLine();
    bool shadow = desc.textShadow;
    if (ImGui::Checkbox("文字の影", &shadow)) {
        editor.RecordUndoSnapshotNow();
        desc.textShadow = shadow;
    }
    captureItemUndo(ImGui::DragFloat("大きさ", &desc.textScale, EditorUi::kDragStepFine, EditorUi::kMinRadius, kMaxTextScale));

    // TextSpace の並び順と一致させる
    const char* spaceLabels[] = { "画面座標(px)", "ワールド座標" };
    int spaceIndex = static_cast<int>(desc.textSpace);
    if (ImGui::Combo("座標基準", &spaceIndex, spaceLabels, 2)) {
        const TextSpace newSpace = static_cast<TextSpace>(spaceIndex);
        if (newSpace != desc.textSpace) {
            editor.RecordUndoSnapshotNow();
            // 座標基準の切り替え時にposition(px⇔ワールド単位)を素通りさせると、
            // 数値のスケールが噛み合わずカメラ範囲外へ飛んで見えなくなるため、その場でスクリーン上の見た目を保つよう変換する
            if (newSpace == TextSpace::World) {
                Vector3 worldPos;
                const Vector3 image = editor.viewport_.ScreenToImage(desc.position.x, desc.position.y);
                desc.position = editor.MouseToGround(image.x, image.y, worldPos) ? worldPos : Vector3 { };
            } else {
                float camX = 0.0f, camY = 0.0f;
                if (editor.camera_) {
                    const Vector3& camPos = editor.camera_->GetTranslate();
                    camX = camPos.x;
                    camY = camPos.y;
                }
                const Vector3 world = editor.WorldPositionOf(desc);
                float screenX, screenY;
                SceneShared::WorldToScreen(world.x, world.y, camX, camY, screenX, screenY);
                desc.position = { screenX, screenY, 0.0f };
            }
            desc.textSpace = newSpace;
        }
    }
    EditorUI::HelpMarker("画面座標: 位置のx/yをスクリーンピクセル座標として使います（カメラに影響されず常に同じ位置に表示）\nワールド座標: 位置をワールド座標として扱い、カメラに応じて画面へ投影します");

    if (desc.textSpace == TextSpace::Screen) {
        RenderScreenAnchorOcclusionWarning(editor, desc);
    }
    ImGui::PopID();
}
} // namespace engine::game
#endif
