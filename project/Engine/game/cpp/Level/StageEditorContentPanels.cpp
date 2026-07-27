/**
 * @file StageEditorContentPanels.cpp
 * @brief ステージエディタの各種ImGuiパネル（ヒエラルキー・ツールバー・インスペクタ・
 * アセットパレット・フラグ・制作ワークフロー・ノーコードイベント・Wave・解析・差分・ヘルプ）を実装するファイル
 * @note StageEditor.cppからの分割ファイルクラス自体はStageEditorのまま、定義の置き場所だけを分けている
 */
#include "StageEditor.h"
#ifdef USE_IMGUI
#include "Camera.h"
#include "EditorUI.h"
#include "EnemyEntity.h"
#include "GameFlags.h"
#include "KnightEnemy.h"
#include "StageEditorPanels.h"
#include "StageEditorPrefabService.h"
#include "WinApp.h"
#include <algorithm>
#include <imgui.h>
#endif
using namespace engine::game;
using namespace engine;
using namespace engine::graphics;

#ifdef USE_IMGUI

namespace {
// アセットパレットに並べる配置物プリセット（モデル+テクスチャが対応済みの組み合わせのみ収録）
struct AssetPreset {
    const char* label;
    const char* model;
    const char* texture;
};

constexpr AssetPreset kAssetPresets[] = {
    { "ブロック", "Resources/block/block.obj", "Resources/block/block.png" },
    { "剣（Sword）", "Resources/Knight/OBJ/Sword.obj", "Resources/Knight/OBJ/SwordPalette.png" },
    { "刀（Katana）", "Resources/Knight/OBJ/Katana.obj", "Resources/Knight/OBJ/KatanaPalette.png" },
    { "ナイト像", "Resources/Knight/OBJ/KnightCharacter.obj", "Resources/Knight/OBJ/KnightCharacterPalette.png" },
    { "短剣（Dagger）", "Resources/MedievalWeaponsPack/OBJ/Dagger.obj", "Resources/MedievalWeaponsPack/OBJ/DaggerPalette.png" },
    { "大槌（Hammer）", "Resources/MedievalWeaponsPack/OBJ/Hammer_Small.obj", "Resources/MedievalWeaponsPack/OBJ/Hammer_SmallPalette.png" },
    { "槍（Spear）", "Resources/MedievalWeaponsPack/OBJ/Spear.obj", "Resources/MedievalWeaponsPack/OBJ/SpearPalette.png" },
    { "大剣（Claymore）", "Resources/MedievalWeaponsPack/OBJ/Claymore.obj", "Resources/MedievalWeaponsPack/OBJ/ClaymorePalette.png" },
    { "大鎌（Scythe）", "Resources/MedievalWeaponsPack/OBJ/Scythe.obj", "Resources/MedievalWeaponsPack/OBJ/ScythePalette.png" },
    { "両手斧（Axe）", "Resources/MedievalWeaponsPack/OBJ/Axe_Double.obj", "Resources/MedievalWeaponsPack/OBJ/Axe_DoublePalette.png" },
};
} // namespace

void StageEditor::DrawHierarchyEntry(int index, int depthLevel)
{
    if (depthLevel > 8) {
        return;
    } // 循環参照の安全弁

    const ObjectDesc& desc = objects_[index].desc;
    bool sel = std::find(selectedObjectIndices_.begin(), selectedObjectIndices_.end(), index)
        != selectedObjectIndices_.end();

    // 種類が一目で分かるようタグを付ける（配置物はタグ無し）
    const char* kindTag = (desc.kind == "enemy_knight") ? "[ナイト] "
        : (desc.kind == "enemy_basic")                  ? "[エネミー] "
                                                        : "";

    // 深さぶんインデントして親子関係を視覚化する
    char label[128];
    std::string indent(static_cast<size_t>(depthLevel) * 2, ' ');
    snprintf(label, sizeof(label), "%s%s%s%s##obj%d",
        indent.c_str(), (depthLevel > 0) ? "└ " : "", kindTag, desc.name.c_str(), index);
    if (ImGui::Selectable(label, sel)) {
        selKind_ = SelKind::Object;
        selIndex_ = index;
        if (!ImGui::GetIO().KeyCtrl) {
            selectedObjectIndices_.clear();
        }
        auto selected = std::find(selectedObjectIndices_.begin(), selectedObjectIndices_.end(), index);
        if (selected == selectedObjectIndices_.end()) {
            selectedObjectIndices_.push_back(index);
        } else if (ImGui::GetIO().KeyCtrl) {
            selectedObjectIndices_.erase(selected);
        }
    }

    // このエントリを親にしている子を直下に描く
    for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
        if (i != index && objects_[i].desc.parent == desc.name) {
            DrawHierarchyEntry(i, depthLevel + 1);
        }
    }
}

