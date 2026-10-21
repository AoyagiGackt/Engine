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
#include <iterator>
#endif
using namespace engine::graphics;

#ifdef USE_IMGUI

namespace engine::game {
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


// ヘルプの操作一覧に並べるキー割り当て（HandleEditorShortcuts・StageEditorViewport::UpdateCamera等の実装と揃える）
struct ControlBinding {
    const char* key;
    const char* action;
};

/** @brief 操作ヘルプの見出しとその節に並べるキー割り当て */
struct ControlSection {
    const char* title;
    const ControlBinding* bindings;
    int count;
};

constexpr ControlBinding kGeneralControls[] = {
    { "F2", "エディタの表示/非表示" },
    { "F4", "編集パネルを隠してゲーム画面を広く見る" },
    { "Ctrl+S", "保存" },
    { "Ctrl+Z / Ctrl+Y", "元に戻す / やり直す" },
};

constexpr ControlBinding kCameraControls[] = {
    { "右ボタン押しながら WASD", "カメラを上下左右へ移動" },
    { "右ボタン押しながら Q / E", "カメラを奥 / 手前へ移動" },
    { "マウスホイール", "ズーム" },
    { "中ボタンドラッグ", "カメラを平行移動" },
    { "F", "選択物へカメラを寄せる（未選択ならプレイヤー）" },
};

constexpr ControlBinding kSelectionControls[] = {
    { "左クリック", "選択" },
    { "Ctrl+左クリック", "選択に追加" },
    { "何も無い所から左ドラッグ", "範囲選択" },
    { "Ctrl+A", "すべての配置物を選択" },
    { "右クリック", "その場のメニュー（配置・テスト開始など）" },
};

constexpr ControlBinding kTransformControls[] = {
    { "W / E / R", "移動 / 回転 / 拡縮ツールに切り替え" },
    { "左ドラッグ", "選択物を動かす（ハンドルの軸を掴むとその軸だけ）" },
    { "Shift+ドラッグ", "奥行き(Z)方向へ移動" },
    { "Ctrl+ドラッグ", "グリッドに揃える / 回転は15度刻み" },
    { "矢印キー", "少しずつ動かす（Shift+上下で奥行き）" },
};

constexpr ControlBinding kEditControls[] = {
    { "Ctrl+D", "複製" },
    { "Ctrl+C / Ctrl+V", "コピー / 貼り付け" },
    { "Delete", "削除" },
};

constexpr ControlSection kControlSections[] = {
    { "全体", kGeneralControls, static_cast<int>(std::size(kGeneralControls)) },
    { "カメラ", kCameraControls, static_cast<int>(std::size(kCameraControls)) },
    { "選択", kSelectionControls, static_cast<int>(std::size(kSelectionControls)) },
    { "移動・回転・拡縮", kTransformControls, static_cast<int>(std::size(kTransformControls)) },
    { "編集", kEditControls, static_cast<int>(std::size(kEditControls)) },
};

constexpr int kControlTableColumns = 2;
constexpr float kControlKeyColumnWidth = 190.0f;

/** @brief 操作一覧を項目ごとの表で描く */
void DrawControlsReference()
{
    ImGui::TextDisabled("カメラとマウス操作は中央のシーンビュー上で行う / 文字入力中はショートカット無効");
    for (const ControlSection& section : kControlSections)
    {
        ImGui::SeparatorText(section.title);
        if (!ImGui::BeginTable(section.title, kControlTableColumns, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV))
        {
            continue;
        }
        ImGui::TableSetupColumn("キー", ImGuiTableColumnFlags_WidthFixed, kControlKeyColumnWidth);
        ImGui::TableSetupColumn("動作", ImGuiTableColumnFlags_WidthStretch);
        for (int i = 0; i < section.count; ++i)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(EditorUi::kChangedColor, "%s", section.bindings[i].key);
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", section.bindings[i].action);
        }
        ImGui::EndTable();
    }
}
} // namespace

