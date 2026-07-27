/**
 * @file StageEditor.cpp
 * @brief ステージ配置の実体管理と実行時編集ワークフローを実装するファイル
 */
#include "StageEditor.h"
#include "Camera.h"
#include "DiagnosticsDraw.h"
#include "DirectXCommon.h"
#include "EnemyEntity.h"
#include "EnemyRegistry.h"
#include "GameFlags.h"
#include "Input.h"
#include "KnightEnemy.h"
#include "Matrix4x4.h"
#include "Model.h"
#include "ModelCommon.h"
#include "Object3d.h"
#include "ParticleManager.h"
#include "StageEditorPanels.h"
#include "StageEditorPrefabService.h"
#include "StageEditorSelectionService.h"
#include "TimeManager.h"
#include "WinApp.h"
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <set>
#include <unordered_set>
#ifdef USE_IMGUI
#include "EditorUI.h"
#include <commdlg.h>
#include <imgui.h>
#pragma comment(lib, "comdlg32.lib")
#endif
using namespace engine::game;
using namespace engine;
using namespace engine::graphics;

// ══════════════════════════════════════════════════════
// ファイル操作とデータ管理
// ══════════════════════════════════════════════════════

StageEditor::StageEditor() = default;

void StageEditor::Open(const std::string& levelPath, ModelCommon* modelCommon, Camera* camera)
{
    // 再読み込み前に旧レベルのGPU参照を完了させ、実体からモデルの順で破棄する
    ReleaseLevelResources(false);

    levelPath_ = levelPath;
    modelCommon_ = modelCommon;
    camera_ = camera;
    viewport_.SetCamera(camera);

    LevelData data = LevelLoader::Load(levelPath);
    playerSpawn_ = data.playerSpawn;
    enemySpawn_ = data.enemySpawn;

    for (auto& desc : data.objects) {
        ObjectEntry entry;
        entry.desc = std::move(desc);
        entry.authoredPosition = entry.desc.position;
        entry.runtimeActive = IsRuntimeActive(entry.desc) && entry.desc.activationDelay <= 0.0f;
        objects_.push_back(std::move(entry));
    }

    // 新規作成時の連番(obj_N等)が、読み込んだレベルに既にある名前と衝突しないよう、
    // "_数字"で終わる名前を走査してnextSerial_をその最大値+1から再開させる
    // （これを怠ると保存済みのobj_0等と同じ名前が新規オブジェクトに振られ、
    //   保存前検証の名前重複エラーで保存できなくなる）
    auto bumpSerialPastName = [this](const std::string& name) {
        const size_t underscorePos = name.find_last_of('_');
        if (underscorePos == std::string::npos || underscorePos + 1 >= name.size()) {
            return;
        }
        const std::string suffix = name.substr(underscorePos + 1);
        if (!std::all_of(suffix.begin(), suffix.end(), [](unsigned char c) { return std::isdigit(c); })) {
            return;
        }
        const int value = std::atoi(suffix.c_str());
        if (value >= nextSerial_) {
            nextSerial_ = value + 1;
        }
    };
    for (const auto& entry : objects_) {
        bumpSerialPastName(entry.desc.name);
    }
    for (const auto& trigger : data.triggers) {
        bumpSerialPastName(trigger.name);
    }

    // EnemyRegistryへの登録キーになるため、実体を生成する前に名前を確定させる
    EnsureUniqueNames(); // 手書きJSON等で名前が無い/重複しているエントリに自動命名する（親子参照に必要）

    for (auto& entry : objects_) {
        RegenerateInstances(entry);
    }

    triggers_.clear();
    for (const auto& desc : data.triggers) {
        TriggerVolume trg;
        trg.Init(desc);
        triggers_.push_back(std::move(trg));
    }
    checkpoints_ = data.checkpoints;

    selKind_ = SelKind::None;
    selIndex_ = -1;
    selectedObjectIndices_.clear();
#ifdef USE_IMGUI
    eventConnection_.Reset();
    // 別ファイルを開いたら、直前のレベルに対するUndo/Redo履歴は無関係になるため破棄する
    history_.Clear();
    dirty_ = false;
    lastSavedSnapshot_ = MakeSnapshot();
    recoveryPath_ = levelPath_ + ".autosave.json";
    std::error_code fileError;
    recoveryAvailable_ = std::filesystem::exists(recoveryPath_, fileError)
        && std::filesystem::exists(levelPath_, fileError)
        && std::filesystem::last_write_time(recoveryPath_, fileError)
            > std::filesystem::last_write_time(levelPath_, fileError);
#endif
    statusMessage_ = "読み込みました: " + levelPath_;
    statusTimer_ = 2.0f;
}

