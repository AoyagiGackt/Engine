/**
 * @file StageEditorEventPanels.cpp
 * @brief イベント・グラフ・Wave設定パネルの描画
 */
#include "StageEditor.h"
#ifdef USE_IMGUI
#include "Camera.h"
#include "DiagnosticsDraw.h"
#include "EditorUI.h"
#include "EnemyEntity.h"
#include "GameFlags.h"
#include "GraphEditor.h"
#include "KnightEnemy.h"
#include "StageEditorPanels.h"
#include "SceneManager.h"
#include "StageEditorPrefabService.h"
#include "StageEditorUiStyle.h"
#include "WinApp.h"
#include <algorithm>
#include <cfloat>
#include <iterator>
#include <imgui.h>
#endif
using namespace engine::game;
using namespace engine;
using namespace engine::graphics;

#ifdef USE_IMGUI

namespace {
namespace EditorUi = engine::game::StageEditorUiStyle;

constexpr ImVec2 kEventWindowPos = { 600.0f, 0.0f };
constexpr ImVec2 kEventWindowSize = { 360.0f, 310.0f };
constexpr ImVec2 kWaveWindowPos = { 290.0f, 240.0f };
constexpr ImVec2 kWaveWindowSize = { 300.0f, 245.0f };
constexpr float kMaxEventDelaySeconds = 30.0f;
constexpr int kMaxWaveEnemyCount = 32;
constexpr float kMinWaveSpacing = 0.5f;
constexpr float kMaxWaveSpacing = 20.0f;
// アセットパレットに並べる配置物プリセット（モデル+テクスチャが対応済みの組み合わせのみ収録）
struct AssetPreset {
    const char* label;
    const char* model;
    const char* texture;
};

constexpr AssetPreset kAssetPresets[] = {
    { "ブロック", "Resources/block/block.obj", "Resources/block/block.png" },
    { "剣", "Resources/Knight/OBJ/Sword.obj", "Resources/Knight/OBJ/SwordPalette.png" },
    { "刀", "Resources/Knight/OBJ/Katana.obj", "Resources/Knight/OBJ/KatanaPalette.png" },
    { "ナイト像", "Resources/Knight/OBJ/KnightCharacter.obj", "Resources/Knight/OBJ/KnightCharacterPalette.png" },
    { "短剣", "Resources/MedievalWeaponsPack/OBJ/Dagger.obj", "Resources/MedievalWeaponsPack/OBJ/DaggerPalette.png" },
    { "大槌", "Resources/MedievalWeaponsPack/OBJ/Hammer_Small.obj", "Resources/MedievalWeaponsPack/OBJ/Hammer_SmallPalette.png" },
    { "槍", "Resources/MedievalWeaponsPack/OBJ/Spear.obj", "Resources/MedievalWeaponsPack/OBJ/SpearPalette.png" },
    { "大剣", "Resources/MedievalWeaponsPack/OBJ/Claymore.obj", "Resources/MedievalWeaponsPack/OBJ/ClaymorePalette.png" },
    { "大鎌", "Resources/MedievalWeaponsPack/OBJ/Scythe.obj", "Resources/MedievalWeaponsPack/OBJ/ScythePalette.png" },
    { "両手斧", "Resources/MedievalWeaponsPack/OBJ/Axe_Double.obj", "Resources/MedievalWeaponsPack/OBJ/Axe_DoublePalette.png" },
};
} // namespace