void StageEditor::RenderHierarchy()
{
    StageEditorHierarchyPanel::Render(*this);
}

void StageEditor::RenderEditorToolbar()
{
    constexpr float kLeftPanelWidth = 280.0f;
    constexpr float kRightPanelWidth = 300.0f;
    constexpr float kToolbarHeight = 42.0f;
    const float toolbarWidth = static_cast<float>(WinApp::kClientWidth) - kLeftPanelWidth - kRightPanelWidth;

    ImGui::SetNextWindowPos(ImVec2(kLeftPanelWidth, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(toolbarWidth, kToolbarHeight), ImGuiCond_Always);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar;
    ImGui::Begin("シーンビューツールバー", nullptr, flags);
    ImGui::TextUnformatted("シーンビュー");
    ImGui::SameLine();
    if (ImGui::Button(playTestMode_ ? "編集へ戻る" : "テスト")) {
        SetPlayTestMode(!playTestMode_);
    }
    ImGui::SameLine();
    ImGui::Checkbox("フラグ", &showFlagsPanel_);
    ImGui::SameLine();
    ImGui::Checkbox("制作", &showWorkflowPanel_);
    ImGui::SameLine();
    ImGui::Checkbox("イベント", &showNoCodeEventPanel_);
    ImGui::SameLine();
    ImGui::Checkbox("Wave", &showWavePanel_);
    ImGui::SameLine();
    if (ImGui::Button("最大化 F4")) {
        viewportFocusMode_ = true;
    }
    ImGui::End();
}

void StageEditor::RenderViewportFocusBar()
{
    // ゲーム画面を遮る範囲を抑えつつ、通常レイアウトへ戻る操作だけを残す
    ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.85f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize
        | ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin("画面優先モード", nullptr, flags);
    ImGui::TextDisabled("編集状態とギズモを維持してパネルを隠している");
    if (ImGui::Button("編集パネルを表示 (F4)")) {
        viewportFocusMode_ = false;
    }
    ImGui::End();
}

void StageEditor::RenderInspector()
{
    StageEditorInspectorPanel::Render(*this);
}

void StageEditor::RenderAssetPalette()
{
    constexpr float kToolbarHeight = 42.0f;
    constexpr float kPanelWidth = 280.0f;
    const float availableHeight = static_cast<float>(WinApp::kClientHeight) - kToolbarHeight;
    const float hierarchyHeight = availableHeight * 0.62f;
    ImGui::SetNextWindowPos(ImVec2(0.0f, kToolbarHeight + hierarchyHeight), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(kPanelWidth, availableHeight - hierarchyHeight), ImGuiCond_Always);
    ImGui::Begin("アセットパレット", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);

    // どちらの動作になるかを隠れた自動判定にせず、ラジオボタンで明示的に選ばせる
    bool hasPropSel = (selKind_ == SelKind::Object && selIndex_ >= 0
        && selIndex_ < static_cast<int>(objects_.size())
        && objects_[selIndex_].desc.kind == "prop");

    ImGui::RadioButton("新規配置", &paletteMode_, 0);
    ImGui::SameLine();
    ImGui::BeginDisabled(!hasPropSel);
    ImGui::RadioButton("選択へ差し替え", &paletteMode_, 1);
    ImGui::EndDisabled();
    EditorUI::HelpMarker("新規配置: クリックしたプリセットを画面中央に追加します\n選択へ差し替え: 選択中の配置物のモデルを置き換えます（配置物を選択中のみ有効）");

    bool applyToSelection = (paletteMode_ == 1 && hasPropSel);
    ImGui::TextDisabled(applyToSelection ? "クリックで選択中の配置物のモデルを差し替え" : "クリックで画面中央に新規配置");

    for (const auto& preset : kAssetPresets) {
        if (ImGui::Button(preset.label, ImVec2(-1, 0))) {
            if (applyToSelection) {
                RecordUndoSnapshotNow();
                ObjectEntry& entry = objects_[selIndex_];
                entry.desc.model = preset.model;
                entry.desc.texture = preset.texture;
                RegenerateInstances(entry);
            } else {
                AddPropAtScreenCenter(preset.model, preset.texture);
            }
        }
    }

    ImGui::End();
}

void StageEditor::RenderWorkflowPanel()
{
    ImGui::SetNextWindowPos(ImVec2(290.0f, 0.0f), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(300.0f, 230.0f), ImGuiCond_Once);
    ImGui::Begin("編集ワークフロー");

    if (ImGui::Button(playTestMode_ ? "編集モードへ戻る" : "現在の配置でテスト", ImVec2(-1.0f, 0.0f))) {
        SetPlayTestMode(!playTestMode_);
    }
    ImGui::TextDisabled(playTestMode_ ? "ゲーム更新中  F2で終了" : "ゲーム停止中  配置を安全に編集できます");

    ImGui::SeparatorText("移動ギズモ");
    ImGui::RadioButton("自由", &gizmoAxis_, 0);
    ImGui::SameLine();
    ImGui::RadioButton("X", &gizmoAxis_, 1);
    ImGui::SameLine();
    ImGui::RadioButton("Y", &gizmoAxis_, 2);
    ImGui::SameLine();
    ImGui::RadioButton("Z", &gizmoAxis_, 3);

    ImGui::SeparatorText("プレハブ");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##prefabName", "英数字のプレハブ名", prefabName_, sizeof(prefabName_));
    ImGui::BeginDisabled(selKind_ != SelKind::Object);
    if (ImGui::Button("選択物を保存", ImVec2(140.0f, 0.0f))) {
        SaveSelectedPrefab();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("配置", ImVec2(140.0f, 0.0f))) {
        InstantiatePrefab();
    }

    ImGui::Checkbox("30秒ごとに自動保存", &autoSaveEnabled_);
    if (ImGui::Button("ステージ解析")) {
        showStageAnalysis_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("保存差分")) {
        showSavedDiff_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("ヘルプ")) {
        showEditorHelp_ = true;
    }
    if (ImGui::Button("保存前検証", ImVec2(-1.0f, 0.0f))) {
        validationIssues_ = ValidateLevel();
        statusMessage_ = validationIssues_.empty()
            ? "検証完了: 問題はありません"
            : "検証完了: " + std::to_string(validationIssues_.size()) + "件の問題があります";
        statusTimer_ = 4.0f;
    }
    if (!validationIssues_.empty() && ImGui::TreeNode("検証結果")) {
        for (int issueIndex = 0; issueIndex < static_cast<int>(validationIssues_.size()); ++issueIndex) {
            const std::string& issue = validationIssues_[issueIndex];
            ImGui::PushID(issueIndex);
            if (ImGui::SmallButton("移動")) {
                for (int objectIndex = 0; objectIndex < static_cast<int>(objects_.size()); ++objectIndex) {
                    if (!objects_[objectIndex].desc.name.empty()
                        && issue.find(objects_[objectIndex].desc.name) != std::string::npos) {
                        selKind_ = SelKind::Object;
                        selIndex_ = objectIndex;
                        selectedObjectIndices_ = { objectIndex };
                        if (camera_) {
                            const Vector3 target = WorldPositionOf(objects_[objectIndex].desc);
                            camera_->SetTranslate({ target.x, target.y, camera_->GetTranslate().z });
                        }
                        break;
                    }
                }
            }
            ImGui::SameLine();
            ImGui::TextWrapped("%s", issue.c_str());
            ImGui::PopID();
        }
        ImGui::TreePop();
    }

    if (recoveryAvailable_) {
        ImGui::OpenPopup("自動保存の復旧");
        recoveryAvailable_ = false;
    }
    if (ImGui::BeginPopupModal("自動保存の復旧", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("通常保存より新しい自動保存データがあります。");
        if (ImGui::Button("復旧する", ImVec2(120.0f, 0.0f))) {
            LevelData recovered = LevelLoader::Load(recoveryPath_);
            LevelSnapshot snapshot;
            snapshot.objects = std::move(recovered.objects);
            snapshot.triggers = std::move(recovered.triggers);
            snapshot.checkpoints = std::move(recovered.checkpoints);
            snapshot.playerSpawn = recovered.playerSpawn;
            snapshot.enemySpawn = recovered.enemySpawn;
            ApplySnapshot(snapshot);
            dirty_ = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("使用しない", ImVec2(120.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::End();
}

void StageEditor::SaveSelectedPrefab()
{
    if (selKind_ != SelKind::Object || selIndex_ < 0 || selIndex_ >= static_cast<int>(objects_.size())) {
        return;
    }
    const std::string path = StageEditorPrefabService::Save(prefabName_, objects_[selIndex_].desc);
    statusMessage_ = "プレハブを保存しました: " + path;
    statusTimer_ = 2.0f;
}

void StageEditor::InstantiatePrefab()
{
    const std::string path = StageEditorPrefabService::MakePath(prefabName_);
    std::vector<ObjectDesc> prefabObjects = StageEditorPrefabService::Load(prefabName_);
    if (prefabObjects.empty()) {
        statusMessage_ = "プレハブが見つかりません: " + path;
        statusTimer_ = 2.0f;
        return;
    }

    RecordUndoSnapshotNow();
    Vector3 center = playerSpawn_;
    MouseToGround(WinApp::kClientWidth * 0.5f, WinApp::kClientHeight * 0.5f, center);
    for (ObjectDesc desc : prefabObjects) {
        ObjectEntry entry;
        entry.desc = std::move(desc);
        entry.desc.name = "prefab_" + std::to_string(nextSerial_++);
        entry.desc.parent.clear();
        entry.desc.position = center + entry.desc.position;
        objects_.push_back(std::move(entry));
        RegenerateInstances(objects_.back());
    }
    selKind_ = SelKind::Object;
    selIndex_ = static_cast<int>(objects_.size()) - 1;
    selectedObjectIndices_ = { selIndex_ };
    statusMessage_ = "プレハブを配置しました";
    statusTimer_ = 2.0f;
}

void StageEditor::RenderNoCodeEventPanel()
{
    ImGui::SetNextWindowPos(ImVec2(600.0f, 0.0f), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(360.0f, 310.0f), ImGuiCond_Once);
    ImGui::Begin("イベント");
    ImGui::TextWrapped("条件が成立したとき、敵・扉・カメラなどへ動作を伝える設定です。上から条件、対象、動作の順に選びます。");
    ImGui::TextDisabled("例  部屋へ入る  0秒後  敵を出現させる");
    if (ImGui::Button("戦闘部屋テンプレートを生成", ImVec2(-1.0f, 0.0f))) {
        RecordUndoSnapshotNow();
        Vector3 center = playerSpawn_;
        MouseToGround(WinApp::kClientWidth * 0.5f, WinApp::kClientHeight * 0.5f, center);
        AppendGeneratedContent(StageEditorContentFactory::CreateBattleRoom(center, nextSerial_));
        statusMessage_ = "戦闘部屋テンプレートを生成しました";
        statusTimer_ = 3.0f;
    }

    const char* sourcePreview = "イベントトリガーを選択";
    const int sourceIndex = eventConnection_.SourceIndex();
    if (sourceIndex >= 0 && sourceIndex < static_cast<int>(triggers_.size())) {
        sourcePreview = triggers_[sourceIndex].GetDesc().name.c_str();
    } else if (sourceIndex >= static_cast<int>(triggers_.size())) {
        const int objectIndex = sourceIndex - static_cast<int>(triggers_.size());
        if (objectIndex >= 0 && objectIndex < static_cast<int>(objects_.size())
            && objects_[objectIndex].desc.kind == "event_condition") {
            sourcePreview = objects_[objectIndex].desc.name.c_str();
        }
    }
    if (ImGui::BeginCombo("発生条件", sourcePreview)) {
        for (int i = 0; i < static_cast<int>(triggers_.size()); ++i) {
            const TriggerDesc& trigger = triggers_[i].GetDesc();
            if (ImGui::Selectable(trigger.name.c_str(), eventConnection_.SourceIndex() == i)) {
                eventConnection_.SourceIndex() = i;
            }
        }
        for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
            if (objects_[i].desc.kind != "event_condition") {
                continue;
            }
            std::string label = objects_[i].desc.name + "  [" + objects_[i].desc.conditionType + "]";
            const int encodedIndex = static_cast<int>(triggers_.size()) + i;
            if (ImGui::Selectable(label.c_str(), eventConnection_.SourceIndex() == encodedIndex)) {
                eventConnection_.SourceIndex() = encodedIndex;
            }
        }
        ImGui::EndCombo();
    }
    EditorUI::HelpMarker("いつ動かすかを選択します。進入トリガーはプレイヤーが範囲へ入った時、全滅条件は指定グループの敵が全員倒れた時に成立します。");

    const char* targetPreview = "動作対象を選択";
    if (eventConnection_.TargetIndex() >= 0 && eventConnection_.TargetIndex() < static_cast<int>(objects_.size())) {
        targetPreview = objects_[eventConnection_.TargetIndex()].desc.name.c_str();
    }
    if (ImGui::BeginCombo("動作対象", targetPreview)) {
        for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
            if (!StageEditorEventConnection::SupportsTarget(objects_[i].desc)) {
                continue;
            }
            const ObjectDesc& object = objects_[i].desc;
            std::string label = object.name + "  [" + object.kind + "]";
            if (ImGui::Selectable(label.c_str(), eventConnection_.TargetIndex() == i)) {
                eventConnection_.TargetIndex() = i;
            }
        }
        ImGui::EndCombo();
    }
    EditorUI::HelpMarker("何を動かすかを選択します。敵の出現地点、扉などのギミック、演出用カメラを対象にできます。");

    const char* actions[] = { "対象を有効化", "対象を無効化" };
    ImGui::Combo("実行する動作", &eventConnection_.ActionIndex(), actions, 2);
    EditorUI::HelpMarker("有効化は対象を出現または動作させます。無効化は対象を消す、または停止する用途に使います。");
    ImGui::DragFloat("実行までの遅延 秒", &eventConnection_.DelaySeconds(), 0.1f, 0.0f, 30.0f, "%.1f");
    EditorUI::HelpMarker("条件成立から動作開始まで待つ秒数です。0なら即座に実行します。");

    ObjectDesc* selectedTarget = eventConnection_.TargetIndex() >= 0
            && eventConnection_.TargetIndex() < static_cast<int>(objects_.size())
        ? &objects_[eventConnection_.TargetIndex()].desc
        : nullptr;
    const bool canConnect = eventConnection_.CanConnect(static_cast<int>(objects_.size()), selectedTarget);
    ImGui::BeginDisabled(!canConnect);
    if (ImGui::Button("接続する", ImVec2(-1.0f, 0.0f))) {
        RecordUndoSnapshotNow();
        std::string sourceName;
        if (eventConnection_.SourceIndex() < static_cast<int>(triggers_.size())) {
            TriggerDesc& trigger = triggers_[eventConnection_.SourceIndex()].GetDesc();
            sourceName = eventConnection_.Connect(trigger, *selectedTarget);
        } else {
            const int conditionIndex = eventConnection_.SourceIndex() - static_cast<int>(triggers_.size());
            if (conditionIndex >= 0 && conditionIndex < static_cast<int>(objects_.size())) {
                sourceName = eventConnection_.Connect(objects_[conditionIndex].desc, *selectedTarget);
            }
        }
        statusMessage_ = sourceName + " から " + selectedTarget->name + " へ接続しました";
        statusTimer_ = 2.0f;
    }
    ImGui::EndDisabled();

    ImGui::SeparatorText("現在の接続");
    int disconnectIndex = -1;
    for (int objectIndex = 0; objectIndex < static_cast<int>(objects_.size()); ++objectIndex) {
        const ObjectDesc& target = objects_[objectIndex].desc;
        if (!StageEditorEventConnection::SupportsTarget(target) || target.activationFlag.empty()) {
            continue;
        }
        const TriggerDesc* source = nullptr;
        for (const auto& trigger : triggers_) {
            if (trigger.GetDesc().flag == target.activationFlag) {
                source = &trigger.GetDesc();
                break;
            }
        }
        std::string conditionSourceName;
        if (!source && target.activationFlag.starts_with("condition_")) {
            const std::string conditionName = target.activationFlag.substr(10);
            for (const auto& condition : objects_) {
                if (condition.desc.kind == "event_condition" && condition.desc.name == conditionName) {
                    conditionSourceName = conditionName;
                    break;
                }
            }
        }
        ImGui::PushID(objectIndex);
        if (source) {
            ImGui::TextWrapped("%s -> %.1f秒 -> %s -> %s", source->name.c_str(), target.activationDelay,
                target.activeWhenFlag ? "有効化" : "無効化", target.name.c_str());
        } else if (!conditionSourceName.empty()) {
            ImGui::TextWrapped("%s -> %.1f秒 -> %s -> %s", conditionSourceName.c_str(), target.activationDelay,
                target.activeWhenFlag ? "有効化" : "無効化", target.name.c_str());
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.3f, 1.0f),
                "接続元なし -> %s", target.name.c_str());
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("解除")) {
            disconnectIndex = objectIndex;
        }
        ImGui::PopID();
    }
    if (disconnectIndex >= 0) {
        RecordUndoSnapshotNow();
        eventConnection_.Disconnect(objects_[disconnectIndex].desc);
    }
    ImGui::End();
}

void StageEditor::RenderWavePanel()
{
    ImGui::SetNextWindowPos(ImVec2(290.0f, 240.0f), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(300.0f, 245.0f), ImGuiCond_Once);
    ImGui::Begin("Wave作成");
    ImGui::TextWrapped("同じグループの敵をまとめて生成します。生成後は各出現地点を個別に移動できます。");
    ImGui::TextDisabled("例  room_1、敵数 3、開始条件 room_entry");
    ImGui::InputText("グループ名", waveGroupName_, sizeof(waveGroupName_));
    EditorUI::HelpMarker("全滅判定でまとめて扱うための名前です。同じ戦闘に出す敵は同じ名前にします。");
    const char* enemyTypes[] = { "汎用エネミー", "ナイト" };
    ImGui::Combo("敵種類", &waveEnemyType_, enemyTypes, 2);
    ImGui::InputInt("敵数", &waveEnemyCount_);
    waveEnemyCount_ = std::clamp(waveEnemyCount_, 1, 32);
    ImGui::DragFloat("配置間隔", &waveSpacing_, 0.1f, 0.5f, 20.0f);
    EditorUI::HelpMarker("生成する敵同士の横方向の間隔です。単位はワールド座標です。");

    const char* triggerPreview = "開始直後";
    if (waveStartTrigger_ >= 0 && waveStartTrigger_ < static_cast<int>(triggers_.size())) {
        triggerPreview = triggers_[waveStartTrigger_].GetDesc().name.c_str();
    }
    if (ImGui::BeginCombo("開始条件", triggerPreview)) {
        if (ImGui::Selectable("開始直後", waveStartTrigger_ < 0)) {
            waveStartTrigger_ = -1;
        }
        for (int i = 0; i < static_cast<int>(triggers_.size()); ++i) {
            if (ImGui::Selectable(triggers_[i].GetDesc().name.c_str(), waveStartTrigger_ == i)) {
                waveStartTrigger_ = i;
            }
        }
        ImGui::EndCombo();
    }
    EditorUI::HelpMarker("開始直後ならステージ開始時に出現します。トリガーを選ぶとプレイヤーが範囲へ入った時に出現します。");

    if (ImGui::Button("Waveを生成", ImVec2(-1.0f, 0.0f))) {
        RecordUndoSnapshotNow();
        Vector3 center = playerSpawn_;
        MouseToGround(WinApp::kClientWidth * 0.5f, WinApp::kClientHeight * 0.5f, center);
        std::string startFlag;
        if (waveStartTrigger_ >= 0 && waveStartTrigger_ < static_cast<int>(triggers_.size())) {
            startFlag = triggers_[waveStartTrigger_].GetDesc().flag;
        }
        StageEditorWaveConfig config;
        config.groupName = waveGroupName_;
        config.spawnType = waveEnemyType_ == 1 ? "knight" : "basic";
        config.activationFlag = startFlag;
        config.enemyCount = waveEnemyCount_;
        config.spacing = waveSpacing_;
        config.center = center;
        AppendGeneratedContent(StageEditorContentFactory::CreateWave(config, nextSerial_));
        statusMessage_ = std::string(waveGroupName_) + "を生成しました";
        statusTimer_ = 2.0f;
    }
    ImGui::TextDisabled("生成後も各SpawnPointを個別に移動できます");
    ImGui::End();
}

void StageEditor::RenderStageAnalysisPanel()
{
    if (!showStageAnalysis_) {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(520.0f, 420.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("ステージ解析", &showStageAnalysis_);
    std::vector<std::string> findings = ValidateLevel();

    std::map<std::string, int> groupEnemyCounts;
    std::map<std::string, int> groupConditionCounts;
    for (const auto& entry : objects_) {
        if (!entry.desc.enemyGroup.empty()
            && (entry.desc.kind == "spawn_point" || entry.desc.kind == "enemy_basic" || entry.desc.kind == "enemy_knight")) {
            ++groupEnemyCounts[entry.desc.enemyGroup];
        }
        if (entry.desc.kind == "event_condition" && entry.desc.conditionType == "enemy_group_defeated") {
            ++groupConditionCounts[entry.desc.enemyGroup];
        }
    }
    for (const auto& [group, count] : groupConditionCounts) {
        if (group.empty() || !groupEnemyCounts.contains(group)) {
            findings.push_back("敵が存在しない全滅条件です: " + group);
        }
    }
    for (const auto& [group, count] : groupEnemyCounts) {
        if (!groupConditionCounts.contains(group)) {
            findings.push_back("全滅後の処理がない敵グループです: " + group);
        }
    }
    for (size_t i = 0; i < objects_.size(); ++i) {
        for (size_t j = i + 1; j < objects_.size(); ++j) {
            const Vector3 a = WorldPositionOf(objects_[i].desc);
            const Vector3 b = WorldPositionOf(objects_[j].desc);
            if (std::abs(a.x - b.x) < 0.01f && std::abs(a.y - b.y) < 0.01f
                && std::abs(a.z - b.z) < 0.01f) {
                findings.push_back("同じ位置に配置されています: " + objects_[i].desc.name + " / " + objects_[j].desc.name);
            }
        }
    }

    if (findings.empty()) {
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.5f, 1.0f), "問題は見つかりませんでした");
    } else {
        ImGui::Text("%d件の確認項目", static_cast<int>(findings.size()));
        for (const std::string& finding : findings) {
            ImGui::BulletText("%s", finding.c_str());
        }
    }
    ImGui::Separator();
    ImGui::BeginDisabled(selKind_ != SelKind::Object || selIndex_ < 0);
    if (ImGui::Button("選択位置からテスト開始", ImVec2(-1.0f, 0.0f))) {
        for (auto& entity : externalEntities_) {
            if (entity.name == "Player" && entity.position) {
                *entity.position = WorldPositionOf(objects_[selIndex_].desc);
                SetPlayTestMode(true);
                break;
            }
        }
    }
    ImGui::EndDisabled();
    ImGui::End();
}

void StageEditor::RenderDiffPanel()
{
    if (!showSavedDiff_) {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(480.0f, 400.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("保存内容との差分", &showSavedDiff_);
    std::map<std::string, const ObjectDesc*> saved;
    for (const auto& desc : lastSavedSnapshot_.objects) {
        saved[desc.name] = &desc;
    }
    int differenceCount = 0;
    for (const auto& entry : objects_) {
        auto found = saved.find(entry.desc.name);
        if (found == saved.end()) {
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.5f, 1.0f), "+ 追加  %s", entry.desc.name.c_str());
            ++differenceCount;
            continue;
        }
        const ObjectDesc& old = *found->second;
        const ObjectDesc& now = entry.desc;
        if (old.position.x != now.position.x || old.position.y != now.position.y || old.position.z != now.position.z
            || old.rotation.x != now.rotation.x || old.rotation.y != now.rotation.y || old.rotation.z != now.rotation.z
            || old.scale.x != now.scale.x || old.scale.y != now.scale.y || old.scale.z != now.scale.z
            || old.enabled != now.enabled || old.kind != now.kind || old.activationFlag != now.activationFlag) {
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "~ 変更  %s", now.name.c_str());
            ++differenceCount;
        }
        saved.erase(found);
    }
    for (const auto& [name, desc] : saved) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "- 削除  %s", name.c_str());
        ++differenceCount;
    }
    if (differenceCount == 0) {
        ImGui::TextDisabled("最後の保存から変更はありません");
    }
    ImGui::End();
}

