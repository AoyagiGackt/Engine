/**
 * @file StageEditorWorkflowPanels.cpp
 * @brief 制作手順・配置メニュー・解析・差分・ヘルプの描画
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
#include <imgui.h>
#endif
using namespace engine::game;
using namespace engine;
using namespace engine::graphics;

#ifdef USE_IMGUI

namespace {
namespace EditorUi = engine::game::StageEditorUiStyle;

// 各ウィンドウの初期配置
constexpr ImVec2 kWorkflowWindowPos = { 290.0f, 0.0f };
constexpr ImVec2 kWorkflowWindowSize = { 300.0f, 230.0f };
constexpr ImVec2 kAnalysisWindowSize = { 520.0f, 420.0f };
constexpr ImVec2 kDiffWindowSize = { 480.0f, 400.0f };
constexpr ImVec2 kHelpWindowSize = { 520.0f, 500.0f };
constexpr ImVec2 kWideButtonSize = { EditorUi::kWideButtonWidth, 0.0f };
constexpr ImVec2 kButtonSize = { EditorUi::kButtonWidth, 0.0f };

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

void StageEditor::RenderWorkflowPanel()
{
    ImGui::SetNextWindowPos(kWorkflowWindowPos, ImGuiCond_Once);
    ImGui::SetNextWindowSize(kWorkflowWindowSize, ImGuiCond_Once);
    ImGui::Begin("編集ワークフロー");

    if (ImGui::Button(playTestMode_ ? "編集モードへ戻る" : "現在の配置でテスト", ImVec2(-1.0f, 0.0f))) {
        SetPlayTestMode(!playTestMode_);
    }
    ImGui::TextDisabled(playTestMode_ ? "ゲーム更新中  F2で終了" : "ゲーム停止中  配置を安全に編集できます");

    ImGui::SeparatorText("テスト");
    if (ImGui::Button("画面中央からテスト", kWideButtonSize)) {
        if (!StartPlayTestAt(ViewCenterOnGround())) {
            statusMessage_ = "このシーンにはプレイヤーが登録されていません";
            statusTimer_ = StageEditor::kStatusNormalSeconds;
        }
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(selKind_ == SelKind::None);
    if (ImGui::Button("選択位置からテスト", kWideButtonSize)) {
        Vector3 start = ViewCenterOnGround();
        if (selKind_ == SelKind::Object && selIndex_ >= 0 && selIndex_ < static_cast<int>(objects_.size())) {
            start = WorldPositionOf(objects_[selIndex_].desc);
        } else if (selKind_ == SelKind::Trigger && selIndex_ >= 0 && selIndex_ < static_cast<int>(triggers_.size())) {
            start = triggers_[selIndex_].GetDesc().position;
        }
        if (!StartPlayTestAt(start)) {
            statusMessage_ = "このシーンにはプレイヤーが登録されていません";
            statusTimer_ = StageEditor::kStatusNormalSeconds;
        }
    }
    ImGui::EndDisabled();
    EditorUI::HelpMarker("プレイヤーをその場所へ移してテストを始めます。後半の区画を何度も確かめる時に使います。F2で編集へ戻ります");
    if (ImGui::Button("選択へカメラ (F)", kWideButtonSize)) {
        FocusCameraOnSelection();
    }
    ImGui::SameLine();
    ImGui::Checkbox("グリッド線", &showGrid_);
    EditorUI::HelpMarker("スナップON中、スナップ間隔のグリッド線を表示します。矢印キーで選択物をスナップ間隔ぶん動かせます（Shift+上下で奥行き）");

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
    if (ImGui::Button("選択物を保存", kWideButtonSize)) {
        SaveSelectedPrefab();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("配置", kWideButtonSize)) {
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
        statusTimer_ = StageEditor::kStatusLongSeconds;
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
        if (ImGui::Button("復旧する", kButtonSize)) {
            LevelData recovered = LevelLoader::Load(recoveryPath_);
            LevelSnapshot snapshot;
            snapshot.objects = std::move(recovered.objects);
            snapshot.triggers = std::move(recovered.triggers);
            snapshot.checkpoints = std::move(recovered.checkpoints);
            snapshot.playerSpawn = recovered.playerSpawn;
            snapshot.enemySpawn = recovered.enemySpawn;
            snapshot.graphPath = recovered.graphPath;
            snapshot.flagGraphs = recovered.flagGraphs;
            ApplySnapshot(snapshot);
            dirty_ = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("使用しない", kButtonSize)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::End();
}

void StageEditor::RenderViewportContextMenu()
{
    if (contextMenuRequested_) {
        ImGui::OpenPopup("SceneContextMenu");
        contextMenuRequested_ = false;
    }
    if (!ImGui::BeginPopup("SceneContextMenu")) {
        return;
    }

    auto startTestHere = [&](const Vector3& position) {
        if (!StartPlayTestAt(position)) {
            statusMessage_ = "このシーンにはプレイヤーが登録されていません";
            statusTimer_ = StageEditor::kStatusNormalSeconds;
        }
    };

    if (contextKind_ == SelKind::Object && contextIndex_ >= 0 && contextIndex_ < static_cast<int>(objects_.size())) {
        ObjectDesc& desc = objects_[contextIndex_].desc;
        ImGui::TextDisabled("%s", desc.name.c_str());
        ImGui::Separator();
        if (ImGui::MenuItem("ここへカメラ (F)")) {
            FocusCameraOn(WorldPositionOf(desc));
        }
        if (ImGui::MenuItem("複製 (Ctrl+D)")) {
            DuplicateSelected();
        }
        if (ImGui::MenuItem("削除 (Delete)")) {
            DeleteSelected();
        }
        ImGui::Separator();
        if (!desc.parent.empty() && ImGui::MenuItem("親を外す")) {
            SetParentPreservingWorld(contextIndex_, -1);
        }
        const bool hasOtherSelection = selectedObjectIndices_.size() > 1;
        if (hasOtherSelection && ImGui::MenuItem("選択中の物をこの子にする")) {
            const std::vector<int> children = selectedObjectIndices_;
            for (int child : children) {
                if (child != contextIndex_) {
                    SetParentPreservingWorld(child, contextIndex_);
                }
            }
        }
        if (ImGui::MenuItem("クリックした物を親にする...")) {
            parentLinkChildIndex_ = contextIndex_;
            statusMessage_ = "親にしたい配置物をクリックしてください";
            statusTimer_ = StageEditor::kStatusLongSeconds;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("この位置からテスト")) {
            startTestHere(WorldPositionOf(desc));
        }
        ImGui::EndPopup();
        return;
    }

    if (contextKind_ == SelKind::Trigger && contextIndex_ >= 0 && contextIndex_ < static_cast<int>(triggers_.size())) {
        const TriggerDesc& desc = triggers_[contextIndex_].GetDesc();
        ImGui::TextDisabled("トリガー %s", desc.name.c_str());
        ImGui::Separator();
        if (ImGui::MenuItem("ここへカメラ (F)")) {
            FocusCameraOn(desc.position);
        }
        if (ImGui::MenuItem("削除 (Delete)")) {
            DeleteSelected();
        }
        if (ImGui::MenuItem("この位置からテスト")) {
            startTestHere(desc.position);
        }
        ImGui::EndPopup();
        return;
    }

    // 何も無い場所: ここに置く
    const Vector3 at = contextWorldPos_;
    ImGui::TextDisabled("ここに置く (%.1f, %.1f)", at.x, at.y);
    ImGui::Separator();
    if (ImGui::MenuItem("ブロック")) {
        AddObjectAt("prop", at);
    }
    if (ImGui::MenuItem("収集物")) {
        AddObjectAt("pickup", at);
    }
    if (ImGui::MenuItem("壊せる物")) {
        AddObjectAt("breakable", at);
    }
    if (ImGui::MenuItem("敵（剣）")) {
        AddObjectAt("enemy_basic", at);
    }
    if (ImGui::MenuItem("敵：ナイト")) {
        AddObjectAt("enemy_knight", at);
    }
    if (ImGui::MenuItem("出現ポイント")) {
        AddObjectAt("spawn_point", at);
    }
    if (ImGui::MenuItem("ギミック")) {
        AddObjectAt("gimmick", at);
    }
    if (ImGui::MenuItem("トリガー")) {
        AddTriggerAt(at);
    }
    if (ImGui::MenuItem("文章UI（画面に固定）")) {
        AddUITextAt(contextScreenPos_);
    }
    if (ImGui::BeginMenu("その他")) {
        if (ImGui::MenuItem("カメラポイント")) {
            AddObjectAt("camera_point", at);
        }
        if (ImGui::MenuItem("巡回ポイント")) {
            AddObjectAt("patrol_point", at);
        }
        if (ImGui::MenuItem("イベント条件")) {
            AddObjectAt("event_condition", at);
        }
        if (ImGui::MenuItem("ワールドテキスト")) {
            AddUITextAt(at, false);
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("テンプレート")) {
        if (ImGui::MenuItem("スライド扉")) {
            RecordUndoSnapshotNow();
            AppendGeneratedContent(StageEditorContentFactory::CreateSlidingDoor(at, templateDoorCondition_, nextSerial_));
        }
        if (ImGui::MenuItem("壊せる壁（ハンマー）")) {
            RecordUndoSnapshotNow();
            AppendGeneratedContent(StageEditorContentFactory::CreateBreakableWall(at, "Hammer", nextSerial_));
        }
        if (ImGui::MenuItem("区画トリガー＋案内文")) {
            RecordUndoSnapshotNow();
            AppendGeneratedContent(StageEditorContentFactory::CreateZoneGuide(at, templateGuideText_, nextSerial_));
        }
        if (ImGui::MenuItem("戦闘部屋")) {
            RecordUndoSnapshotNow();
            AppendGeneratedContent(StageEditorContentFactory::CreateBattleRoom(at, nextSerial_));
        }
        ImGui::EndMenu();
    }
    ImGui::Separator();
    if (ImGui::MenuItem("ここからテスト")) {
        startTestHere(at);
    }
    if (ImGui::MenuItem("ここへカメラ")) {
        FocusCameraOn(at);
    }
    ImGui::EndPopup();
}

void StageEditor::RenderStageAnalysisPanel()
{
    if (!showStageAnalysis_) {
        return;
    }
    ImGui::SetNextWindowSize(kAnalysisWindowSize, ImGuiCond_FirstUseEver);
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
            if (std::abs(a.x - b.x) < EditorUi::kOverlapEpsilon && std::abs(a.y - b.y) < EditorUi::kOverlapEpsilon
                && std::abs(a.z - b.z) < EditorUi::kOverlapEpsilon) {
                findings.push_back("同じ位置に配置されています: " + objects_[i].desc.name + " / " + objects_[j].desc.name);
            }
        }
    }

    if (findings.empty()) {
        ImGui::TextColored(EditorUi::kOkColor, "問題は見つかりませんでした");
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
    ImGui::SetNextWindowSize(kDiffWindowSize, ImGuiCond_FirstUseEver);
    ImGui::Begin("保存内容との差分", &showSavedDiff_);
    std::map<std::string, const ObjectDesc*> saved;
    for (const auto& desc : lastSavedSnapshot_.objects) {
        saved[desc.name] = &desc;
    }
    int differenceCount = 0;
    for (const auto& entry : objects_) {
        auto found = saved.find(entry.desc.name);
        if (found == saved.end()) {
            ImGui::TextColored(EditorUi::kOkColor, "+ 追加  %s", entry.desc.name.c_str());
            ++differenceCount;
            continue;
        }
        const ObjectDesc& old = *found->second;
        const ObjectDesc& now = entry.desc;
        if (old.position.x != now.position.x || old.position.y != now.position.y || old.position.z != now.position.z
            || old.rotation.x != now.rotation.x || old.rotation.y != now.rotation.y || old.rotation.z != now.rotation.z
            || old.scale.x != now.scale.x || old.scale.y != now.scale.y || old.scale.z != now.scale.z
            || old.enabled != now.enabled || old.kind != now.kind || old.activationFlag != now.activationFlag) {
            ImGui::TextColored(EditorUi::kChangedColor, "~ 変更  %s", now.name.c_str());
            ++differenceCount;
        }
        saved.erase(found);
    }
    for (const auto& [name, desc] : saved) {
        ImGui::TextColored(EditorUi::kErrorColor, "- 削除  %s", name.c_str());
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
    ImGui::SetNextWindowSize(kHelpWindowSize, ImGuiCond_FirstUseEver);
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

#endif
