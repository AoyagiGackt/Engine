/**
 * @file StageEditorFlagsPanel.cpp
 * @brief ステージエディタのフラグとチェックポイントのパネルを実装するファイル
 * @note StageEditorContentPanels.cppからさらに分割したファイルクラス自体はStageEditorのまま、
 * 定義の置き場所だけを分けている
 */
#include "StageEditor.h"
#include "TextBufferCapacity.h"
#ifdef USE_IMGUI
#include "GameFlags.h"
#include "StageEditorUiStyle.h"
#include "WinApp.h"
#include <algorithm>
#include <imgui.h>
#endif
using namespace engine::graphics;

namespace engine::game {
#ifdef USE_IMGUI
namespace {
namespace EditorUi = engine::game::StageEditorUiStyle;
constexpr ImVec2 kFlagsWindowSize = { 300.0f, 360.0f };
constexpr float kFlagsWindowTop = 350.0f;
constexpr float kMaxCheckpointRadius = 20.0f;
}

/** @brief GameFlagsの現在値一覧とチェックポイントの追加/一覧を表示する */
void StageEditor::RenderFlagsPanel()
{
    // 画面右端に置き、今立っているフラグを色分けで一覧表示する
    const auto* display = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(display->Pos.x + display->Size.x - kFlagsWindowSize.x,
        display->Pos.y + kFlagsWindowTop), ImGuiCond_Once);
    ImGui::SetNextWindowSize(kFlagsWindowSize, ImGuiCond_Once);
    ImGui::Begin("フラグとチェックポイント");
    // 立っているフラグと下りているフラグを色で分けて1行ずつ出す
    GameFlags::GetInstance()->VisitFlags([](const std::string& name, bool value)
    {
        ImGui::TextColored(value ? EditorUi::kFlagOnColor : EditorUi::kFlagOffColor,
            "%s = %s", name.c_str(), value ? "true" : "false");
    });

    // 今のプレイヤー開始位置にチェックポイントを追加する
    ImGui::SeparatorText("チェックポイント");
    if (ImGui::Button("現在の開始位置へ追加", ImVec2(-1.0f, 0.0f)))
    {
        RecordUndoSnapshotNow();
        // 名前は 1 から始まる通し番号にする
        CheckpointDesc checkpoint;
        checkpoint.name = "checkpoint_" + std::to_string(checkpoints_.size() + 1);
        checkpoint.position = playerSpawn_;
        checkpoints_.push_back(std::move(checkpoint));
    }

    // チェックポイントごとに名前・復帰位置・半径を編集する（削除は一覧を描き終えてから行う）
    int removeIndex = -1;
    for (int i = 0; i < static_cast<int>(checkpoints_.size()); ++i)
    {
        CheckpointDesc& checkpoint = checkpoints_[i];
        // 同じ名前のチェックポイントがあっても入力欄が混ざらないよう番号で区別する
        ImGui::PushID(i);
        if (ImGui::TreeNodeEx(checkpoint.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
        {
            // 名前は入力用の固定長バッファに写してから編集する
            char name[TextBufferCapacity::kName] = { };
            strncpy_s(name, checkpoint.name.c_str(), _TRUNCATE);
            const bool nameChanged = ImGui::InputText("名前", name, sizeof(name));
            if (CaptureItemUndo(nameChanged))
            {
                checkpoint.name = name;
            }

            // 位置は値を直接書き換え、編集の開始と終わりで元に戻す履歴を取る
            const bool positionChanged = ImGui::DragFloat3("復帰位置", &checkpoint.position.x, EditorUi::kDragStepPosition);
            CaptureItemUndo(positionChanged);

            // 半径は最小値より小さくならないように丸める
            const bool radiusChanged = ImGui::DragFloat(
                "有効化半径", &checkpoint.activationRadius, EditorUi::kDragStepFine, EditorUi::kMinRadius, kMaxCheckpointRadius);
            if (CaptureItemUndo(radiusChanged))
            {
                checkpoint.activationRadius = (std::max)(checkpoint.activationRadius, EditorUi::kMinRadius);
            }

            // ここでは番号を覚えるだけにして、消すのはループの後
            if (ImGui::Button("削除", ImVec2(-1.0f, 0.0f)))
            {
                removeIndex = i;
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    // 削除ボタンが押されたチェックポイントを、戻せるようにしてから消す
    if (removeIndex >= 0)
    {
        RecordUndoSnapshotNow();
        checkpoints_.erase(checkpoints_.begin() + removeIndex);
    }
    ImGui::End();
}
#else
/** @brief GameFlagsの現在値一覧とチェックポイントの追加/一覧を表示する */
void StageEditor::RenderFlagsPanel() { }

#endif

} // namespace engine::game