StageEditor::~StageEditor()
{
    Finalize();
}

void StageEditor::Finalize()
{
    if (visible_) {
        TimeManager::GetInstance()->SetTimeScale(savedTimeScale_);
    }
    visible_ = false;
    playTestMode_ = false;
    ReleaseLevelResources(true);
    viewport_.Reset();
    camera_ = nullptr;
    modelCommon_ = nullptr;
}

void StageEditor::ReleaseLevelResources(bool releaseExternalEntities)
{
    // 描画に使用した実体を解放する前にGPUからの参照完了を保証する
    if ((!objects_.empty() || !modelStorage_.empty()) && modelCommon_ && modelCommon_->GetDxCommon()) {
        modelCommon_->GetDxCommon()->WaitForGpu();
    }

    // レジストリ参照、描画実体、参照キャッシュ、所有モデルの順に破棄する
    for (auto& entry : objects_) {
        DestroyObjectRuntime(entry, false);
    }
    objects_.clear();
    modelCache_.clear();
    modelStorage_.clear();
    triggers_.clear();
    checkpoints_.clear();
    if (releaseExternalEntities) {
        externalEntities_.clear();
    }
}

void StageEditor::UnregisterEnemyEntity(const ObjectEntry& entry)
{
    if (entry.enemy) {
        EnemyRegistry::GetInstance()->Unregister(entry.desc.name);
    }
}

void StageEditor::DestroyObjectRuntime(ObjectEntry& entry, bool waitForGpu)
{
    const bool ownsRuntime = !entry.instances.empty() || entry.knight || entry.enemy;
    if (waitForGpu && ownsRuntime && modelCommon_ && modelCommon_->GetDxCommon()) {
        modelCommon_->GetDxCommon()->WaitForGpu();
    }

    // レジストリは実体を参照するため、所有ポインタを破棄する前に登録を解除する。
    UnregisterEnemyEntity(entry);
    entry.instances.clear();
    entry.knight.reset();
    entry.enemy.reset();
}

void StageEditor::EnsureUniqueNames()
{
    std::set<std::string> used;
    for (auto& entry : objects_) {
        std::string& name = entry.desc.name;
        if (name.empty() || used.count(name)) {
            std::string candidate;
            do {
                candidate = "obj_" + std::to_string(nextSerial_++);
            } while (used.count(candidate));
            name = candidate;
        }
        used.insert(name);
    }
}

Vector3 StageEditor::ParentWorldPositionOf(const ObjectDesc& desc) const
{
    Vector3 pos = { };
    const ObjectDesc* cur = &desc;
    for (int guard = 0; guard < 16 && !cur->parent.empty(); ++guard) {
        const ObjectDesc* parent = nullptr;
        for (const auto& entry : objects_) {
            if (entry.desc.name == cur->parent) {
                parent = &entry.desc;
                break;
            }
        }
        if (!parent) {
            break;
        }
        pos = pos + parent->position;
        cur = parent;
    }
    return pos;
}

Vector3 StageEditor::WorldPositionOf(const ObjectDesc& desc) const
{
    Vector3 result = desc.position;
    const ObjectDesc* cur = &desc;
    for (int guard = 0; guard < 16 && !cur->parent.empty(); ++guard) {
        const ObjectDesc* parent = nullptr;
        for (const auto& entry : objects_) {
            if (entry.desc.name == cur->parent) {
                parent = &entry.desc;
                break;
            }
        }
        if (!parent) {
            break;
        }
        const float c = std::cos(parent->rotation.z);
        const float s = std::sin(parent->rotation.z);
        result = { result.x * c - result.y * s + parent->position.x,
            result.x * s + result.y * c + parent->position.y,
            result.z + parent->position.z };
        cur = parent;
    }
    return result;
}