void StageEditor::RenderNoCodeEventPanel()
{
    ImGui::SetNextWindowPos(kEventWindowPos, ImGuiCond_Once);
    ImGui::SetNextWindowSize(kEventWindowSize, ImGuiCond_Once);
    ImGui::Begin("イベント");
    ImGui::TextWrapped("条件が成立したとき、敵・扉・カメラなどへ動作を伝える設定です。上から条件、対象、動作の順に選びます。");
    ImGui::TextDisabled("例  部屋へ入る  0秒後  敵を出現させる");
    if (ImGui::Button("戦闘部屋テンプレートを生成", ImVec2(-1.0f, 0.0f))) {
        RecordUndoSnapshotNow();
        AppendGeneratedContent(StageEditorContentFactory::CreateBattleRoom(ViewCenterOnGround(), nextSerial_));
        statusMessage_ = "戦闘部屋テンプレートを生成しました";
        statusTimer_ = StageEditor::kStatusNormalSeconds;
    }
    if (ImGui::CollapsingHeader("部品テンプレート（画面中央に生成）")) {
        constexpr float kTemplateStatusSeconds = 3.0f;
        constexpr int kPickupRowCount = 3;
        constexpr float kPickupRowSpacing = 1.5f;
        constexpr const char* kWallWeapons[] = { "何でも", "Sword", "Spear", "Hammer", "Dagger", "Ball", "Greatsword", "Scythe", "Axe" };
        constexpr int kWallWeaponCount = static_cast<int>(sizeof(kWallWeapons) / sizeof(kWallWeapons[0]));

        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##doorCondition", "開く条件のフラグ名（condition_xxx）", templateDoorCondition_, sizeof(templateDoorCondition_));
        if (ImGui::Button("スライド扉", ImVec2(-1.0f, 0.0f))) {
            RecordUndoSnapshotNow();
            AppendGeneratedContent(StageEditorContentFactory::CreateSlidingDoor(ViewCenterOnGround(), templateDoorCondition_, nextSerial_));
            statusMessage_ = "スライド扉を生成しました（条件フラグが立つとせり上がって消えます）";
            statusTimer_ = kTemplateStatusSeconds;
        }

        ImGui::SetNextItemWidth(-1.0f);
        ImGui::Combo("##wallWeapon", &templateWallWeapon_, kWallWeapons, kWallWeaponCount);
        if (ImGui::Button("壊せる壁", ImVec2(-1.0f, 0.0f))) {
            RecordUndoSnapshotNow();
            const std::string weapon = templateWallWeapon_ == 0 ? "" : kWallWeapons[templateWallWeapon_];
            AppendGeneratedContent(StageEditorContentFactory::CreateBreakableWall(ViewCenterOnGround(), weapon, nextSerial_));
            statusMessage_ = "壊せる壁を生成しました";
            statusTimer_ = kTemplateStatusSeconds;
        }

        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##guideText", "案内文", templateGuideText_, sizeof(templateGuideText_));
        if (ImGui::Button("区画トリガー＋案内文", ImVec2(-1.0f, 0.0f))) {
            RecordUndoSnapshotNow();
            AppendGeneratedContent(StageEditorContentFactory::CreateZoneGuide(ViewCenterOnGround(), templateGuideText_, nextSerial_));
            statusMessage_ = "区画トリガーと案内文を生成しました（前の案内はグラフのSetObjectEnabledで消せます）";
            statusTimer_ = kTemplateStatusSeconds;
        }

        if (ImGui::Button("収集物を3つ並べる", ImVec2(-1.0f, 0.0f))) {
            RecordUndoSnapshotNow();
            AppendGeneratedContent(StageEditorContentFactory::CreatePickupRow(ViewCenterOnGround(), kPickupRowCount, kPickupRowSpacing, nextSerial_));
            statusMessage_ = "収集物を生成しました";
            statusTimer_ = kTemplateStatusSeconds;
        }
    }

    const char* sourcePreview = "イベントトリガーを選択";
    const int sourceIndex = eventConnection_.SourceIndex();
    if (sourceIndex >= 0 && sourceIndex < static_cast<int>(triggers_.size())) {
        sourcePreview = triggers_[sourceIndex].GetDesc().name.c_str();
    } else if (sourceIndex >= static_cast<int>(triggers_.size())) {
        const int objectIndex = sourceIndex - static_cast<int>(triggers_.size());
        if (objectIndex >= 0 && objectIndex < static_cast<int>(objects_.size())
            && objects_[objectIndex].desc.kind == ObjectKindName::kEventCondition) {
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
            if (objects_[i].desc.kind != ObjectKindName::kEventCondition) {
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
    ImGui::SameLine();
    {
        // 3Dビューでトリガー/イベント条件を選択中なら、それをそのまま発生条件に使えるようにする
        const int selectedSourceIndex = SelectionKindOf(selKind_).EventSourceIndex(*this, selIndex_);
        ImGui::BeginDisabled(selectedSourceIndex < 0);
        if (ImGui::SmallButton("選択中を使う##sourceUseSelection")) {
            eventConnection_.SourceIndex() = selectedSourceIndex;
        }
        ImGui::EndDisabled();
    }
    EditorUI::HelpMarker("いつ動かすかを選択します。進入トリガーはプレイヤーが範囲へ入った時、全滅条件は指定グループの敵が全員倒れた時に成立します。3Dビューでトリガー/条件を選択中なら「選択中を使う」で反映できます。");

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
    ImGui::SameLine();
    {
        // 3Dビューで動作対象になれる配置物を選択中なら、それをそのまま動作対象に使えるようにする
        const bool hasValidSelection = selKind_ == SelKind::Object && selIndex_ >= 0
            && selIndex_ < static_cast<int>(objects_.size())
            && StageEditorEventConnection::SupportsTarget(objects_[selIndex_].desc);
        ImGui::BeginDisabled(!hasValidSelection);
        if (ImGui::SmallButton("選択中を使う##targetUseSelection")) {
            eventConnection_.TargetIndex() = selIndex_;
        }
        ImGui::EndDisabled();
    }
    EditorUI::HelpMarker("何を動かすかを選択します。敵の出現地点、扉などのギミック、演出用カメラを対象にできます。3Dビューで対象を選択中なら「選択中を使う」で反映できます。");

    const char* actions[] = { "対象を有効化", "対象を無効化" };
    ImGui::Combo("実行する動作", &eventConnection_.ActionIndex(), actions, 2);
    EditorUI::HelpMarker("有効化は対象を出現または動作させます。無効化は対象を消す、または停止する用途に使います。");
    ImGui::DragFloat("実行までの遅延 秒", &eventConnection_.DelaySeconds(), EditorUi::kDragStepSeconds, 0.0f, kMaxEventDelaySeconds, "%.1f");
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
        statusTimer_ = StageEditor::kStatusShortSeconds;
    }
    ImGui::EndDisabled();

    ImGui::SeparatorText("現在の接続");
    int disconnectIndex = -1;
    auto renderConnectionTarget = [&](int objectIndex) {
        const ObjectDesc& target = objects_[objectIndex].desc;
        ImGui::PushID(objectIndex);
        ImGui::Bullet();
        ImGui::SameLine();
        ImGui::TextWrapped("%.1f秒 -> %s -> %s", target.activationDelay,
            target.activeWhenFlag ? "有効化" : "無効化", target.name.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("解除")) {
            disconnectIndex = objectIndex;
        }
        ImGui::PopID();
    };

    // 1つの発生条件から複数へ繋げているケースが一目で分かるよう、対象ごとではなく発生条件ごとにまとめて表示する
    for (const auto& triggerEntry : triggers_) {
        const TriggerDesc& trigger = triggerEntry.GetDesc();
        std::vector<int> targetIndices;
        for (int objectIndex = 0; objectIndex < static_cast<int>(objects_.size()); ++objectIndex) {
            const ObjectDesc& target = objects_[objectIndex].desc;
            if (StageEditorEventConnection::SupportsTarget(target) && target.activationFlag == trigger.flag) {
                targetIndices.push_back(objectIndex);
            }
        }
        if (targetIndices.empty()) {
            continue;
        }
        ImGui::TextColored(EditorUi::kHeadingColor, "%s (%d件)", trigger.name.c_str(),
            static_cast<int>(targetIndices.size()));
        for (int objectIndex : targetIndices) {
            renderConnectionTarget(objectIndex);
        }
    }
    for (const auto& conditionEntry : objects_) {
        if (conditionEntry.desc.kind != ObjectKindName::kEventCondition) {
            continue;
        }
        const std::string conditionFlag = "condition_" + conditionEntry.desc.name;
        std::vector<int> targetIndices;
        for (int objectIndex = 0; objectIndex < static_cast<int>(objects_.size()); ++objectIndex) {
            const ObjectDesc& target = objects_[objectIndex].desc;
            if (StageEditorEventConnection::SupportsTarget(target) && target.activationFlag == conditionFlag) {
                targetIndices.push_back(objectIndex);
            }
        }
        if (targetIndices.empty()) {
            continue;
        }
        ImGui::TextColored(EditorUi::kHeadingColor, "%s [%s] (%d件)",
            conditionEntry.desc.name.c_str(), conditionEntry.desc.conditionType.c_str(),
            static_cast<int>(targetIndices.size()));
        for (int objectIndex : targetIndices) {
            renderConnectionTarget(objectIndex);
        }
    }
    // トリガー/条件が削除された等で接続元が見つからないものは最後にまとめて警告表示する
    for (int objectIndex = 0; objectIndex < static_cast<int>(objects_.size()); ++objectIndex) {
        const ObjectDesc& target = objects_[objectIndex].desc;
        if (!StageEditorEventConnection::SupportsTarget(target) || target.activationFlag.empty()) {
            continue;
        }
        bool matched = false;
        for (const auto& trigger : triggers_) {
            if (trigger.GetDesc().flag == target.activationFlag) {
                matched = true;
                break;
            }
        }
        if (!matched && target.activationFlag.starts_with("condition_")) {
            const std::string conditionName = target.activationFlag.substr(10);
            for (const auto& condition : objects_) {
                if (condition.desc.kind == ObjectKindName::kEventCondition && condition.desc.name == conditionName) {
                    matched = true;
                    break;
                }
            }
        }
        if (matched) {
            continue;
        }
        ImGui::PushID(objectIndex);
        ImGui::TextColored(EditorUi::kMissingSourceColor, "接続元なし -> %s", target.name.c_str());
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

void StageEditor::RenderGraphPanel()
{
    constexpr float kPanelPosX = 600.0f;
    constexpr float kPanelPosY = 320.0f;
    constexpr float kPanelWidth = 380.0f;
    constexpr float kPanelHeight = 300.0f;
    constexpr float kRemoveButtonWidth = 24.0f;
    constexpr float kStatusSeconds = 3.0f;
    ImGui::SetNextWindowPos(ImVec2(kPanelPosX, kPanelPosY), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(kPanelWidth, kPanelHeight), ImGuiCond_Once);
    ImGui::Begin("グラフ", &showGraphPanel_);
    ImGui::TextWrapped("このレベルで動かすノードグラフ（F1で編集）を紐付けます。常駐グラフは読込直後から走り、フラグ起動グラフは指定フラグが立った瞬間に走ります。");

    ImGui::SeparatorText("常駐グラフ");
    ImGui::SetNextItemWidth(-1.0f);
    const bool graphPathChanged = ImGui::InputTextWithHint("##graphPath", "Resources/Graphs/xxx.json（空なら無し）", graphPathBuffer_, sizeof(graphPathBuffer_));
    if (ImGui::IsItemActivated()) {
        BeginUndoCapture();
    }
    if (graphPathChanged) {
        MarkUndoDirty();
        graphPath_ = graphPathBuffer_;
    }
    if (ImGui::IsItemDeactivated()) {
        CommitUndoCapture();
    }
    ImGui::BeginDisabled(graphPath_.empty());
    if (ImGui::Button("ノードエディタで開く##main", ImVec2(-1.0f, 0.0f))) {
        GraphEditor::GetInstance()->OpenAndShow(graphPath_);
    }
    ImGui::EndDisabled();

    ImGui::SeparatorText("フラグ起動グラフ");
    ImGui::TextDisabled("例  トリガーのフラグ room_start_0 → 敵出現の演出グラフ");
    for (int i = 0; i < static_cast<int>(flagGraphs_.size()); ++i) {
        ImGui::PushID(i);
        if (ImGui::Button("x", ImVec2(kRemoveButtonWidth, 0.0f))) {
            RecordUndoSnapshotNow();
            flagGraphs_.erase(flagGraphs_.begin() + i);
            ImGui::PopID();
            break;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("開く")) {
            GraphEditor::GetInstance()->OpenAndShow(flagGraphs_[i].graphPath);
        }
        ImGui::SameLine();
        ImGui::TextWrapped("%s  →  %s", flagGraphs_[i].flag.c_str(), flagGraphs_[i].graphPath.c_str());
        ImGui::PopID();
    }
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##newFlag", "フラグ名（トリガー/条件のフラグ、pickup_<名前> 等）", newFlagGraphFlag_, sizeof(newFlagGraphFlag_));
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##newFlagPath", "グラフJSONのパス", newFlagGraphPath_, sizeof(newFlagGraphPath_));
    const bool canAdd = newFlagGraphFlag_[0] != '\0' && newFlagGraphPath_[0] != '\0';
    ImGui::BeginDisabled(!canAdd);
    if (ImGui::Button("紐付けを追加", ImVec2(-1.0f, 0.0f))) {
        RecordUndoSnapshotNow();
        FlagGraphBinding binding;
        binding.flag = newFlagGraphFlag_;
        binding.graphPath = newFlagGraphPath_;
        flagGraphs_.push_back(std::move(binding));
        newFlagGraphFlag_[0] = '\0';
    }
    ImGui::EndDisabled();

    ImGui::SeparatorText("実行状態");
    ImGui::Text("実行中のグラフ: %d", levelGraphs_.GetRunningCount());
    if (ImGui::Button("グラフを再起動（保存内容で読み直す）", ImVec2(-1.0f, 0.0f))) {
        LevelData data;
        data.graphPath = graphPath_;
        data.flagGraphs = flagGraphs_;
        levelGraphs_.Start(data);
        statusMessage_ = "レベルのグラフを再起動しました";
        statusTimer_ = kStatusSeconds;
    }
    EditorUI::HelpMarker("グラフの編集はF1のノードエディタで行い、保存後にここで再起動すると反映されます");
    ImGui::End();
}

void StageEditor::RenderWavePanel()
{
    ImGui::SetNextWindowPos(kWaveWindowPos, ImGuiCond_Once);
    ImGui::SetNextWindowSize(kWaveWindowSize, ImGuiCond_Once);
    ImGui::Begin("Wave作成");
    ImGui::TextWrapped("同じグループの敵をまとめて生成します。生成後は各出現地点を個別に移動できます。");
    ImGui::TextDisabled("例  room_1、敵数 3、開始条件 room_entry");
    ImGui::InputText("グループ名", waveGroupName_, sizeof(waveGroupName_));
    EditorUI::HelpMarker("全滅判定でまとめて扱うための名前です。同じ戦闘に出す敵は同じ名前にします。");
    const char* enemyTypes[] = { "汎用エネミー", "ナイト" };
    ImGui::Combo("敵種類", &waveEnemyType_, enemyTypes, static_cast<int>(std::size(enemyTypes)));
    ImGui::InputInt("敵数", &waveEnemyCount_);
    waveEnemyCount_ = std::clamp(waveEnemyCount_, 1, kMaxWaveEnemyCount);
    ImGui::DragFloat("配置間隔", &waveSpacing_, EditorUi::kDragStepPosition, kMinWaveSpacing, kMaxWaveSpacing);
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
        center = ViewCenterOnGround();
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
        statusTimer_ = StageEditor::kStatusShortSeconds;
    }
    ImGui::TextDisabled("生成後も各SpawnPointを個別に移動できます");
    ImGui::End();
}

#endif
