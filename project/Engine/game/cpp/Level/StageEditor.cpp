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

namespace {
constexpr float kNudgeStep = 0.25f; // スナップOFF時に矢印キーで動かす量（ワールド単位）
}

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
    graphPath_ = data.graphPath;
    flagGraphs_ = data.flagGraphs;
    activeEditor_ = this;
    ResetLevelLocalFlags(data);

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
    EnsureHudAnchors(); // 操作説明/武器選択パネルの位置マーカーが無ければ既定位置で追加する（旧レベルにも自動で足す）

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

    // 配置物とトリガーが揃ってから常駐グラフを起動する（グラフが配置物を名前で操作できるように）
    levelGraphs_.Start(data);

    selKind_ = SelKind::None;
    selIndex_ = -1;
    selectedObjectIndices_.clear();
#ifdef USE_IMGUI
    strncpy_s(graphPathBuffer_, graphPath_.c_str(), _TRUNCATE);
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
    statusTimer_ = StageEditor::kStatusShortSeconds;
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
    if (activeEditor_ == this) {
        activeEditor_ = nullptr;
    }
}

void StageEditor::ReleaseLevelResources(bool releaseExternalEntities)
{
    // 描画に使用した実体を解放する前にGPUからの参照完了を保証する
    if (!objects_.empty() && modelCommon_ && modelCommon_->GetDxCommon()) {
        modelCommon_->GetDxCommon()->WaitForGpu();
    }

    // グラフは配置物を名前で参照するため、実体より先に止める
    levelGraphs_.Stop();

    // レジストリ参照、描画実体の順に破棄する
    // （モデルの実体はModelManagerが所有・共有するのでここでは破棄しない。GetOrLoadModel()参照）
    for (auto& entry : objects_) {
        DestroyObjectRuntime(entry, false);
    }
    objects_.clear();
    modelCache_.clear();
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

void StageEditor::ResetLevelLocalFlags(const LevelData& data)
{
    GameFlags* flags = GameFlags::GetInstance();
    for (const TriggerDesc& trigger : data.triggers) {
        if (!trigger.flag.empty() && flags->HasFlag(trigger.flag)) {
            flags->SetFlag(trigger.flag, false);
        }
    }
    for (const ObjectDesc& desc : data.objects) {
        if (desc.name.empty()) {
            continue;
        }
        const char* prefix = desc.kind == "event_condition" ? "condition_"
            : desc.kind == "pickup"                         ? "pickup_"
            : desc.kind == "breakable"                      ? "broken_"
                                                            : nullptr;
        if (!prefix) {
            continue;
        }
        const std::string flag = std::string(prefix) + desc.name;
        if (flags->HasFlag(flag)) {
            flags->SetFlag(flag, false);
        }
    }
}

void StageEditor::FocusCameraOn(const Vector3& worldPosition)
{
    if (!camera_) {
        return;
    }
    constexpr float kFocusOffsetY = 3.0f; // ゲームカメラと同じく少し上から見下ろす
    Vector3& cameraPosition = camera_->GetTranslate();
    cameraPosition.x = worldPosition.x;
    cameraPosition.y = worldPosition.y + kFocusOffsetY;
}

void StageEditor::FocusCameraOnSelection()
{
    if (selKind_ == SelKind::Object && selIndex_ >= 0 && selIndex_ < static_cast<int>(objects_.size())) {
        FocusCameraOn(WorldPositionOf(objects_[selIndex_].desc));
        return;
    }
    if (selKind_ == SelKind::Trigger && selIndex_ >= 0 && selIndex_ < static_cast<int>(triggers_.size())) {
        FocusCameraOn(triggers_[selIndex_].GetDesc().position);
        return;
    }
    if (selKind_ == SelKind::External && selIndex_ >= 0 && selIndex_ < static_cast<int>(externalEntities_.size())
        && externalEntities_[selIndex_].position) {
        FocusCameraOn(*externalEntities_[selIndex_].position);
        return;
    }
    for (const ExternalEntityRef& ref : externalEntities_) {
        if (ref.name == "Player" && ref.position) {
            FocusCameraOn(*ref.position);
            return;
        }
    }
}

bool StageEditor::StartPlayTestAt(const Vector3& worldPosition)
{
    for (const ExternalEntityRef& ref : externalEntities_) {
        if (ref.name != "Player" || !ref.position) {
            continue;
        }
        *ref.position = worldPosition;
        FocusCameraOn(worldPosition);
        SetPlayTestMode(true);
        return true;
    }
    return false;
}

void StageEditor::SetPlayTestMode(bool enabled)
{
    playTestMode_ = enabled;
    TimeManager::GetInstance()->SetTimeScale(enabled ? savedTimeScale_ : 0.0f);
}

Vector3 StageEditor::ViewCenterOnGround() const
{
    Vector3 center = playerSpawn_;
    const Vector3 screen = viewport_.ViewCenter();
    MouseToGround(screen.x, screen.y, center);
    return center;
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
        statusTimer_ = StageEditor::kStatusLongSeconds;
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
    statusTimer_ = StageEditor::kStatusShortSeconds;
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
    data.graphPath = graphPath_;
    data.flagGraphs = flagGraphs_;
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
        viewport_.ClearImageRect();
        DiagnosticsDraw::SetImageViewport();
        return;
    }

    // F4で編集状態を維持したままパネル表示だけを切り替える
    // （F3はゲーム側の当たり判定オーバーレイ表示と統一するためF4にしている）
    if (ImGui::IsKeyPressed(ImGuiKey_F4, false)) {
        viewportFocusMode_ = !viewportFocusMode_;
    }
    RenderGameViewport();

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
    ImDrawList* overlay = DiagnosticsDraw::GetDrawList();
    const Vector3 topLeft = viewport_.ScreenToImage(0.0f, 0.0f);
    const Vector3 bottomRight = viewport_.ScreenToImage(WinApp::kClientWidth, WinApp::kClientHeight);
    overlay->PushClipRect({ topLeft.x, topLeft.y }, { bottomRight.x, bottomRight.y }, true);
    if (!playTestMode_) {
        DrawGridOverlay();
        DrawGizmos();
    }
    overlay->PopClipRect();
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
    if (playTestMode_) { return; }
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
    } else if (ImGui::IsKeyPressed(ImGuiKey_F)) {
        FocusCameraOnSelection();
    } else if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ClearSelection();
    } else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A)) {
        SelectAllObjects();
    }

    // 変形ツールの切替（右ボタンでカメラを飛ばしている間はWASDなので切り替えない）
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Right) && !io.KeyCtrl) {
        if (ImGui::IsKeyPressed(ImGuiKey_W)) {
            transformTool_ = TransformTool::Move;
        } else if (ImGui::IsKeyPressed(ImGuiKey_E)) {
            transformTool_ = TransformTool::Rotate;
        } else if (ImGui::IsKeyPressed(ImGuiKey_R)) {
            transformTool_ = TransformTool::Scale;
        }
    }

    // 矢印キーで選択物を微調整（Shift+上下は奥行き）。押しっぱなしはImGuiのリピートに任せる
    const float step = snapEnabled_ && snapStep_ > 0.0f ? snapStep_ : kNudgeStep;
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
        NudgeSelection(-step, 0.0f, 0.0f);
    } else if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
        NudgeSelection(step, 0.0f, 0.0f);
    } else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
        if (io.KeyShift) {
            NudgeSelection(0.0f, 0.0f, step);
        } else {
            NudgeSelection(0.0f, step, 0.0f);
        }
    } else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
        if (io.KeyShift) {
            NudgeSelection(0.0f, 0.0f, -step);
        } else {
            NudgeSelection(0.0f, -step, 0.0f);
        }
    }
}

