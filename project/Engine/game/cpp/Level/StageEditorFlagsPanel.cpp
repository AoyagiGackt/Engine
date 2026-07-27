/**
 * @file StageEditorFlagsPanel.cpp
 * @brief ステージエディタの「フラグとチェックポイント」パネルを実装するファイル
 * @note StageEditorContentPanels.cppからさらに分割したファイルクラス自体はStageEditorのまま、
 * 定義の置き場所だけを分けている
 */
#include "StageEditor.h"
#ifdef USE_IMGUI
#include "GameFlags.h"
#include "WinApp.h"
#include <algorithm>
#include <imgui.h>
#endif
using namespace engine::game;
using namespace engine;
using namespace engine::graphics;

#ifdef USE_IMGUI
void StageEditor::RenderFlagsPanel()
{
    ImGui::SetNextWindowPos(ImVec2(static_cast<float>(WinApp::kClientWidth) - 300.0f, 350.0f), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(300.0f, 360.0f), ImGuiCond_Once);
    ImGui::Begin("フラグとチェックポイント");
    for (const auto& [name, value] : GameFlags::GetInstance()->GetAll()) {
        ImGui::TextColored(value ? ImVec4(0.5f, 1.0f, 0.6f, 1.0f) : ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
            "%s = %s", name.c_str(), value ? "true" : "false");
    }

    ImGui::SeparatorText("チェックポイント");
    if (ImGui::Button("現在の開始位置へ追加", ImVec2(-1.0f, 0.0f))) {
        RecordUndoSnapshotNow();
        CheckpointDesc checkpoint;
        checkpoint.name = "checkpoint_" + std::to_string(checkpoints_.size() + 1);
        checkpoint.position = playerSpawn_;
        checkpoints_.push_back(std::move(checkpoint));
    }

    int removeIndex = -1;
    for (int i = 0; i < static_cast<int>(checkpoints_.size()); ++i) {
        CheckpointDesc& checkpoint = checkpoints_[i];
        ImGui::PushID(i);
        if (ImGui::TreeNodeEx(checkpoint.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            char name[96] = { };
            strncpy_s(name, checkpoint.name.c_str(), _TRUNCATE);
            const bool nameChanged = ImGui::InputText("名前", name, sizeof(name));
            if (ImGui::IsItemActivated()) {
                BeginUndoCapture();
            }
            if (nameChanged) {
                checkpoint.name = name;
                MarkUndoDirty();
            }
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                CommitUndoCapture();
            }

            const bool positionChanged = ImGui::DragFloat3("復帰位置", &checkpoint.position.x, 0.1f);
            if (ImGui::IsItemActivated()) {
                BeginUndoCapture();
            }
            if (positionChanged) {
                MarkUndoDirty();
            }
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                CommitUndoCapture();
            }

            const bool radiusChanged = ImGui::DragFloat(
                "有効化半径", &checkpoint.activationRadius, 0.05f, 0.1f, 20.0f);
            if (ImGui::IsItemActivated()) {
                BeginUndoCapture();
            }
            if (radiusChanged) {
                checkpoint.activationRadius = (std::max)(checkpoint.activationRadius, 0.1f);
                MarkUndoDirty();
            }
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                CommitUndoCapture();
            }

            if (ImGui::Button("削除", ImVec2(-1.0f, 0.0f))) {
                removeIndex = i;
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (removeIndex >= 0) {
        RecordUndoSnapshotNow();
        checkpoints_.erase(checkpoints_.begin() + removeIndex);
    }
    ImGui::End();
}
#else
void StageEditor::RenderFlagsPanel() { }
#endif