bool StageEditor::IsDescendantOf(const std::string& candidateName, const std::string& selfName) const
{
    // candidate から親を辿って self に行き着くなら子孫（親に設定すると循環する）
    const ObjectDesc* cur = nullptr;
    for (const auto& entry : objects_) {
        if (entry.desc.name == candidateName) {
            cur = &entry.desc;
            break;
        }
    }
    for (int guard = 0; guard < 16 && cur; ++guard) {
        if (cur->parent.empty()) {
            return false;
        }
        if (cur->parent == selfName) {
            return true;
        }
        const ObjectDesc* next = nullptr;
        for (const auto& entry : objects_) {
            if (entry.desc.name == cur->parent) {
                next = &entry.desc;
                break;
            }
        }
        cur = next;
    }
    return false;
}

void StageEditor::Save()
{
#ifdef USE_IMGUI
    validationIssues_ = ValidateLevel();
    if (!validationIssues_.empty()) {
        statusMessage_ = "保存前検証で問題が見つかりました";
        statusTimer_ = 4.0f;
        return;
    }
#endif
    SaveToPath(levelPath_);
#ifdef USE_IMGUI
    dirty_ = false;
    autoSaveElapsed_ = 0.0f;
    lastSavedSnapshot_ = MakeSnapshot();
#endif
    statusMessage_ = "保存しました: " + levelPath_;
    statusTimer_ = 2.0f;
}

void StageEditor::SaveToPath(const std::string& path) const
{
    LevelData data;
    data.playerSpawn = playerSpawn_;
    data.enemySpawn = enemySpawn_;
    for (const auto& entry : objects_) {
        data.objects.push_back(entry.desc);
    }
    for (const auto& trigger : triggers_) {
        data.triggers.push_back(trigger.GetDesc());
    }
    data.checkpoints = checkpoints_;
    LevelLoader::Save(path, data);
}

void StageEditor::Update(Input* input, const Vector3& playerPos)
{
    // トリガー判定はエディタの表示状態に関係なく常に行う（普段のプレイ中でも成立させるため）
    for (auto& trg : triggers_) {
        const bool justFired = trg.Update(playerPos);
        if (justFired && trg.GetDesc().spawnsWaterSplash && onWaterSplashRequested_) {
            onWaterSplashRequested_(trg.GetDesc().position);
        }
    }

#ifdef USE_IMGUI
    UpdateEditorVisibility(input);
    if (!visible_) {
        return;
    }

    // F4で編集状態を維持したままパネル表示だけを切り替える
    // （F3はゲーム側の当たり判定オーバーレイ表示と統一するためF4にしている）
    if (ImGui::IsKeyPressed(ImGuiKey_F4, false)) {
        viewportFocusMode_ = !viewportFocusMode_;
    }

    const float realDt = ImGui::GetIO().DeltaTime;
    UpdateAutoSave(realDt);
    if (statusTimer_ > 0.0f) {
        statusTimer_ -= realDt;
    }

    UpdateFreeCamera(input, realDt);

    if (camera_) {
        DiagnosticsDraw::SetCamera(camera_->GetViewProjectionMatrix(),
            static_cast<float>(WinApp::kClientWidth), static_cast<float>(WinApp::kClientHeight));
    }

    UpdateViewportInteraction();

    HandleEditorShortcuts();
    RenderEditorPanels();
    DrawGizmos();
#else
    (void)input;
#endif
}

#ifdef USE_IMGUI
void StageEditor::UpdateEditorVisibility(Input* input)
{
    const bool wasVisible = visible_;
    if (input && input->TriggerKey(DIK_F2)) {
        visible_ = !visible_;
    }
    if (visible_ == wasVisible) {
        return;
    }
    if (visible_) {
        savedTimeScale_ = TimeManager::GetInstance()->GetTimeScale();
        TimeManager::GetInstance()->SetTimeScale(0.0f);
        return;
    }
    playTestMode_ = false;
    TimeManager::GetInstance()->SetTimeScale(savedTimeScale_);
}