void StageEditor::NudgeSelection(float dx, float dy, float dz)
{
    if (selKind_ == SelKind::Trigger) {
        if (selIndex_ < 0 || selIndex_ >= static_cast<int>(triggers_.size())) {
            return;
        }
        RecordUndoSnapshotNow();
        Vector3& position = triggers_[selIndex_].GetDesc().position;
        position = position + Vector3 { dx, dy, dz };
        return;
    }
    if (selKind_ == SelKind::External) {
        if (selIndex_ < 0 || selIndex_ >= static_cast<int>(externalEntities_.size()) || !externalEntities_[selIndex_].position) {
            return;
        }
        Vector3& position = *externalEntities_[selIndex_].position;
        position = position + Vector3 { dx, dy, dz };
        return;
    }
    if (selKind_ != SelKind::Object || selectedObjectIndices_.empty()) {
        return;
    }
    RecordUndoSnapshotNow();
    for (int index : selectedObjectIndices_) {
        if (index < 0 || index >= static_cast<int>(objects_.size())) {
            continue;
        }
        ObjectEntry& entry = objects_[index];
        entry.desc.position = entry.desc.position + Vector3 { dx, dy, dz };
        entry.authoredPosition = entry.desc.position;
        if (IsVisualKind(entry.desc.kind)) {
            RefreshTransforms(entry);
        }
    }
}

void StageEditor::DrawGridOverlay()
{
    if (!snapEnabled_ || !showGrid_ || snapStep_ <= 0.0f || !camera_) {
        return;
    }
    constexpr float kGridHalfWidth = 14.0f; // カメラ中心から左右に描く範囲
    constexpr float kGridMinY = -2.0f;
    constexpr float kGridMaxY = 14.0f;
    constexpr int kMaxLinesPerAxis = 80; // スナップ間隔が小さすぎる時に線で埋まらないよう上限を設ける
    constexpr ImU32 kGridColor = IM_COL32(255, 255, 255, 40);
    constexpr ImU32 kGridAxisColor = IM_COL32(255, 255, 255, 110);

    const Vector3& cameraPosition = camera_->GetTranslate();
    const float step = snapStep_;
    if ((kGridHalfWidth * 2.0f) / step > static_cast<float>(kMaxLinesPerAxis)) {
        return;
    }
    const float left = std::floor((cameraPosition.x - kGridHalfWidth) / step) * step;
    const float right = cameraPosition.x + kGridHalfWidth;
    for (float x = left; x <= right; x += step) {
        DiagnosticsDraw::DrawLine({ x, kGridMinY, 0.0f }, { x, kGridMaxY, 0.0f }, std::abs(x) < step * 0.5f ? kGridAxisColor : kGridColor);
    }
    const float bottom = std::floor(kGridMinY / step) * step;
    for (float y = bottom; y <= kGridMaxY; y += step) {
        DiagnosticsDraw::DrawLine({ left, y, 0.0f }, { right, y, 0.0f }, std::abs(y) < step * 0.5f ? kGridAxisColor : kGridColor);
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
    RenderViewportContextMenu();
    if (showFlagsPanel_) {
        RenderFlagsPanel();
    }
    if (showWorkflowPanel_) {
        RenderWorkflowPanel();
    }
    if (showNoCodeEventPanel_) {
        RenderNoCodeEventPanel();
    }
    if (showGraphPanel_) {
        RenderGraphPanel();
    }
    if (showWavePanel_) {
        RenderWavePanel();
    }
    RenderStageAnalysisPanel();
    RenderDiffPanel();
    RenderEditorHelpPanel();
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
    snap.graphPath = graphPath_;
    snap.flagGraphs = flagGraphs_;
    return snap;
}

#endif