/** @brief プレハブ、検証、自動保存、編集とテストの切り替えをまとめて表示する */
void StageEditor::RenderWorkflowPanel()
{
    // 初回だけ決まった位置と大きさで開く（その後はユーザーが動かした位置を保つ）
    ImGui::SetNextWindowPos(kWorkflowWindowPos, ImGuiCond_Once);
    ImGui::SetNextWindowSize(kWorkflowWindowSize, ImGuiCond_Once);
    ImGui::Begin("編集ワークフロー");

    // 編集とテストプレイの切り替えと、今どちらの状態かの表示
    if (ImGui::Button(playTestMode_ ? "編集モードへ戻る" : "現在の配置でテスト", ImVec2(-1.0f, 0.0f)))
    {
        SetPlayTestMode(!playTestMode_);
    }
    ImGui::TextDisabled(playTestMode_ ? "ゲーム更新中  F2で終了" : "ゲーム停止中  配置を安全に編集できます");

    // 画面中央・選択位置からのテスト開始と、カメラ・グリッドの補助
    ImGui::SeparatorText("テスト");
    if (ImGui::Button("画面中央からテスト", kWideButtonSize))
    {
        StartPlayTestOrWarn(ViewCenterOnGround());
    }
    ImGui::SameLine();
    // 何も選んでいない時は開始位置が決まらないので押せなくする
    ImGui::BeginDisabled(selKind_ == SelKind::None);
    if (ImGui::Button("選択位置からテスト", kWideButtonSize))
    {
        // 選択物の位置が取れない種類なら画面中央から始める
        Vector3 start;
        if (!SelectionKindOf(selKind_).WorldPosition(*this, selIndex_, start))
        {
            start = ViewCenterOnGround();
        }
        StartPlayTestOrWarn(start);
    }
    ImGui::EndDisabled();
    EditorUI::HelpMarker("プレイヤーをその場所へ移してテストを始めます。後半の区画を何度も確かめる時に使います。F2で編集へ戻ります");
    if (ImGui::Button("選択へカメラ (F)", kWideButtonSize))
    {
        FocusCameraOnSelection();
    }
    ImGui::SameLine();
    ImGui::Checkbox("グリッド線", &showGrid_);
    EditorUI::HelpMarker("スナップON中、スナップ間隔のグリッド線を表示します。矢印キーで選択物をスナップ間隔ぶん動かせます（Shift+上下で奥行き）");

    // ドラッグで動かす時の軸の制限
    ImGui::SeparatorText("移動ギズモ");
    ImGui::RadioButton("自由", &gizmoAxis_, StageEditorUiStyle::kGizmoAxisFree);
    ImGui::SameLine();
    ImGui::RadioButton("X", &gizmoAxis_, StageEditorUiStyle::kGizmoAxisX);
    ImGui::SameLine();
    ImGui::RadioButton("Y", &gizmoAxis_, StageEditorUiStyle::kGizmoAxisY);
    ImGui::SameLine();
    ImGui::RadioButton("Z", &gizmoAxis_, StageEditorUiStyle::kGizmoAxisZ);

    // 選択物をプレハブとして保存・画面中央へ配置
    ImGui::SeparatorText("プレハブ");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##prefabName", "英数字のプレハブ名", prefabName_, sizeof(prefabName_));
    // 保存できるのは配置物を選んでいる時だけ
    ImGui::BeginDisabled(selKind_ != SelKind::Object);
    if (ImGui::Button("選択物を保存", kWideButtonSize))
    {
        SaveSelectedPrefab();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("配置", kWideButtonSize))
    {
        InstantiatePrefab();
    }

    // 自動保存の切り替えと、解析・差分・ヘルプの各パネルを開くボタン
    ImGui::Checkbox("30秒ごとに自動保存", &autoSaveEnabled_);
    if (ImGui::Button("ステージ解析"))
    {
        showStageAnalysis_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("保存差分"))
    {
        showSavedDiff_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("ヘルプ"))
    {
        showEditorHelp_ = true;
    }
    // 保存前検証を実行し、問題の数を知らせる
    if (ImGui::Button("保存前検証", ImVec2(-1.0f, 0.0f)))
    {
        // 結果は下の一覧で見られるよう残しておく
        validationIssues_ = ValidateLevel();
        statusMessage_ = validationIssues_.empty()
            ? "検証完了: 問題はありません"
            : "検証完了: " + std::to_string(validationIssues_.size()) + "件の問題があります";
        statusTimer_ = StageEditor::kStatusLongSeconds;
    }
    // 問題があった時だけ、1件ずつ移動ボタンと問題文を並べる
    if (!validationIssues_.empty() && ImGui::TreeNode("検証結果"))
    {
        for (int issueIndex = 0; issueIndex < static_cast<int>(validationIssues_.size()); ++issueIndex)
        {
            const std::string& issue = validationIssues_[issueIndex];
            // 移動ボタンが並ぶので問題の番号で区別する
            ImGui::PushID(issueIndex);
            // 問題文に名前が含まれる配置物を選び、カメラをそこへ寄せる
            if (ImGui::SmallButton("移動"))
            {
                for (int objectIndex = 0; objectIndex < static_cast<int>(objects_.size()); ++objectIndex)
                {
                    if (!objects_[objectIndex].desc.name.empty()
                        && issue.find(objects_[objectIndex].desc.name) != std::string::npos)
                    {
                        selKind_ = SelKind::Object;
                        selIndex_ = objectIndex;
                        selectedObjectIndices_ = { objectIndex };
                        // 奥行きは変えず、横と高さだけを配置物に合わせる
                        if (camera_)
                        {
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

    // 通常保存より新しい自動保存が見つかったら、復旧するかを1度だけ聞く
    if (recoveryAvailable_)
    {
        ImGui::OpenPopup("自動保存の復旧");
        recoveryAvailable_ = false;
    }
    if (ImGui::BeginPopupModal("自動保存の復旧", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("通常保存より新しい自動保存データがあります。");
        if (ImGui::Button("復旧する", kButtonSize))
        {
            // 自動保存を読み、元に戻す履歴と同じ形にして丸ごと差し替える（復旧後は未保存扱い）
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
        if (ImGui::Button("使用しない", kButtonSize))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::End();
}

/** @brief 右クリックメニューを描く（RenderEditorPanelsから毎フレーム呼ぶ） */
void StageEditor::RenderViewportContextMenu()
{
    // ビューで右クリックされたフレームだけメニューを開く
    if (contextMenuRequested_)
    {
        ImGui::OpenPopup("SceneContextMenu");
        contextMenuRequested_ = false;
    }
    if (!ImGui::BeginPopup("SceneContextMenu"))
    {
        return;
    }

    // 配置物やトリガーの上なら、その種類のメニュー（複製・削除など）を出して終わる
    if (SelectionKindOf(contextKind_).DrawContextMenu(*this, contextIndex_))
    {
        ImGui::EndPopup();
        return;
    }

    // 何も無い場所: ここに置く
    const Vector3 at = contextWorldPos_;
    ImGui::TextDisabled("ここに置く (%.1f, %.1f)", at.x, at.y);
    ImGui::Separator();
    /** @brief 右クリックの位置に置ける配置物（メニューの表示名と種類名） */
    struct PlaceMenuItem {
        const char* label;
        const char* kind;
    };
    static constexpr PlaceMenuItem kPlaceItems[] = {
        { "ブロック", "prop" },
        { "収集物", "pickup" },
        { "壊せる物", "breakable" },
        { "敵（剣）", "enemy_basic" },
        { "敵：ナイト", "enemy_knight" },
        { "出現ポイント", "spawn_point" },
        { "ギミック", "gimmick" },
    };
    static constexpr PlaceMenuItem kOtherPlaceItems[] = {
        { "カメラポイント", "camera_point" },
        { "巡回ポイント", "patrol_point" },
        { "イベント条件", "event_condition" },
    };
    // 表の順にメニューへ並べ、選ばれた種類をその場所に置く
    auto placeMenu = [&](const auto& items) {
        for (const PlaceMenuItem& item : items)
        {
            if (ImGui::MenuItem(item.label))
            {
                AddObjectAt(item.kind, at);
            }
        }
    };
    placeMenu(kPlaceItems);
    if (ImGui::MenuItem("トリガー"))
    {
        AddTriggerAt(at);
    }
    if (ImGui::MenuItem("文章UI（画面に固定）"))
    {
        AddUITextAt(contextScreenPos_);
    }
    if (ImGui::BeginMenu("その他"))
    {
        placeMenu(kOtherPlaceItems);
        if (ImGui::MenuItem("ワールドテキスト"))
        {
            AddUITextAt(at, false);
        }
        ImGui::EndMenu();
    }
    // まとめて生成するテンプレート（元に戻せるよう先に履歴を取る）
    if (ImGui::BeginMenu("テンプレート"))
    {
        if (ImGui::MenuItem("スライド扉"))
        {
            RecordUndoSnapshotNow();
            AppendGeneratedContent(StageEditorContentFactory::CreateSlidingDoor(at, templateDoorCondition_, nextSerial_));
        }
        if (ImGui::MenuItem("壊せる壁（ハンマー）"))
        {
            RecordUndoSnapshotNow();
            AppendGeneratedContent(StageEditorContentFactory::CreateBreakableWall(at, "Hammer", nextSerial_));
        }
        if (ImGui::MenuItem("区画トリガー＋案内文"))
        {
            RecordUndoSnapshotNow();
            AppendGeneratedContent(StageEditorContentFactory::CreateZoneGuide(at, templateGuideText_, nextSerial_));
        }
        if (ImGui::MenuItem("戦闘部屋"))
        {
            RecordUndoSnapshotNow();
            AppendGeneratedContent(StageEditorContentFactory::CreateBattleRoom(at, nextSerial_));
        }
        ImGui::EndMenu();
    }
    ImGui::Separator();
    // その場所からのテスト開始とカメラ移動
    if (ImGui::MenuItem("ここからテスト"))
    {
        StartPlayTestOrWarn(at);
    }
    if (ImGui::MenuItem("ここへカメラ"))
    {
        FocusCameraOn(at);
    }
    ImGui::EndPopup();
}

/** @brief 配置・接続・到達性の問題を解析して一覧表示する */
void StageEditor::RenderStageAnalysisPanel()
{
    if (!showStageAnalysis_)
    {
        return;
    }
    ImGui::SetNextWindowSize(kAnalysisWindowSize, ImGuiCond_FirstUseEver);
    ImGui::Begin("ステージ解析", &showStageAnalysis_);
    std::vector<std::string> findings = ValidateLevel();

    // 敵グループごとに、敵の数と、そのグループの全滅を待つ条件の数を数える
    std::map<std::string, int> groupEnemyCounts;
    std::map<std::string, int> groupConditionCounts;
    for (const auto& entry : objects_)
    {
        if (!entry.desc.enemyGroup.empty()
            && ObjectKind::Of(entry.desc.kind).PlacesEnemy())
        {
            ++groupEnemyCounts[entry.desc.enemyGroup];
        }
        if (ObjectKind::Of(entry.desc.kind).WatchesEnemyGroup(entry.desc))
        {
            ++groupConditionCounts[entry.desc.enemyGroup];
        }
    }
    // 敵のいない全滅条件と、全滅しても何も起きない敵グループを知らせる
    for (const auto& [group, count] : groupConditionCounts)
    {
        if (group.empty() || !groupEnemyCounts.contains(group))
        {
            findings.push_back("敵が存在しない全滅条件です: " + group);
        }
    }
    for (const auto& [group, count] : groupEnemyCounts)
    {
        if (!groupConditionCounts.contains(group))
        {
            findings.push_back("全滅後の処理がない敵グループです: " + group);
        }
    }
    // ほぼ同じ位置に重なって置かれた配置物の組を知らせる
    for (size_t i = 0; i < objects_.size(); ++i)
    {
        for (size_t j = i + 1; j < objects_.size(); ++j)
        {
            const Vector3 a = WorldPositionOf(objects_[i].desc);
            const Vector3 b = WorldPositionOf(objects_[j].desc);
            if (std::abs(a.x - b.x) < EditorUi::kOverlapEpsilon && std::abs(a.y - b.y) < EditorUi::kOverlapEpsilon
                && std::abs(a.z - b.z) < EditorUi::kOverlapEpsilon)
            {
                findings.push_back("同じ位置に配置されています: " + objects_[i].desc.name + " / " + objects_[j].desc.name);
            }
        }
    }

    // 見つかった項目を一覧で出す
    if (findings.empty())
    {
        ImGui::TextColored(EditorUi::kOkColor, "問題は見つかりませんでした");
    }
    else
    {
        ImGui::Text("%d件の確認項目", static_cast<int>(findings.size()));
        for (const std::string& finding : findings)
        {
            ImGui::BulletText("%s", finding.c_str());
        }
    }
    ImGui::Separator();
    // 選択中の配置物の位置からテストを始める
    ImGui::BeginDisabled(selKind_ != SelKind::Object || selIndex_ < 0);
    if (ImGui::Button("選択位置からテスト開始", ImVec2(-1.0f, 0.0f)))
    {
        StartPlayTestOrWarn(WorldPositionOf(objects_[selIndex_].desc));
    }
    ImGui::EndDisabled();
    ImGui::End();
}

/** @brief 最後に保存した状態との差分を一覧表示する */
void StageEditor::RenderDiffPanel()
{
    // 閉じている時は比べない
    if (!showSavedDiff_)
    {
        return;
    }
    ImGui::SetNextWindowSize(kDiffWindowSize, ImGuiCond_FirstUseEver);
    ImGui::Begin("保存内容との差分", &showSavedDiff_);
    // 最後に保存した時の配置物を名前で引けるようにする
    std::map<std::string, const ObjectDesc*> saved;
    for (const auto& desc : lastSavedSnapshot_.objects)
    {
        saved[desc.name] = &desc;
    }
    // 今の配置物を1つずつ保存時と比べ、無ければ追加、位置・回転・拡縮・有効・種類・フラグが違えば変更として出す
    int differenceCount = 0;
    for (const auto& entry : objects_)
    {
        auto found = saved.find(entry.desc.name);
        if (found == saved.end())
        {
            ImGui::TextColored(EditorUi::kOkColor, "+ 追加  %s", entry.desc.name.c_str());
            ++differenceCount;
            continue;
        }
        const ObjectDesc& old = *found->second;
        const ObjectDesc& now = entry.desc;
        if (old.position.x != now.position.x || old.position.y != now.position.y || old.position.z != now.position.z
            || old.rotation.x != now.rotation.x || old.rotation.y != now.rotation.y || old.rotation.z != now.rotation.z
            || old.scale.x != now.scale.x || old.scale.y != now.scale.y || old.scale.z != now.scale.z
            || old.enabled != now.enabled || old.kind != now.kind || old.activationFlag != now.activationFlag)
        {
            ImGui::TextColored(EditorUi::kChangedColor, "~ 変更  %s", now.name.c_str());
            ++differenceCount;
        }
        saved.erase(found);
    }
    // 比べ終えて保存時の側にだけ残った物は削除された物
    for (const auto& [name, desc] : saved)
    {
        ImGui::TextColored(EditorUi::kErrorColor, "- 削除  %s", name.c_str());
        ++differenceCount;
    }
    if (differenceCount == 0)
    {
        ImGui::TextDisabled("最後の保存から変更はありません");
    }
    ImGui::End();
}

/** @brief 制作手順と確認項目をエディタ内に表示する */
void StageEditor::RenderEditorHelpPanel()
{
    // 閉じている時は何も出さない。開いた時はタブで操作一覧と制作手順を切り替える
    if (!showEditorHelp_)
    {
        return;
    }
    ImGui::SetNextWindowSize(kHelpWindowSize, ImGuiCond_FirstUseEver);
    ImGui::Begin("ステージ制作ヘルプ", &showEditorHelp_);
    // ウィンドウが畳まれていてタブが出せない時は、ウィンドウを閉じて終わる
    if (!ImGui::BeginTabBar("##helpTabs"))
    {
        ImGui::End();
        return;
    }
    // 1つ目のタブ: キーとマウスの操作一覧
    if (ImGui::BeginTabItem("操作一覧"))
    {
        DrawControlsReference();
        ImGui::EndTabItem();
    }
    // 2つ目のタブ: 制作の手順・用語・困った時の確認・チェックリスト（タブが閉じていれば終わる）
    if (!ImGui::BeginTabItem("制作手順"))
    {
        ImGui::EndTabBar();
        ImGui::End();
        return;
    }
    ImGui::SeparatorText("最初のステージを作る手順");
    ImGui::TextWrapped("この手順では、部屋に入ると敵が出現し、全滅すると出口が開く場面を作成します。");
    // 手順は見出し行と、その下の補足説明を組にして並べる
    ImGui::BulletText("1  アセットパレットから床と壁を配置する");
    ImGui::TextWrapped("   配置物を選択し、詳細設定の当たり判定を有効にします。床や壁はプレイヤーが通り抜けないsolid配置物にします。");
    ImGui::BulletText("2  イベントパネルの戦闘部屋テンプレートを生成する");
    ImGui::TextWrapped("   進入トリガー、敵の出現地点、全滅条件、出口、カメラ演出が一括で作られます。初回はここから始めるのが簡単です。");
    ImGui::BulletText("3  中央ビューで各部品を選択して位置を調整する");
    ImGui::TextWrapped("   入口に水色のトリガー、戦闘場所に敵の出現地点、奥に出口を移動します。右の詳細設定で数値も直接変更できます。");
    // 後半は確認と保存の手順
    ImGui::BulletText("4  上部のテストを押し、実際に入口から通して遊ぶ");
    ImGui::TextWrapped("   敵が出ない場合はイベントの現在の接続を確認します。出口が開かない場合は敵グループ名と全滅条件のグループ名を揃えます。");
    ImGui::BulletText("5  制作パネルで保存前検証を行い、問題がなければ保存する");

    // 種類・イベントの考え方・困った時の確認は、必要な時だけ開く折りたたみにする
    if (ImGui::CollapsingHeader("配置物の種類"))
    {
        ImGui::BulletText("地形  見た目と当たり判定を持つ床や壁を作る");
        ImGui::BulletText("敵  ステージ開始時から存在する敵を置く");
        ImGui::BulletText("出現地点  条件成立後に敵を出現させる");
        ImGui::BulletText("ギミック  扉、足場、点滅、移動など条件で動く物を作る");
        ImGui::BulletText("イベント条件  敵グループ全滅などを次の動作へつなぐ");
        ImGui::BulletText("カメラ地点  条件成立時に指定位置と角度へカメラを移動する");
        ImGui::BulletText("トリガー  プレイヤーが範囲へ入ったことを条件にする");
    }
    // イベントを3つの問いに分けて説明する
    if (ImGui::CollapsingHeader("イベントの考え方"))
    {
        ImGui::TextWrapped("イベントは、いつ、何を、どうするの3項目で作ります。");
        ImGui::BulletText("いつ  進入トリガー、敵グループ全滅などを選ぶ");
        ImGui::BulletText("何を  敵の出現地点、扉、カメラ地点を選ぶ");
        ImGui::BulletText("どうする  有効化または無効化と、実行までの秒数を選ぶ");
        ImGui::TextWrapped("一つの条件から複数の対象へ接続できます。敵を出し、扉を閉め、カメラを動かす処理を同じ進入トリガーから作れます。");
    }
    // よくある症状と、まず確かめる場所の組
    if (ImGui::CollapsingHeader("困ったとき"))
    {
        ImGui::BulletText("敵が出ない  開始条件と出現地点の接続、対象の有効設定を確認する");
        ImGui::BulletText("出口が開かない  敵と全滅条件のグループ名を確認する");
        ImGui::BulletText("選択できない  中央シーンビュー内でクリックし、パネル上では操作しない");
        ImGui::BulletText("カメラが戻らない  カメラ地点の保持時間と次のカメラ演出を確認する");
        ImGui::BulletText("変更が消えた  未保存表示を確認し、Ctrl+Sで保存する");
        ImGui::BulletText("原因が分からない  制作パネルの保存前検証とステージ解析を実行する");
    }
    ImGui::SeparatorText("制作チェックリスト");
    // 保存前に目で確かめる項目（チェック状態はエディタを開いている間だけ覚える）
    static constexpr const char* kChecklistLabels[] = {
        "開始地点から出口まで移動できる",
        "すべての敵グループに全滅後の処理がある",
        "カメラ演出後に操作画面へ戻る",
        "ギミックでプレイヤーを閉じ込めない",
        "保存前検証に問題がない",
    };
    // 項目数とチェック状態の配列の大きさがずれたらコンパイル時に気付けるようにする
    static_assert(std::size(kChecklistLabels) == kHelpChecklistCount);
    for (int i = 0; i < kHelpChecklistCount; ++i)
    {
        ImGui::Checkbox(kChecklistLabels[i], &helpChecklist_[i]);
    }
    // 開いたタブ・タブバー・ウィンドウを内側から順に閉じる
    ImGui::EndTabItem();
    ImGui::EndTabBar();
    ImGui::End();
}

} // namespace engine::game

#endif