void StageEditor::HandleEditorShortcuts()
{
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) {
        return;
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)) {
        Undo();
    } else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y)) {
        Redo();
    } else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) {
        Save();
    } else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D)) {
        DuplicateSelected();
    } else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C)) {
        CopySelected();
    } else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V)) {
        PasteClipboard();
    } else if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
        DeleteSelected();
    }
}

void StageEditor::RenderEditorPanels()
{
    if (viewportFocusMode_) {
        RenderViewportFocusBar();
        return;
    }
    RenderEditorToolbar();
    RenderHierarchy();
    RenderInspector();
    RenderAssetPalette();
    if (showFlagsPanel_) {
        RenderFlagsPanel();
    }
    if (showWorkflowPanel_) {
        RenderWorkflowPanel();
    }
    if (showNoCodeEventPanel_) {
        RenderNoCodeEventPanel();
    }
    if (showWavePanel_) {
        RenderWavePanel();
    }
    RenderStageAnalysisPanel();
    RenderDiffPanel();
    RenderEditorHelpPanel();
}

void StageEditor::SetPlayTestMode(bool enabled)
{
    playTestMode_ = enabled;
    TimeManager::GetInstance()->SetTimeScale(enabled ? savedTimeScale_ : 0.0f);
}

// ══════════════════════════════════════════════════════
// 編集履歴と選択操作
// ══════════════════════════════════════════════════════

StageEditor::LevelSnapshot StageEditor::MakeSnapshot() const
{
    LevelSnapshot snap;
    snap.objects.reserve(objects_.size());
    for (const auto& entry : objects_) {
        snap.objects.push_back(entry.desc);
    }
    snap.triggers.reserve(triggers_.size());
    for (const auto& trg : triggers_) {
        snap.triggers.push_back(trg.GetDesc());
    }
    snap.checkpoints = checkpoints_;
    snap.playerSpawn = playerSpawn_;
    snap.enemySpawn = enemySpawn_;
    return snap;
}

void StageEditor::ApplySnapshot(const LevelSnapshot& snap)
{
    // UndoとRedoで配置実体を破棄する前に、GPUからの参照完了を保証する
    if (!objects_.empty() && modelCommon_ && modelCommon_->GetDxCommon()) {
        modelCommon_->GetDxCommon()->WaitForGpu();
    }
    // Open()のファイル読み込み抜き版。実体はdescから作り直す
    for (auto& entry : objects_) {
        UnregisterEnemyEntity(entry);
    }
    objects_.clear();
    for (const auto& desc : snap.objects) {
        ObjectEntry entry;
        entry.desc = desc;
        entry.authoredPosition = entry.desc.position;
        entry.runtimeActive = IsRuntimeActive(entry.desc) && entry.desc.activationDelay <= 0.0f;
        objects_.push_back(std::move(entry));
    }
    for (auto& entry : objects_) {
        RegenerateInstances(entry);
    }

    triggers_.clear();
    for (const auto& desc : snap.triggers) {
        TriggerVolume trg;
        trg.Init(desc);
        triggers_.push_back(std::move(trg));
    }
    checkpoints_ = snap.checkpoints;

    playerSpawn_ = snap.playerSpawn;
    enemySpawn_ = snap.enemySpawn;

    selKind_ = SelKind::None;
    selIndex_ = -1;
    viewportDragging_ = false;
}

void StageEditor::RecordUndoSnapshotNow()
{
    history_.Record(MakeSnapshot());
    dirty_ = true;
}

void StageEditor::BeginUndoCapture()
{
    if (!history_.IsCapturing()) {
        history_.Begin(MakeSnapshot());
    }
}

void StageEditor::MarkUndoDirty()
{
    history_.MarkChanged();
    dirty_ = true;
}

void StageEditor::CommitUndoCapture()
{
    history_.Commit();
}

void StageEditor::Undo()
{
    std::optional<LevelSnapshot> snapshot = history_.Undo(MakeSnapshot());
    if (!snapshot) {
        return;
    }
    ApplySnapshot(*snapshot);
    dirty_ = true;
    statusMessage_ = "元に戻しました";
    statusTimer_ = 1.5f;
}

void StageEditor::Redo()
{
    std::optional<LevelSnapshot> snapshot = history_.Redo(MakeSnapshot());
    if (!snapshot) {
        return;
    }
    ApplySnapshot(*snapshot);
    dirty_ = true;
    statusMessage_ = "やり直しました";
    statusTimer_ = 1.5f;
}

