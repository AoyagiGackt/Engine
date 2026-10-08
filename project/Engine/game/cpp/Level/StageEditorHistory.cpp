/**
 * @file StageEditorHistory.cpp
 * @brief 編集履歴・検証・自動保存の管理
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

namespace {
constexpr float kNudgeStep = 0.25f; // スナップOFF時に矢印キーで動かす量（ワールド単位）
}

#ifdef USE_IMGUI
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
    graphPath_ = snap.graphPath;
    flagGraphs_ = snap.flagGraphs;
    strncpy_s(graphPathBuffer_, graphPath_.c_str(), _TRUNCATE);

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
    statusTimer_ = StageEditor::kStatusBriefSeconds;
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
    statusTimer_ = StageEditor::kStatusBriefSeconds;
}

float StageEditor::SnapValue(float v) const
{
    // Ctrlを押している間はスナップOFFでも一時的に揃える
    const bool snapActive = snapEnabled_ || ImGui::GetIO().KeyCtrl;
    if (!snapActive || snapStep_ <= 0.0f) {
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
        const ObjectKind& kind = ObjectKind::Of(desc.kind);
        if (kind.RequiresModel() && desc.model.empty()) {
            issues.push_back("モデル未設定: " + desc.name);
        }
        kind.Validate(desc, issues);
        if (desc.scale.x <= 0.0f || desc.scale.y <= 0.0f || desc.scale.z <= 0.0f) {
            issues.push_back("スケールが0以下です: " + desc.name);
        }
        if (desc.type == PlacementType::Row && (desc.count <= 0 || desc.step == 0.0f)) {
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
        if (!desc.activationFlag.empty()) {
            bool sourceExists = std::any_of(triggers_.begin(), triggers_.end(), [&](const TriggerVolume& trigger) {
                return trigger.GetDesc().flag == desc.activationFlag;
            });
            if (!sourceExists && desc.activationFlag.starts_with("condition_")) {
                const std::string conditionName = desc.activationFlag.substr(10);
                sourceExists = std::any_of(objects_.begin(), objects_.end(), [&](const ObjectEntry& condition) {
                    return condition.desc.kind == ObjectKindName::kEventCondition && condition.desc.name == conditionName;
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

    for (const auto& binding : flagGraphs_) {
        if (binding.flag.empty() || binding.graphPath.empty()) {
            issues.push_back("フラグ起動グラフのフラグ名またはパスが空です");
            continue;
        }
        std::error_code fileError;
        if (!std::filesystem::exists(binding.graphPath, fileError)) {
            issues.push_back("フラグ起動グラフのファイルが見つかりません: " + binding.graphPath);
        }
    }
    if (!graphPath_.empty()) {
        std::error_code fileError;
        if (!std::filesystem::exists(graphPath_, fileError)) {
            issues.push_back("常駐グラフのファイルが見つかりません: " + graphPath_);
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
    statusTimer_ = StageEditor::kStatusShortSeconds;
}
#endif