void StageEditor::RenderEditorHelpPanel()
{
    if (!showEditorHelp_) {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(520.0f, 500.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("ステージ制作ヘルプ", &showEditorHelp_);
    ImGui::SeparatorText("最初のステージを作る手順");
    ImGui::TextWrapped("この手順では、部屋に入ると敵が出現し、全滅すると出口が開く場面を作成します。");
    ImGui::BulletText("1  アセットパレットから床と壁を配置する");
    ImGui::TextWrapped("   配置物を選択し、詳細設定の当たり判定を有効にします。床や壁はプレイヤーが通り抜けないsolid配置物にします。");
    ImGui::BulletText("2  イベントパネルの戦闘部屋テンプレートを生成する");
    ImGui::TextWrapped("   進入トリガー、敵の出現地点、全滅条件、出口、カメラ演出が一括で作られます。初回はここから始めるのが簡単です。");
    ImGui::BulletText("3  中央ビューで各部品を選択して位置を調整する");
    ImGui::TextWrapped("   入口に水色のトリガー、戦闘場所に敵の出現地点、奥に出口を移動します。右の詳細設定で数値も直接変更できます。");
    ImGui::BulletText("4  上部のテストを押し、実際に入口から通して遊ぶ");
    ImGui::TextWrapped("   敵が出ない場合はイベントの現在の接続を確認します。出口が開かない場合は敵グループ名と全滅条件のグループ名を揃えます。");
    ImGui::BulletText("5  制作パネルで保存前検証を行い、問題がなければ保存する");

    if (ImGui::CollapsingHeader("配置物の種類")) {
        ImGui::BulletText("地形  見た目と当たり判定を持つ床や壁を作る");
        ImGui::BulletText("敵  ステージ開始時から存在する敵を置く");
        ImGui::BulletText("出現地点  条件成立後に敵を出現させる");
        ImGui::BulletText("ギミック  扉、足場、点滅、移動など条件で動く物を作る");
        ImGui::BulletText("イベント条件  敵グループ全滅などを次の動作へつなぐ");
        ImGui::BulletText("カメラ地点  条件成立時に指定位置と角度へカメラを移動する");
        ImGui::BulletText("トリガー  プレイヤーが範囲へ入ったことを条件にする");
    }
    if (ImGui::CollapsingHeader("イベントの考え方")) {
        ImGui::TextWrapped("イベントは、いつ、何を、どうするの3項目で作ります。");
        ImGui::BulletText("いつ  進入トリガー、敵グループ全滅などを選ぶ");
        ImGui::BulletText("何を  敵の出現地点、扉、カメラ地点を選ぶ");
        ImGui::BulletText("どうする  有効化または無効化と、実行までの秒数を選ぶ");
        ImGui::TextWrapped("一つの条件から複数の対象へ接続できます。敵を出し、扉を閉め、カメラを動かす処理を同じ進入トリガーから作れます。");
    }
    if (ImGui::CollapsingHeader("困ったとき")) {
        ImGui::BulletText("敵が出ない  開始条件と出現地点の接続、対象の有効設定を確認する");
        ImGui::BulletText("出口が開かない  敵と全滅条件のグループ名を確認する");
        ImGui::BulletText("選択できない  中央シーンビュー内でクリックし、パネル上では操作しない");
        ImGui::BulletText("カメラが戻らない  カメラ地点の保持時間と次のカメラ演出を確認する");
        ImGui::BulletText("変更が消えた  未保存表示を確認し、Ctrl+Sで保存する");
        ImGui::BulletText("原因が分からない  制作パネルの保存前検証とステージ解析を実行する");
    }
    ImGui::SeparatorText("制作チェックリスト");
    ImGui::Checkbox("開始地点から出口まで移動できる", &helpChecklist_[0]);
    ImGui::Checkbox("すべての敵グループに全滅後の処理がある", &helpChecklist_[1]);
    ImGui::Checkbox("カメラ演出後に操作画面へ戻る", &helpChecklist_[2]);
    ImGui::Checkbox("ギミックでプレイヤーを閉じ込めない", &helpChecklist_[3]);
    ImGui::Checkbox("保存前検証に問題がない", &helpChecklist_[4]);
    ImGui::End();
}
#else
void StageEditor::RenderHierarchy() { }
void StageEditor::RenderEditorToolbar() { }
void StageEditor::RenderViewportFocusBar() { }
void StageEditor::RenderInspector() { }
void StageEditor::RenderAssetPalette() { }
void StageEditor::RenderWorkflowPanel() { }
void StageEditor::RenderNoCodeEventPanel() { }
void StageEditor::RenderWavePanel() { }
void StageEditor::RenderStageAnalysisPanel() { }
void StageEditor::RenderDiffPanel() { }
void StageEditor::RenderEditorHelpPanel() { }
void StageEditor::DrawHierarchyEntry(int, int) { }
#endif