float StageEditor::SnapValue(float v) const
{
    if (!snapEnabled_ || snapStep_ <= 0.0f) {
        return v;
    }
    return std::round(v / snapStep_) * snapStep_;
}

std::vector<std::string> StageEditor::ValidateLevel() const
{
    std::vector<std::string> issues;
    std::unordered_set<std::string> names;
    for (const auto& entry : objects_) {
        const ObjectDesc& desc = entry.desc;
        if (desc.name.empty()) {
            issues.push_back("名前が空のオブジェクトがあります");
        } else if (!names.insert(desc.name).second) {
            issues.push_back("オブジェクト名が重複しています: " + desc.name);
        }
        if ((desc.kind == "prop" || desc.kind == "gimmick" || desc.kind == "terrain") && desc.model.empty()) {
            issues.push_back("モデル未設定: " + desc.name);
        }
        if (desc.scale.x <= 0.0f || desc.scale.y <= 0.0f || desc.scale.z <= 0.0f) {
            issues.push_back("スケールが0以下です: " + desc.name);
        }
        if (desc.type == "row" && (desc.count <= 0 || desc.step == 0.0f)) {
            issues.push_back("列配置の個数または間隔が無効です: " + desc.name);
        }
        if (!desc.parent.empty()) {
            const bool parentExists = std::any_of(objects_.begin(), objects_.end(), [&](const ObjectEntry& other) {
                return other.desc.name == desc.parent;
            });
            if (!parentExists) {
                issues.push_back("親が見つかりません: " + desc.name + " -> " + desc.parent);
            } else if (desc.parent == desc.name || IsDescendantOf(desc.parent, desc.name)) {
                issues.push_back("親子関係が循環しています: " + desc.name);
            }
        }
        if (desc.kind == "spawn_point" && desc.spawnType != "basic" && desc.spawnType != "knight") {
            issues.push_back("SpawnPointの敵種類が不正です: " + desc.name);
        }
        if (desc.kind == "patrol_point" && desc.patrolRoute.empty()) {
            issues.push_back("巡回ルート名が空です: " + desc.name);
        }
        if (desc.kind == "terrain" && !desc.solid) {
            issues.push_back("Terrainの当たり判定が無効です: " + desc.name);
        }
        if (!desc.activationFlag.empty()) {
            bool sourceExists = std::any_of(triggers_.begin(), triggers_.end(), [&](const TriggerVolume& trigger) {
                return trigger.GetDesc().flag == desc.activationFlag;
            });
            if (!sourceExists && desc.activationFlag.starts_with("condition_")) {
                const std::string conditionName = desc.activationFlag.substr(10);
                sourceExists = std::any_of(objects_.begin(), objects_.end(), [&](const ObjectEntry& condition) {
                    return condition.desc.kind == "event_condition" && condition.desc.name == conditionName;
                });
            }
            if (!sourceExists) {
                issues.push_back("イベント接続元が見つかりません: " + desc.name);
            }
        }
    }

    std::unordered_set<std::string> triggerNames;
    for (const auto& trigger : triggers_) {
        const TriggerDesc& desc = trigger.GetDesc();
        if (desc.name.empty() || !triggerNames.insert(desc.name).second) {
            issues.push_back("トリガー名が空または重複しています: " + desc.name);
        }
        if (desc.flag.empty() || desc.radius <= 0.0f) {
            issues.push_back("トリガー設定が不正です: " + desc.name);
        }
    }
    return issues;
}

void StageEditor::UpdateAutoSave(float realDt)
{
    if (!autoSaveEnabled_ || !dirty_ || levelPath_.empty()) {
        autoSaveElapsed_ = 0.0f;
        return;
    }
    autoSaveElapsed_ += realDt;
    if (autoSaveElapsed_ < kAutoSaveIntervalSeconds) {
        return;
    }
    recoveryPath_ = levelPath_ + ".autosave.json";
    SaveToPath(recoveryPath_);
    autoSaveElapsed_ = 0.0f;
    statusMessage_ = "自動保存しました: " + recoveryPath_;
    statusTimer_ = 2.0f;
}
#endif
