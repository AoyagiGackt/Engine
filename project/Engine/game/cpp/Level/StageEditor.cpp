/**
 * @file StageEditor.cpp
 * @brief ステージ配置の実体管理と実行時編集ワークフローを実装するファイル
 */
#include "StageEditor.h"
#include "StageEditorSelectionService.h"
#include "StageEditorPrefabService.h"
#include "Camera.h"
#include "DebugDraw.h"
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
#include "TimeManager.h"
#include "WinApp.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
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

#ifdef USE_IMGUI
namespace {
// Windows のファイル選択ダイアログを開き、ユーザーが選んだファイルのパスを返す（キャンセル時は空文字列）
std::string OpenFileDialog(const char* filter, const char* initDir)
{
    char path[MAX_PATH] = { };
    OPENFILENAMEA ofn = { };
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrInitialDir = initDir;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameA(&ofn)) {
        return path;
    }
    return { };
}

// ダイアログが返す絶対パスを、JSON側の表記（"Resources/..."、区切りは/）に正規化する
std::string ToProjectRelativePath(const std::string& absPath)
{
    std::string p = absPath;
    for (char& ch : p) {
        if (ch == '\\') {
            ch = '/';
        }
    }
    size_t pos = p.find("Resources/");
    return (pos != std::string::npos) ? p.substr(pos) : p;
}

// アセットパレットに並べる配置物プリセット（モデル+テクスチャが対応済みの組み合わせのみ収録）
struct AssetPreset {
    const char* label;
    const char* model;
    const char* texture;
};
// Shift+ドラッグのZ移動  マウス垂直1pxあたりの移動量（カメラ距離1.0基準720p想定の見かけ等速係数）
constexpr float kZDragPerPixel = 0.0015f;

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
#endif

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
    return ParentWorldPositionOf(desc) + desc.position;
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

Model* StageEditor::GetOrLoadModel(const std::string& modelPath, const std::string& texPath)
{
    std::string key = modelPath + '|' + texPath;
    auto it = modelCache_.find(key);
    if (it != modelCache_.end()) {
        return it->second;
    }

    auto model = std::make_unique<Model>();
    model->Initialize(modelCommon_, modelPath, texPath);
    Model* ptr = model.get();
    modelStorage_.push_back(std::move(model));
    modelCache_[key] = ptr;
    return ptr;
}

void StageEditor::RegenerateInstances(ObjectEntry& entry)
{
    // 構造変更で既存実体を作り直す場合だけGPUの参照完了を待つ
    if ((!entry.instances.empty() || entry.knight || entry.enemy)
        && modelCommon_ && modelCommon_->GetDxCommon()) {
        modelCommon_->GetDxCommon()->WaitForGpu();
    }
    entry.instances.clear();
    const ObjectDesc& desc = entry.desc;
    if (!desc.enabled) {
        UnregisterEnemyEntity(entry);
        entry.knight.reset();
        entry.enemy.reset();
        return;
    }

    const bool wantsKnight = desc.kind == "enemy_knight"
        || (desc.kind == "spawn_point" && desc.spawnType == "knight");
    const bool wantsBasic = desc.kind == "enemy_basic"
        || (desc.kind == "spawn_point" && desc.spawnType != "knight");
    if (!wantsKnight) {
        entry.knight.reset();
    }
    if (!wantsBasic) {
        UnregisterEnemyEntity(entry);
        entry.enemy.reset();
    }

    if (desc.kind == "enemy_knight" || (desc.kind == "spawn_point" && desc.spawnType == "knight" && IsRuntimeActive(desc))) {
        if (!entry.knight) {
            entry.knight = std::make_unique<KnightEnemy>();
            entry.knight->Initialize(modelCommon_, WorldPositionOf(desc));
        }
        return;
    }
    if (desc.kind == "enemy_basic" || (desc.kind == "spawn_point" && desc.spawnType != "knight" && IsRuntimeActive(desc))) {
        if (!entry.enemy) {
            entry.enemy = std::make_unique<EnemyEntity>();
            entry.enemy->Initialize(modelCommon_, WorldPositionOf(desc));
            entry.enemy->SetId(desc.name);
            EnemyRegistry::GetInstance()->Register(desc.name, entry.enemy.get());
        }
        return;
    }

    if (desc.kind == "spawn_point" || desc.kind == "camera_point" || desc.kind == "patrol_point") {
        return;
    }

    if (desc.model.empty()) {
        return;
    }

    Model* model = GetOrLoadModel(desc.model, desc.texture);

    auto spawnOne = [&](const Vector3& pos) {
        auto obj = std::make_unique<Object3d>();
        obj->Initialize(modelCommon_);
        obj->SetModel(model);
        obj->SetPosition(pos);
        obj->SetRotation(desc.rotation);
        obj->SetScale(desc.scale);
        obj->SetEnableLighting(desc.lighting);
        obj->Update();
        entry.instances.push_back(std::move(obj));
    };

    // 位置はRefreshTransforms()が毎フレーム上書きするため、ここでは個数分の生成だけが本質
    int instanceCount = (desc.type == "row") ? (std::max)(1, desc.count) : 1;
    for (int i = 0; i < instanceCount; ++i) {
        spawnOne(desc.position);
    }
    RefreshTransforms(entry);
}

void StageEditor::AppendGeneratedContent(StageEditorGeneratedContent content)
{
    for (TriggerDesc& desc : content.triggers) {
        TriggerVolume trigger;
        trigger.Init(desc);
        triggers_.push_back(std::move(trigger));
    }
    for (ObjectDesc& desc : content.objects) {
        ObjectEntry entry;
        entry.desc = std::move(desc);
        entry.authoredPosition = entry.desc.position;
        objects_.push_back(std::move(entry));
        RegenerateInstances(objects_.back());
    }
}

void StageEditor::RefreshTransforms(ObjectEntry& entry)
{
    const ObjectDesc& desc = entry.desc;
    Vector3 basePos = WorldPositionOf(desc); // 親がいる場合は親のワールド位置＋ローカルオフセット

    for (int i = 0; i < static_cast<int>(entry.instances.size()); ++i) {
        Vector3 pos = basePos;
        if (desc.type == "row") {
            float offset = desc.step * static_cast<float>(i);
            if (desc.axis == 'y') {
                pos.y += offset;
            } else if (desc.axis == 'z') {
                pos.z += offset;
            } else {
                pos.x += offset;
            }
        }
        entry.instances[i]->SetPosition(pos);
        entry.instances[i]->SetRotation(desc.rotation);
        entry.instances[i]->SetScale(desc.scale);
        entry.instances[i]->SetEnableLighting(desc.lighting);
    }
}

void StageEditor::AddPropAtScreenCenter(const std::string& model, const std::string& texture)
{
    // 見えている画面の中央（z=0平面上）に置くカメラをどこへ動かしていても手元に出る
    Vector3 center = playerSpawn_;
    MouseToGround(WinApp::kClientWidth * 0.5f, WinApp::kClientHeight * 0.5f, center);

#ifdef USE_IMGUI
    RecordUndoSnapshotNow();
    center.x = SnapValue(center.x);
    center.y = SnapValue(center.y);
#endif

    ObjectEntry entry;
    entry.desc.name = "obj_" + std::to_string(nextSerial_++);
    entry.desc.kind = "prop";
    entry.desc.type = "static";
    entry.desc.position = center;
    entry.desc.model = model;
    entry.desc.texture = texture;
    objects_.push_back(std::move(entry));
    RegenerateInstances(objects_.back());
    selKind_ = SelKind::Object;
    selIndex_ = static_cast<int>(objects_.size()) - 1;
}

void StageEditor::RegisterExternalEntity(const std::string& name, Vector3* position)
{
    for (auto& ref : externalEntities_) {
        if (ref.name == name) {
            ref.position = position; // 同名なら上書き（Scene再初期化等での再登録に備える）
            return;
        }
    }
    externalEntities_.push_back({ name, position });
}

std::vector<engine::AABB> StageEditor::GetSolidColliders() const
{
    std::vector<engine::AABB> result;
    for (const auto& entry : objects_) {
        if (!entry.desc.solid || !entry.runtimeActive) {
            continue;
        }
        const ObjectDesc& desc = entry.desc;
        Vector3 basePos = WorldPositionOf(desc);
        // Terrainは描画メッシュの各三角形をAABBへ変換し、表示形状の変更と同期する
        if (desc.kind == "terrain" && desc.meshCollider) {
            const std::string key = desc.model + '|' + desc.texture;
            auto modelIt = modelCache_.find(key);
            if (modelIt != modelCache_.end()) {
                const auto& vertices = modelIt->second->GetVertices();
                const auto& indices = modelIt->second->GetIndices();
                auto transformVertex = [&](const Vector4& vertex) {
                    Vector3 value = { vertex.x * desc.scale.x, vertex.y * desc.scale.y, vertex.z * desc.scale.z };
                    const float cx = std::cos(desc.rotation.x), sx = std::sin(desc.rotation.x);
                    const float cy = std::cos(desc.rotation.y), sy = std::sin(desc.rotation.y);
                    const float cz = std::cos(desc.rotation.z), sz = std::sin(desc.rotation.z);
                    value = { value.x, value.y * cx - value.z * sx, value.y * sx + value.z * cx };
                    value = { value.x * cy + value.z * sy, value.y, -value.x * sy + value.z * cy };
                    value = { value.x * cz - value.y * sz, value.x * sz + value.y * cz, value.z };
                    return value + basePos;
                };
                for (size_t i = 0; i + 2 < indices.size(); i += 3) {
                    const Vector3 a = transformVertex(vertices[indices[i]].position);
                    const Vector3 b = transformVertex(vertices[indices[i + 1]].position);
                    const Vector3 c = transformVertex(vertices[indices[i + 2]].position);
                    constexpr float kColliderThickness = 0.03f;
                    result.push_back({
                        { (std::min)({ a.x, b.x, c.x }) - kColliderThickness,
                            (std::min)({ a.y, b.y, c.y }) - kColliderThickness,
                            (std::min)({ a.z, b.z, c.z }) - kColliderThickness },
                        { (std::max)({ a.x, b.x, c.x }) + kColliderThickness,
                            (std::max)({ a.y, b.y, c.y }) + kColliderThickness,
                            (std::max)({ a.z, b.z, c.z }) + kColliderThickness }
                    });
                }
            }
            continue;
        }
        Vector3 half = { 0.5f * desc.scale.x, 0.5f * desc.scale.y, 0.5f * desc.scale.z };

        int instanceCount = static_cast<int>(entry.instances.size());
        for (int i = 0; i < instanceCount; ++i) {
            Vector3 pos = basePos;
            if (desc.type == "row") {
                float offset = desc.step * static_cast<float>(i);
                if (desc.axis == 'y') {
                    pos.y += offset;
                } else if (desc.axis == 'z') {
                    pos.z += offset;
                } else {
                    pos.x += offset;
                }
            }
            result.push_back({ { pos.x - half.x, pos.y - half.y, pos.z - half.z },
                { pos.x + half.x, pos.y + half.y, pos.z + half.z } });
        }
    }
    return result;
}

bool StageEditor::IsRuntimeActive(const ObjectDesc& desc) const
{
    if (!desc.enabled) {
        return false;
    }
    if (desc.activationFlag.empty()) {
        return true;
    }
    return GameFlags::GetInstance()->GetFlag(desc.activationFlag) == desc.activeWhenFlag;
}

// ══════════════════════════════════════════════════════
// ランタイム更新と描画
// ══════════════════════════════════════════════════════

void StageEditor::UpdateObjects(ParticleManager* pm, const Vector3& playerPos)
{
    // 描画済み状態を更新開始時に戻し、BaseScene側の二重描画判定をフレーム単位に保つ
    objectsDrawnThisFrame_ = false;
    constexpr float kRuntimeDeltaSeconds = 1.0f / 60.0f;

    // 条件、接続先の有効化、実体の順で更新して同じフレーム内に結果を反映する
    EvaluateEventConditions(kRuntimeDeltaSeconds);
    UpdateRuntimeActivation(kRuntimeDeltaSeconds);
    for (auto& entry : objects_) {
        UpdateRuntimeEntry(entry, pm, playerPos, kRuntimeDeltaSeconds);
    }
}

void StageEditor::EvaluateEventConditions(float dt)
{
    // ノーコード条件を先に評価し、接続先が参照するゲームフラグへ反映する
    for (auto& condition : objects_) {
        if (!condition.desc.enabled || condition.desc.kind != "event_condition") {
            continue;
        }
        bool met = condition.desc.conditionType == "manual"
            ? GameFlags::GetInstance()->GetFlag("condition_" + condition.desc.name)
            : false;
        if (condition.desc.conditionType == "timer") {
            condition.runtimeTimer += dt;
            met = condition.runtimeTimer >= condition.desc.conditionSeconds;
        } else if (condition.desc.conditionType == "enemy_group_defeated") {
            bool foundEnemy = false;
            met = true;
            for (const auto& enemyEntry : objects_) {
                if (enemyEntry.desc.enemyGroup != condition.desc.enemyGroup) {
                    continue;
                }
                if (enemyEntry.knight) {
                    foundEnemy = true;
                    met &= !enemyEntry.knight->IsAlive();
                } else if (enemyEntry.enemy) {
                    foundEnemy = true;
                    met &= enemyEntry.enemy->IsDefeated();
                } else if (enemyEntry.desc.kind == "spawn_point") {
                    foundEnemy = true;
                    met = false;
                }
            }
            met &= foundEnemy;
        }
        GameFlags::GetInstance()->SetFlag("condition_" + condition.desc.name, met);
    }
}

void StageEditor::UpdateRuntimeActivation(float dt)
{
    // 対象ごとの遅延を評価する。有効化は成立後に待ち、無効化は成立後も遅延中だけ維持する
    for (auto& entry : objects_) {
        const ObjectDesc& desc = entry.desc;
        if (!desc.enabled) {
            entry.runtimeActive = false;
            continue;
        }
        if (desc.activationFlag.empty()) {
            entry.runtimeActive = true;
            if (desc.kind == "gimmick") {
                entry.runtimeTimer += dt;
            }
            continue;
        }
        const bool flagValue = GameFlags::GetInstance()->GetFlag(desc.activationFlag);
        if (flagValue) {
            entry.runtimeTimer += dt;
        } else {
            entry.runtimeTimer = 0.0f;
        }
        entry.runtimeActive = desc.activeWhenFlag
            ? flagValue && entry.runtimeTimer >= desc.activationDelay
            : !flagValue || entry.runtimeTimer < desc.activationDelay;
        if (desc.kind == "camera_point" && desc.activeWhenFlag && desc.cameraHoldSeconds > 0.0f
            && entry.runtimeTimer > desc.activationDelay + desc.cameraHoldSeconds) {
            entry.runtimeActive = false;
        }
    }
}

void StageEditor::UpdateRuntimeEntry(ObjectEntry& entry, ParticleManager* pm,
    const Vector3& playerPos, float dt)
{
    if (!entry.runtimeActive) {
        return;
    }
    if (entry.desc.kind == "spawn_point" && !entry.knight && !entry.enemy) {
        RegenerateInstances(entry);
        return;
    }
    if (entry.desc.kind == "camera_point") {
        // 編集中は演出用カメラポイントで自由カメラを上書きしない
        if (visible_ && !playTestMode_) {
            return;
        }
        if (camera_) {
            const Vector3 targetPosition = WorldPositionOf(entry.desc);
            Vector3& cameraPosition = camera_->GetTranslate();
            const float blend = entry.desc.cameraBlendSeconds <= 0.0f
                ? 1.0f
                : (std::min)(1.0f, dt / entry.desc.cameraBlendSeconds);
            cameraPosition.x += (targetPosition.x - cameraPosition.x) * blend;
            cameraPosition.y += (targetPosition.y - cameraPosition.y) * blend;
            cameraPosition.z += (targetPosition.z - cameraPosition.z) * blend;
            camera_->SetRotate(entry.desc.rotation);
        }
        return;
    }
    if (entry.desc.kind == "patrol_point" || !IsRuntimeActive(entry.desc)) {
        return;
    }
    if (entry.knight || entry.enemy) {
        UpdateEnemyEntry(entry, pm, playerPos);
        return;
    }

    // 一時的なギミック変形だけを描画実体へ渡し、保存対象の編集値は維持する
    const Vector3 authoredPosition = entry.desc.position;
    const Vector3 authoredRotation = entry.desc.rotation;
    if (entry.desc.kind == "gimmick") {
        const float phase = entry.runtimeTimer * entry.desc.motionSpeed;
        if (entry.desc.gimmickMotion == "move_y") {
            entry.desc.position.y += std::sin(phase) * entry.desc.motionAmount;
        } else if (entry.desc.gimmickMotion == "fall") {
            entry.desc.position.y -= (std::min)(entry.desc.motionAmount, phase * entry.desc.motionAmount);
        } else if (entry.desc.gimmickMotion == "rotate_y") {
            entry.desc.rotation.y += phase;
        }
    }
    RefreshTransforms(entry);
    entry.desc.position = authoredPosition;
    entry.desc.rotation = authoredRotation;
    for (auto& obj : entry.instances) {
        obj->Update();
    }
}

void StageEditor::UpdateEnemyEntry(ObjectEntry& entry, ParticleManager* pm, const Vector3& playerPos)
{
    Vector3 worldPos = WorldPositionOf(entry.desc);
    if (!ShouldPauseGame() && UpdatePatrol(entry)) {
        return;
    }
    if (entry.knight) {
        if (ShouldPauseGame()) {
            // 編集中はAI/重力を進めず、desc.positionへドラッグされた位置だけ反映する
            entry.knight->GetPositionRef() = worldPos;
            entry.knight->RefreshVisualTransforms();
        } else {
            entry.knight->Update(pm, playerPos);
            // Inspector表示・親子追従の基準にするため現在地を書き戻す（JSON保存はしない）
            entry.desc.position = entry.knight->GetPosition();
        }
    } else if (entry.enemy) {
        if (ShouldPauseGame()) {
            entry.enemy->GetPositionRef() = worldPos;
            entry.enemy->RefreshVisualTransforms();
        } else {
            entry.enemy->Update();
            entry.desc.position = entry.enemy->GetPosition();
        }
    }
}

bool StageEditor::UpdatePatrol(ObjectEntry& entry)
{
    if (entry.desc.patrolRoute.empty()) {
        return false;
    }
    std::vector<const ObjectDesc*> points;
    for (const auto& candidate : objects_) {
        if (candidate.desc.enabled && candidate.desc.kind == "patrol_point"
            && candidate.desc.patrolRoute == entry.desc.patrolRoute) {
            points.push_back(&candidate.desc);
        }
    }
    if (points.empty()) {
        return false;
    }
    std::sort(points.begin(), points.end(), [](const ObjectDesc* lhs, const ObjectDesc* rhs) {
        return lhs->routeOrder < rhs->routeOrder;
    });
    entry.patrolTargetIndex %= static_cast<int>(points.size());
    const Vector3 target = WorldPositionOf(*points[entry.patrolTargetIndex]);
    Vector3* position = entry.knight ? &entry.knight->GetPositionRef() : &entry.enemy->GetPositionRef();
    const float dx = target.x - position->x;
    const float dy = target.y - position->y;
    const float dz = target.z - position->z;
    const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    constexpr float kWaypointArrivalDistance = 0.12f;
    if (distance <= kWaypointArrivalDistance) {
        entry.patrolTargetIndex = (entry.patrolTargetIndex + 1) % static_cast<int>(points.size());
    } else {
        const float step = (std::max)(0.0f, entry.desc.patrolSpeed) / 60.0f;
        const float ratio = (std::min)(1.0f, step / distance);
        position->x += dx * ratio;
        position->y += dy * ratio;
        position->z += dz * ratio;
    }
    entry.desc.position = *position;
    if (entry.knight) {
        entry.knight->RefreshVisualTransforms();
    } else {
        entry.enemy->RefreshVisualTransforms();
    }
    return true;
}

void StageEditor::DrawObjects()
{
    // BaseScene::Render()がシーンのDraw()の直後に自動で呼ぶため自己完結させる
    // 呼び出し側が既にモデル用PSO/ルートシグネチャを設定済みである前提を置かず、ここで自分で設定する
    // （その代わり配置物は毎フレーム最後に上乗せ描画される＝シャドウ/ポストエフェクトの対象外になる）
    // Scene::Draw()内でHUDより前に自分で呼んだ場合は、その旨をフラグで記録する
    // （BaseScene::Render()側の自動呼び出しを止め、配置ブロックがHUDテキストの上に重なるのを防ぐ）
    objectsDrawnThisFrame_ = true;

    if (!modelCommon_ || objects_.empty()) {
        return;
    }
    modelCommon_->CommonDrawSettings();
    // ルートシグネチャを設定し直すと全ルート引数が未設定へ戻るため、
    // 平行光源、ポイントライト、シャドウマップを配置物の描画前に再設定する
    Object3d::RebindCommonLighting(modelCommon_->GetDxCommon()->GetCommandList());

    for (auto& entry : objects_) {
        if (!entry.runtimeActive) {
            continue;
        }
        if (entry.desc.kind == "gimmick" && entry.desc.gimmickMotion == "blink"
            && std::sin(entry.runtimeTimer * entry.desc.motionSpeed) < 0.0f) {
            continue;
        }
        for (auto& obj : entry.instances) {
            obj->Draw();
        }
        if (entry.knight) {
            entry.knight->Draw();
        }
        if (entry.enemy) {
            entry.enemy->Draw();
        }
    }
}

std::vector<KnightEnemy*> StageEditor::GetKnights()
{
    std::vector<KnightEnemy*> result;
    for (auto& entry : objects_) {
        if (entry.knight && entry.runtimeActive) {
            result.push_back(entry.knight.get());
        }
    }
    return result;
}

void StageEditor::Update(Input* input, const Vector3& playerPos)
{
    // トリガー判定はエディタの表示状態に関係なく常に行う（普段のプレイ中でも成立させるため）
    for (auto& trg : triggers_) {
        trg.Update(playerPos);
    }

#ifdef USE_IMGUI
    UpdateEditorVisibility(input);
    if (!visible_) {
        return;
    }

    // F3で編集状態を維持したままパネル表示だけを切り替える
    if (ImGui::IsKeyPressed(ImGuiKey_F3, false)) {
        viewportFocusMode_ = !viewportFocusMode_;
    }

    const float realDt = ImGui::GetIO().DeltaTime;
    UpdateAutoSave(realDt);
    if (statusTimer_ > 0.0f) {
        statusTimer_ -= realDt;
    }

    UpdateFreeCamera(input, realDt);

    if (camera_) {
        DebugDraw::SetCamera(camera_->GetViewProjectionMatrix(),
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
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)) Undo();
    else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y)) Redo();
    else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) Save();
    else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D)) DuplicateSelected();
    else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C)) CopySelected();
    else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V)) PasteClipboard();
    else if (ImGui::IsKeyPressed(ImGuiKey_Delete)) DeleteSelected();
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
    if (showFlagsPanel_) RenderFlagsPanel();
    if (showWorkflowPanel_) RenderWorkflowPanel();
    if (showNoCodeEventPanel_) RenderNoCodeEventPanel();
    if (showWavePanel_) RenderWavePanel();
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

void StageEditor::DeleteSelected()
{
    StageEditorSelectionService::DeleteSelected(*this);
}

void StageEditor::DuplicateSelected()
{
    StageEditorSelectionService::DuplicateSelected(*this);
}

void StageEditor::CopySelected()
{
    StageEditorSelectionService::CopySelected(*this);
}

void StageEditor::PasteClipboard()
{
    StageEditorSelectionService::PasteClipboard(*this);
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

// ══════════════════════════════════════════════════════
// エディタパネル
// ══════════════════════════════════════════════════════

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
    constexpr float kToolbarHeight = 42.0f;
    constexpr float kPanelWidth = 280.0f;
    const float hierarchyHeight = (static_cast<float>(WinApp::kClientHeight) - kToolbarHeight) * 0.62f;
    ImGui::SetNextWindowPos(ImVec2(0.0f, kToolbarHeight), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(kPanelWidth, hierarchyHeight), ImGuiCond_Always);
    ImGui::Begin("ステージエディタ", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);

    ImGui::TextDisabled("F2: 表示/非表示    F3: 画面優先    WASD/QE: カメラ移動");
    if (ImGui::Button("ゲーム画面を広く表示 (F3)", ImVec2(-1.0f, 0.0f))) {
        viewportFocusMode_ = true;
    }
    if (ImGui::CollapsingHeader("制作ガイド", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool hasGround = false;
        bool hasEnemy = false;
        bool hasEventConnection = false;
        for (const auto& entry : objects_) {
            hasGround |= entry.desc.solid || entry.desc.kind == "terrain";
            hasEnemy |= entry.desc.kind == "spawn_point" || entry.desc.kind == "enemy_basic"
                || entry.desc.kind == "enemy_knight";
            hasEventConnection |= !entry.desc.activationFlag.empty();
        }
        const int completed = static_cast<int>(hasGround) + static_cast<int>(hasEnemy)
            + static_cast<int>(!triggers_.empty()) + static_cast<int>(hasEventConnection);
        ImGui::ProgressBar(static_cast<float>(completed) / 4.0f, ImVec2(-1.0f, 0.0f));
        ImGui::TextWrapped("上から順に進めると、配置からゲーム進行までコードを書かずに作成できます。");
        ImGui::BulletText("%s 1 地形を置き、詳細設定で当たり判定を有効にする",
            hasGround ? "[完了]" : "[次]  ");
        ImGui::BulletText("%s 2 敵またはWaveを配置する",
            hasEnemy ? "[完了]" : "[未]  ");
        ImGui::BulletText("%s 3 プレイヤーが入るトリガーを配置する",
            !triggers_.empty() ? "[完了]" : "[未]  ");
        ImGui::BulletText("%s 4 イベントで条件と動作対象を接続する",
            hasEventConnection ? "[完了]" : "[未]  ");
        if (ImGui::SmallButton("Waveを作る")) {
            showWavePanel_ = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("イベントを作る")) {
            showNoCodeEventPanel_ = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("詳しい説明")) {
            showEditorHelp_ = true;
        }
        ImGui::TextDisabled("最後に上部のテストで確認し、制作パネルから検証して保存する");
    }
    if (ImGui::CollapsingHeader("使い方")) {
        ImGui::BulletText("WASD: カメラ移動    Q/E・マウスホイール: 奥/手前へズーム");
        ImGui::BulletText("画面上のオブジェクトを左クリック: 選択");
        ImGui::BulletText("そのまま左ドラッグ: つかんで移動（Shift+ドラッグ: 奥行き(Z)移動）");
        ImGui::BulletText("Ctrl+Z: 元に戻す  Ctrl+Y: やり直す  Ctrl+S: 保存");
        ImGui::BulletText("Ctrl+D・複製ボタン: 選択中の物を複製    Deleteキー: 削除");
        ImGui::BulletText("Ctrl+C / Ctrl+V: 選択中の配置物をコピー・貼り付け");
        ImGui::BulletText("Ctrlを押しながら選択: 複数選択して一括移動・回転・拡縮");
        ImGui::BulletText("スナップをONにすると、移動・配置・複製の座標が指定間隔の倍数に揃う");
        ImGui::BulletText("[+]ボタン: 配置物/敵(ナイト・汎用エネミー)を選んで画面中央に新規追加");
        ImGui::BulletText("敵は実際にHPを持って湧く本物の敵配置数・種類は自由に増減できる");
        ImGui::BulletText("左下のアセットパレット: モード切替で新規配置/選択物への差し替えを選べる");
        ImGui::BulletText("右の詳細設定で数値・モデル・親子関係を編集");
        ImGui::BulletText("親を設定すると、親を動かしたとき子も一緒に動く");
        ImGui::BulletText("トリガー(水色の球): プレイヤーが入るとフラグON");
        ImGui::BulletText("　フラグはノードエディタ(F1)のGetFlagで参照できる");
        ImGui::BulletText("エンティティ(マゼンタの十字): Player/Enemy等も同様に選択・ドラッグ移動できる");
        ImGui::BulletText("　ただし削除不可・JSONに保存されない（位置はシーン起動時の初期値に戻る）");
        ImGui::BulletText("保存ボタンでJSONへ書き出しゲーム本編に即反映");
    }
    char pathBuf[256];
    strncpy_s(pathBuf, levelPath_.c_str(), _TRUNCATE);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputText("##path", pathBuf, sizeof(pathBuf))) {
        levelPath_ = pathBuf;
    }
    if (ImGui::Button("開く", ImVec2(80, 0))) {
        // 未保存の編集がある時は黙って破棄せず、確認モーダルを挟む
        if (dirty_) {
            ImGui::OpenPopup("開くの確認");
        } else {
            Open(levelPath_, modelCommon_, camera_);
        }
    }
    if (EditorUI::ConfirmModal("開くの確認",
            "未保存の変更があります。\n変更を破棄して読み込み直しますか？",
            "破棄して開く")
        == EditorUI::ConfirmResult::Ok) {
        Open(levelPath_, modelCommon_, camera_);
    }
    ImGui::SameLine();
    if (ImGui::Button("保存", ImVec2(80, 0))) {
        Save();
    }
    if (dirty_) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "未保存");
    }

    ImGui::BeginDisabled(!history_.CanUndo());
    if (ImGui::Button("元に戻す", ImVec2(80, 0))) {
        Undo();
    }
    ImGui::EndDisabled();
    if (ImGui::Button("選択条件をテスト発火")) {
        const int sourceIndex = eventConnection_.SourceIndex();
        if (sourceIndex >= 0 && sourceIndex < static_cast<int>(triggers_.size())) {
            GameFlags::GetInstance()->SetFlag(triggers_[sourceIndex].GetDesc().flag, true);
        } else {
            const int conditionIndex = sourceIndex - static_cast<int>(triggers_.size());
            if (conditionIndex >= 0 && conditionIndex < static_cast<int>(objects_.size())) {
                GameFlags::GetInstance()->SetFlag("condition_" + objects_[conditionIndex].desc.name, true);
            }
        }
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!history_.CanRedo());
    if (ImGui::Button("やり直す", ImVec2(80, 0))) {
        Redo();
    }
    ImGui::EndDisabled();

    ImGui::Checkbox("スナップ", &snapEnabled_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70.0f);
    ImGui::DragFloat("##snapStep", &snapStep_, 0.1f, 0.1f, 10.0f, "%.1f");
    EditorUI::HelpMarker("ドラッグ移動・新規配置・複製の座標を、この間隔の倍数に揃えます");

    if (statusTimer_ > 0.0f) {
        ImGui::TextDisabled("%s", statusMessage_.c_str());
    }

    ImGui::Separator();

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##hierarchySearch", "名前・種類・モデルを検索", hierarchySearch_, sizeof(hierarchySearch_));

    std::string searchText = hierarchySearch_;
    std::transform(searchText.begin(), searchText.end(), searchText.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    auto matchesSearch = [&](const std::string& text) {
        if (searchText.empty()) {
            return true;
        }
        std::string target = text;
        std::transform(target.begin(), target.end(), target.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return target.find(searchText) != std::string::npos;
    };

    char objHeader[48];
    snprintf(objHeader, sizeof(objHeader), "オブジェクト (%d)", static_cast<int>(objects_.size()));
    bool objOpen = ImGui::TreeNodeEx(objHeader, ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::SameLine();
    if (ImGui::SmallButton("+##addObj")) {
        ImGui::OpenPopup("AddObjectPopup");
    }
    if (ImGui::BeginPopup("AddObjectPopup")) {
        // 見えている画面の中央（z=0平面上）に置くカメラをどこへ動かしていても手元に出る
        Vector3 center = playerSpawn_;
        MouseToGround(WinApp::kClientWidth * 0.5f, WinApp::kClientHeight * 0.5f, center);

        auto addEntry = [&](const std::string& namePrefix, const std::string& kind) {
            RecordUndoSnapshotNow();
            ObjectEntry entry;
            entry.desc.name = namePrefix + "_" + std::to_string(nextSerial_++);
            entry.desc.kind = kind;
            entry.desc.position = center;
            entry.desc.position.x = SnapValue(entry.desc.position.x);
            entry.desc.position.y = SnapValue(entry.desc.position.y);
            objects_.push_back(std::move(entry));
            RegenerateInstances(objects_.back());
            selKind_ = SelKind::Object;
            selIndex_ = static_cast<int>(objects_.size()) - 1;
        };

        if (ImGui::MenuItem("配置物（ブロック）")) {
            AddPropAtScreenCenter("Resources/block/block.obj", "Resources/block/block.png");
        }
        if (ImGui::MenuItem("敵：ナイト")) {
            addEntry("knight", "enemy_knight");
        }
        if (ImGui::MenuItem("敵：汎用エネミー")) {
            addEntry("enemy", "enemy_basic");
        }
        if (ImGui::MenuItem("SpawnPoint")) {
            addEntry("spawn", "spawn_point");
            objects_.back().desc.activationFlag = objects_.back().desc.name + "_active";
        }
        if (ImGui::MenuItem("ギミック")) {
            addEntry("gimmick", "gimmick");
            objects_.back().desc.activationFlag = objects_.back().desc.name + "_active";
            objects_.back().desc.model = "Resources/block/block.obj";
            objects_.back().desc.texture = "Resources/block/block.png";
            RegenerateInstances(objects_.back());
        }
        if (ImGui::MenuItem("カメラポイント")) {
            addEntry("camera", "camera_point");
            objects_.back().desc.activationFlag = objects_.back().desc.name + "_active";
        }
        if (ImGui::MenuItem("巡回Waypoint")) {
            addEntry("waypoint", "patrol_point");
        }
        if (ImGui::MenuItem("イベント条件")) {
            addEntry("condition", "event_condition");
        }
        if (ImGui::MenuItem("Terrain")) {
            addEntry("terrain", "terrain");
            ObjectDesc& terrain = objects_.back().desc;
            terrain.model = "Resources/block/block.obj";
            terrain.texture = "Resources/block/block.png";
            terrain.solid = true;
            terrain.meshCollider = true;
            RegenerateInstances(objects_.back());
        }
        ImGui::EndPopup();
    }
    if (objOpen) {
        if (!searchText.empty()) {
            for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
                const ObjectDesc& d = objects_[i].desc;
                if (!matchesSearch(d.name) && !matchesSearch(d.kind) && !matchesSearch(d.model)) {
                    continue;
                }
                bool selected = std::find(selectedObjectIndices_.begin(), selectedObjectIndices_.end(), i)
                    != selectedObjectIndices_.end();
                std::string label = d.name + "  " + d.kind + "##searchObj" + std::to_string(i);
                if (ImGui::Selectable(label.c_str(), selected)) {
                    selKind_ = SelKind::Object;
                    selIndex_ = i;
                    if (!ImGui::GetIO().KeyCtrl) {
                        selectedObjectIndices_.clear();
                    }
                    auto selectedIt = std::find(selectedObjectIndices_.begin(), selectedObjectIndices_.end(), i);
                    if (selectedIt == selectedObjectIndices_.end()) {
                        selectedObjectIndices_.push_back(i);
                    } else if (ImGui::GetIO().KeyCtrl) {
                        selectedObjectIndices_.erase(selectedIt);
                    }
                }
            }
        } else {
            auto parentExists = [&](const std::string& parentName) {
                if (parentName.empty()) {
                    return false;
                }
                for (const auto& e : objects_) {
                    if (e.desc.name == parentName) {
                        return true;
                    }
                }
                return false;
            };
            for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
                if (!parentExists(objects_[i].desc.parent)) {
                    DrawHierarchyEntry(i, 0);
                }
            }
        }
        ImGui::TreePop();
    }

    if (!externalEntities_.empty()) {
        char entHeader[48];
        snprintf(entHeader, sizeof(entHeader), "エンティティ (%d)", static_cast<int>(externalEntities_.size()));
        bool entOpen = ImGui::TreeNodeEx(entHeader, ImGuiTreeNodeFlags_DefaultOpen);
        if (entOpen) {
            for (int i = 0; i < static_cast<int>(externalEntities_.size()); ++i) {
                if (!matchesSearch(externalEntities_[i].name)) {
                    continue;
                }
                bool sel = (selKind_ == SelKind::External && selIndex_ == i);
                char label[96];
                snprintf(label, sizeof(label), "  %s##ent%d", externalEntities_[i].name.c_str(), i);
                if (ImGui::Selectable(label, sel)) {
                    selKind_ = SelKind::External;
                    selIndex_ = i;
                }
            }
            ImGui::TreePop();
        }
    }

    char trgHeader[48];
    snprintf(trgHeader, sizeof(trgHeader), "トリガー (%d)", static_cast<int>(triggers_.size()));
    bool trgOpen = ImGui::TreeNodeEx(trgHeader, ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::SameLine();
    if (ImGui::SmallButton("+##addTrg")) {
        RecordUndoSnapshotNow();
        TriggerDesc desc;
        desc.name = "trigger_" + std::to_string(nextSerial_++);
        desc.position = playerSpawn_;
        desc.flag = desc.name;
        TriggerVolume trg;
        trg.Init(desc);
        triggers_.push_back(std::move(trg));
        selKind_ = SelKind::Trigger;
        selIndex_ = static_cast<int>(triggers_.size()) - 1;
    }
    if (trgOpen) {
        for (int i = 0; i < static_cast<int>(triggers_.size()); ++i) {
            bool sel = (selKind_ == SelKind::Trigger && selIndex_ == i);
            const TriggerDesc& d = triggers_[i].GetDesc();
            if (!matchesSearch(d.name) && !matchesSearch(d.flag)) {
                continue;
            }
            char label[96];
            snprintf(label, sizeof(label), "  %s -> %s=%s", d.name.c_str(), d.flag.c_str(), d.value ? "true" : "false");
            if (ImGui::Selectable(label, sel)) {
                selKind_ = SelKind::Trigger;
                selIndex_ = i;
            }
        }
        ImGui::TreePop();
    }

    ImGui::Separator();
    // エンティティ(Player/Enemy等)はエディタが生成したものではないため削除・複製の対象外
    bool hasSel = (selKind_ == SelKind::Object || selKind_ == SelKind::Trigger);
    float halfWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    ImGui::BeginDisabled(!hasSel);
    if (ImGui::Button("複製 (Ctrl+D)", ImVec2(halfWidth, 0))) {
        DuplicateSelected();
    }
    ImGui::SameLine();
    if (ImGui::Button("選択を削除 (Del)", ImVec2(halfWidth, 0))) {
        DeleteSelected();
    }
    ImGui::EndDisabled();

    ImGui::End();
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
    if (ImGui::Button("最大化 F3")) {
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
    if (ImGui::Button("編集パネルを表示 (F3)")) {
        viewportFocusMode_ = false;
    }
    ImGui::End();
}

void StageEditor::RenderInspector()
{
    constexpr float kToolbarHeight = 42.0f;
    constexpr float kPanelWidth = 300.0f;
    ImGui::SetNextWindowPos(ImVec2(static_cast<float>(WinApp::kClientWidth) - kPanelWidth, kToolbarHeight), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(kPanelWidth, static_cast<float>(WinApp::kClientHeight) - kToolbarHeight), ImGuiCond_Always);
    ImGui::Begin("詳細設定", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);

    if (selKind_ == SelKind::Object && selIndex_ >= 0 && selIndex_ < static_cast<int>(objects_.size())) {
        ObjectEntry& entry = objects_[selIndex_];
        ObjectDesc& desc = entry.desc;
        bool structuralDirty = false;
        bool transformDirty = false;

        bool enabled = desc.enabled;
        if (ImGui::Checkbox("ゲーム側で有効", &enabled)) {
            RecordUndoSnapshotNow();
            desc.enabled = enabled;
            structuralDirty = true;
        }

        // 名前（親子参照のキーなので、変更時は子の親参照も追従させる）
        {
            char nameBuf[96];
            strncpy_s(nameBuf, desc.name.c_str(), _TRUNCATE);
            bool changed = ImGui::InputText("名前", nameBuf, sizeof(nameBuf));
            if (ImGui::IsItemActivated()) {
                BeginUndoCapture();
            }
            if (changed) {
                std::string newName = nameBuf;
                if (newName != desc.name && !newName.empty()) {
                    MarkUndoDirty();
                    for (auto& other : objects_) {
                        if (other.desc.parent == desc.name) {
                            other.desc.parent = newName;
                        }
                    }
                    desc.name = newName;
                }
            }
            if (ImGui::IsItemDeactivated()) {
                CommitUndoCapture();
            }
        }

        // 親の選択（自分自身と自分の子孫は循環になるため選択肢から除外する）
        {
            std::string currentParent = desc.parent.empty() ? "(なし)" : desc.parent;
            if (ImGui::BeginCombo("親", currentParent.c_str())) {
                if (ImGui::Selectable("(なし)", desc.parent.empty())) {
                    if (!desc.parent.empty()) {
                        RecordUndoSnapshotNow();
                        // 親を外しても見た目の位置が変わらないよう、ワールド座標をローカルへ引き継ぐ
                        desc.position = WorldPositionOf(desc);
                        desc.parent.clear();
                    }
                }
                for (const auto& other : objects_) {
                    const std::string& name = other.desc.name;
                    if (name == desc.name || IsDescendantOf(name, desc.name)) {
                        continue;
                    }
                    if (ImGui::Selectable(name.c_str(), desc.parent == name)) {
                        if (desc.parent != name) {
                            RecordUndoSnapshotNow();
                            // 付け替えても見た目の位置が変わらないよう、新しい親基準のローカル座標へ変換する
                            Vector3 world = WorldPositionOf(desc);
                            desc.parent = name;
                            Vector3 parentW = ParentWorldPositionOf(desc);
                            desc.position = Subtract(world, parentW);
                        }
                    }
                }
                ImGui::EndCombo();
            }
        }

        const bool visualKind = desc.kind == "prop" || desc.kind == "gimmick" || desc.kind == "terrain";
        if (visualKind) {
            char modelBuf[256];
            strncpy_s(modelBuf, desc.model.c_str(), _TRUNCATE);
            ImGui::SetNextItemWidth(180.0f);
            bool modelChanged = ImGui::InputText("モデル", modelBuf, sizeof(modelBuf));
            if (ImGui::IsItemActivated()) {
                BeginUndoCapture();
            }
            if (modelChanged) {
                MarkUndoDirty();
                desc.model = modelBuf;
            }
            structuralDirty |= ImGui::IsItemDeactivatedAfterEdit();
            if (ImGui::IsItemDeactivated()) {
                CommitUndoCapture();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("参照##model")) {
                std::string p = OpenFileDialog("OBJファイル\0*.obj\0すべてのファイル\0*.*\0\0", "Resources");
                if (!p.empty()) {
                    RecordUndoSnapshotNow();
                    desc.model = ToProjectRelativePath(p);
                    structuralDirty = true;
                }
            }

            char texBuf[256];
            strncpy_s(texBuf, desc.texture.c_str(), _TRUNCATE);
            ImGui::SetNextItemWidth(180.0f);
            bool texChanged = ImGui::InputText("テクスチャ", texBuf, sizeof(texBuf));
            if (ImGui::IsItemActivated()) {
                BeginUndoCapture();
            }
            if (texChanged) {
                MarkUndoDirty();
                desc.texture = texBuf;
            }
            structuralDirty |= ImGui::IsItemDeactivatedAfterEdit();
            if (ImGui::IsItemDeactivated()) {
                CommitUndoCapture();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("参照##tex")) {
                std::string p = OpenFileDialog("画像ファイル\0*.png;*.jpg;*.jpeg\0すべてのファイル\0*.*\0\0", "Resources");
                if (!p.empty()) {
                    RecordUndoSnapshotNow();
                    desc.texture = ToProjectRelativePath(p);
                    structuralDirty = true;
                }
            }

            const char* kTypes[] = { "static", "row" };
            const char* kTypeLabels[] = { "単体配置(static)", "並べて配置(row)" };
            int typeIdx = (desc.type == "row") ? 1 : 0;
            if (ImGui::Combo("種類", &typeIdx, kTypeLabels, 2)) {
                RecordUndoSnapshotNow();
                desc.type = kTypes[typeIdx];
                structuralDirty = true;
            }
            EditorUI::HelpMarker("単体配置: 1つだけ置く\n並べて配置: 同じモデルを一定間隔で複数並べる（階段や壁に便利）");
        } else {
            const char* kindLabel = desc.kind.c_str();
            ImGui::TextDisabled("%s", kindLabel);
            if (entry.knight) {
                ImGui::Text("HP: %d / %d", entry.knight->GetHp(), entry.knight->GetMaxHp());
            } else if (entry.enemy) {
                ImGui::Text("HP: %d / %d", entry.enemy->GetHp(), entry.enemy->GetMaxHp());
            }
        }

        if (!desc.parent.empty()) {
            ImGui::TextDisabled("※位置は親からの相対値");
        }
        // ドラッグ系ウィジェットは操作開始で変更前を控え、離した時に1回分のUndoとして確定する
        auto captureItemUndo = [&](bool changed) {
            if (ImGui::IsItemActivated()) {
                BeginUndoCapture();
            }
            if (changed) {
                MarkUndoDirty();
            }
            if (ImGui::IsItemDeactivated()) {
                CommitUndoCapture();
            }
            return changed;
        };
        const Vector3 previousPosition = desc.position;
        const bool positionChanged = captureItemUndo(ImGui::DragFloat3("位置", &desc.position.x, 0.1f));
        transformDirty |= positionChanged;
        if (positionChanged && selectedObjectIndices_.size() > 1) {
            const Vector3 delta = {
                desc.position.x - previousPosition.x,
                desc.position.y - previousPosition.y,
                desc.position.z - previousPosition.z
            };
            for (int index : selectedObjectIndices_) {
                if (index >= 0 && index < static_cast<int>(objects_.size()) && index != selIndex_) {
                    objects_[index].desc.position = objects_[index].desc.position + delta;
                    RefreshTransforms(objects_[index]);
                }
            }
        }

        if (visualKind || desc.kind == "camera_point") {
            const Vector3 previousRotation = desc.rotation;
            const bool rotationChanged = captureItemUndo(ImGui::DragFloat3("回転", &desc.rotation.x, 0.01f));
            transformDirty |= rotationChanged;
            const Vector3 previousScale = desc.scale;
            const bool scaleChanged = captureItemUndo(ImGui::DragFloat3("スケール", &desc.scale.x, 0.05f));
            transformDirty |= scaleChanged;
            if ((rotationChanged || scaleChanged) && selectedObjectIndices_.size() > 1) {
                const Vector3 rotationDelta = {
                    desc.rotation.x - previousRotation.x,
                    desc.rotation.y - previousRotation.y,
                    desc.rotation.z - previousRotation.z
                };
                const Vector3 scaleDelta = {
                    desc.scale.x - previousScale.x,
                    desc.scale.y - previousScale.y,
                    desc.scale.z - previousScale.z
                };
                for (int index : selectedObjectIndices_) {
                    if (index < 0 || index >= static_cast<int>(objects_.size()) || index == selIndex_
                        || (objects_[index].desc.kind != "prop" && objects_[index].desc.kind != "gimmick"
                            && objects_[index].desc.kind != "terrain" && objects_[index].desc.kind != "camera_point")) {
                        continue;
                    }
                    if (rotationChanged) {
                        objects_[index].desc.rotation = objects_[index].desc.rotation + rotationDelta;
                    }
                    if (scaleChanged) {
                        objects_[index].desc.scale = objects_[index].desc.scale + scaleDelta;
                    }
                    RefreshTransforms(objects_[index]);
                }
            }
            {
                bool lighting = desc.lighting;
                if (ImGui::Checkbox("ライティング", &lighting)) {
                    RecordUndoSnapshotNow();
                    desc.lighting = lighting;
                    transformDirty = true;
                }
                bool solid = desc.solid;
                if (ImGui::Checkbox("当たり判定あり(solid)", &solid)) {
                    RecordUndoSnapshotNow();
                    desc.solid = solid;
                }
                EditorUI::HelpMarker("ONにするとプレイヤーや敵が乗れる・ぶつかる足場になります（オレンジの枠で表示）");
            }

            if (desc.kind == "terrain") {
                if (ImGui::Checkbox("メッシュ同期コライダー", &desc.meshCollider)) {
                    RecordUndoSnapshotNow();
                    desc.solid = desc.meshCollider || desc.solid;
                }
            }

            if (desc.type == "row") {
                const char* kAxes[] = { "x", "y", "z" };
                int axisIdx = (desc.axis == 'y') ? 1 : (desc.axis == 'z') ? 2
                                                                          : 0;
                if (ImGui::Combo("並べる軸", &axisIdx, kAxes, 3)) {
                    RecordUndoSnapshotNow();
                    desc.axis = kAxes[axisIdx][0];
                    structuralDirty = true;
                }
                int count = desc.count;
                if (ImGui::InputInt("個数", &count)) {
                    RecordUndoSnapshotNow();
                    desc.count = (std::max)(1, count);
                    structuralDirty = true;
                }
                transformDirty |= captureItemUndo(ImGui::DragFloat("間隔", &desc.step, 0.05f));
            }
        }

        if (desc.kind == "gimmick" || desc.kind == "spawn_point" || desc.kind == "camera_point") {
            char flagBuffer[96] = { };
            strncpy_s(flagBuffer, desc.activationFlag.c_str(), _TRUNCATE);
            if (captureItemUndo(ImGui::InputText("有効化フラグ", flagBuffer, sizeof(flagBuffer)))) {
                desc.activationFlag = flagBuffer;
            }
            EditorUI::HelpMarker("空なら常時有効です。イベントトリガーが同名のフラグを立てると有効になります");
        }
        if (desc.kind == "spawn_point") {
            const char* spawnTypes[] = { "basic", "knight" };
            int spawnTypeIndex = desc.spawnType == "knight" ? 1 : 0;
            if (ImGui::Combo("発生する敵", &spawnTypeIndex, spawnTypes, 2)) {
                RecordUndoSnapshotNow();
                desc.spawnType = spawnTypes[spawnTypeIndex];
                structuralDirty = true;
            }
        }
        if (desc.kind == "spawn_point" || desc.kind == "enemy_basic" || desc.kind == "enemy_knight") {
            char groupBuffer[96] = { };
            strncpy_s(groupBuffer, desc.enemyGroup.c_str(), _TRUNCATE);
            if (captureItemUndo(ImGui::InputText("敵グループ", groupBuffer, sizeof(groupBuffer)))) {
                desc.enemyGroup = groupBuffer;
            }
        }
        if (desc.kind == "enemy_basic" || desc.kind == "enemy_knight" || desc.kind == "patrol_point") {
            char routeBuffer[96] = { };
            strncpy_s(routeBuffer, desc.patrolRoute.c_str(), _TRUNCATE);
            if (captureItemUndo(ImGui::InputText("巡回ルート名", routeBuffer, sizeof(routeBuffer)))) {
                desc.patrolRoute = routeBuffer;
            }
            if (desc.kind == "patrol_point") {
                if (ImGui::InputInt("巡回順", &desc.routeOrder)) {
                    RecordUndoSnapshotNow();
                }
            } else {
                captureItemUndo(ImGui::DragFloat("巡回速度", &desc.patrolSpeed, 0.05f, 0.0f, 20.0f));
            }
        }
        if (desc.kind == "event_condition") {
            const char* conditionTypes[] = { "manual", "timer", "enemy_group_defeated" };
            int conditionIndex = desc.conditionType == "timer" ? 1
                : desc.conditionType == "enemy_group_defeated" ? 2
                                                                 : 0;
            if (ImGui::Combo("条件", &conditionIndex, conditionTypes, 3)) {
                RecordUndoSnapshotNow();
                desc.conditionType = conditionTypes[conditionIndex];
            }
            if (desc.conditionType == "timer") {
                captureItemUndo(ImGui::DragFloat("成立までの秒数", &desc.conditionSeconds, 0.1f, 0.0f, 300.0f));
            } else if (desc.conditionType == "enemy_group_defeated") {
                char groupBuffer[96] = { };
                strncpy_s(groupBuffer, desc.enemyGroup.c_str(), _TRUNCATE);
                if (captureItemUndo(ImGui::InputText("監視する敵グループ", groupBuffer, sizeof(groupBuffer)))) {
                    desc.enemyGroup = groupBuffer;
                }
            }
        }
        if (desc.kind == "gimmick") {
            const char* motions[] = { "none", "move_y", "rotate_y", "fall", "blink" };
            int motionIndex = desc.gimmickMotion == "move_y" ? 1
                : desc.gimmickMotion == "rotate_y"            ? 2
                : desc.gimmickMotion == "fall"                ? 3
                : desc.gimmickMotion == "blink"               ? 4
                                                                  : 0;
            if (ImGui::Combo("動作プリセット", &motionIndex, motions, 5)) {
                RecordUndoSnapshotNow();
                desc.gimmickMotion = motions[motionIndex];
            }
            captureItemUndo(ImGui::DragFloat("動作量", &desc.motionAmount, 0.1f));
            captureItemUndo(ImGui::DragFloat("動作速度", &desc.motionSpeed, 0.1f, 0.0f, 20.0f));
        }
        if (desc.kind == "camera_point") {
            captureItemUndo(ImGui::DragFloat("カメラ補間秒数", &desc.cameraBlendSeconds, 0.05f, 0.0f, 10.0f));
            captureItemUndo(ImGui::DragFloat("カメラ維持秒数", &desc.cameraHoldSeconds, 0.1f, 0.0f, 30.0f));
        }

        if (structuralDirty) {
            RegenerateInstances(entry);
        } else if (transformDirty && visualKind) {
            // enemy系はStageEditor::UpdateObjects()側が毎フレームdesc.position⇔実体位置を同期するので、ここでは不要
            RefreshTransforms(entry);
        }
    } else if (selKind_ == SelKind::Trigger && selIndex_ >= 0 && selIndex_ < static_cast<int>(triggers_.size())) {
        TriggerDesc& desc = triggers_[selIndex_].GetDesc();

        auto captureItemUndo = [&](bool changed) {
            if (ImGui::IsItemActivated()) {
                BeginUndoCapture();
            }
            if (changed) {
                MarkUndoDirty();
            }
            if (ImGui::IsItemDeactivated()) {
                CommitUndoCapture();
            }
            return changed;
        };

        char nameBuf[96];
        strncpy_s(nameBuf, desc.name.c_str(), _TRUNCATE);
        bool nameChanged = ImGui::InputText("名前", nameBuf, sizeof(nameBuf));
        if (captureItemUndo(nameChanged)) {
            desc.name = nameBuf;
        }

        char flagBuf[96];
        strncpy_s(flagBuf, desc.flag.c_str(), _TRUNCATE);
        bool flagChanged = ImGui::InputText("フラグ名", flagBuf, sizeof(flagBuf));
        if (captureItemUndo(flagChanged)) {
            desc.flag = flagBuf;
        }
        EditorUI::HelpMarker("プレイヤーが球に入ると、この名前のフラグが立ちます。\nノードエディタ(F1)のGetFlagノードで参照できます");

        captureItemUndo(ImGui::DragFloat3("位置", &desc.position.x, 0.1f));
        captureItemUndo(ImGui::DragFloat("半径", &desc.radius, 0.05f, 0.1f, 50.0f));
        {
            bool value = desc.value;
            if (ImGui::Checkbox("進入時に設定する値", &value)) {
                RecordUndoSnapshotNow();
                desc.value = value;
            }
            bool once = desc.once;
            if (ImGui::Checkbox("一度だけ成立させる", &once)) {
                RecordUndoSnapshotNow();
                desc.once = once;
            }
        }
        ImGui::TextDisabled(triggers_[selIndex_].IsInside() ? "プレイヤーは範囲内にいます" : "プレイヤーは範囲外です");
    } else if (selKind_ == SelKind::External && selIndex_ >= 0 && selIndex_ < static_cast<int>(externalEntities_.size())) {
        ExternalEntityRef& ref = externalEntities_[selIndex_];
        ImGui::Text("%s", ref.name.c_str());
        ImGui::TextDisabled("ランタイム実体（JSONには保存されません）");
        if (ref.position) {
            ImGui::DragFloat3("位置", &ref.position->x, 0.1f);
        }
    } else {
        ImGui::TextDisabled("左のステージエディタでオブジェクト/トリガーを選択してください");
    }

    ImGui::End();
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

void StageEditor::DrawGizmos()
{
    // チェックポイントは緑の範囲と十字で表示する
    for (const CheckpointDesc& checkpoint : checkpoints_) {
        DebugDraw::DrawSphere({ checkpoint.position, checkpoint.activationRadius }, DebugDraw::kColorGreen);
        DebugDraw::DrawCross(checkpoint.position, 0.45f, DebugDraw::kColorGreen);
    }

    for (int i = 0; i < static_cast<int>(triggers_.size()); ++i) {
        const TriggerDesc& d = triggers_[i].GetDesc();
        ImU32 color = triggers_[i].IsInside()                  ? DebugDraw::kColorGreen
            : (selKind_ == SelKind::Trigger && selIndex_ == i) ? DebugDraw::kColorYellow
                                                               : DebugDraw::kColorCyan;
        DebugDraw::DrawSphere({ d.position, d.radius }, color);
        DebugDraw::DrawCross(d.position, 0.3f, color);
    }
    // 全オブジェクトに小さな十字マーカーを出し、どこをクリックすれば掴めるか分かるようにする
    // solid=trueのものは実際の当たり判定AABBをオレンジのワイヤーフレームで重ねて表示する
    // 敵配置（enemy_knight/enemy_basic）は赤い十字にして配置物(白)と一目で区別できるようにする
    for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
        bool sel = std::find(selectedObjectIndices_.begin(), selectedObjectIndices_.end(), i)
            != selectedObjectIndices_.end();
        const ObjectDesc& d = objects_[i].desc;
        bool isEnemy = (d.kind != "prop");
        Vector3 world = WorldPositionOf(d);
        ImU32 baseColor = isEnemy ? DebugDraw::kColorRed : DebugDraw::kColorWhite;
        DebugDraw::DrawCross(world, sel ? 0.6f : (isEnemy ? 0.35f : 0.25f), sel ? DebugDraw::kColorYellow : baseColor);
        if (sel) {
            // 選択位置から3軸を表示し、ワークフロー上の軸制限と対応させる
            constexpr float kAxisLength = 2.0f;
            DebugDraw::DrawLine(world, world + Vector3 { kAxisLength, 0.0f, 0.0f }, DebugDraw::kColorRed);
            DebugDraw::DrawLine(world, world + Vector3 { 0.0f, kAxisLength, 0.0f }, DebugDraw::kColorGreen);
            DebugDraw::DrawLine(world, world + Vector3 { 0.0f, 0.0f, kAxisLength }, DebugDraw::kColorCyan);
        }

        if (d.solid) {
            Vector3 half = { 0.5f * d.scale.x, 0.5f * d.scale.y, 0.5f * d.scale.z };
            DebugDraw::DrawAABB({ { world.x - half.x, world.y - half.y, world.z - half.z },
                                    { world.x + half.x, world.y + half.y, world.z + half.z } },
                DebugDraw::kColorOrange);
        }

        // 親子関係を白線で可視化する（親→子）
        const std::string& parentName = objects_[i].desc.parent;
        if (!parentName.empty()) {
            for (const auto& other : objects_) {
                if (other.desc.name == parentName) {
                    DebugDraw::DrawLine(WorldPositionOf(other.desc), world, IM_COL32(255, 255, 255, 90));
                    break;
                }
            }
        }
    }

    // イベントの条件から対象へ接続線を引き、複数アクションの流れを可視化する
    for (const auto& targetEntry : objects_) {
        const ObjectDesc& target = targetEntry.desc;
        if (target.activationFlag.empty()) {
            continue;
        }
        bool sourceFound = false;
        Vector3 sourcePosition = { };
        for (const auto& trigger : triggers_) {
            if (trigger.GetDesc().flag == target.activationFlag) {
                sourcePosition = trigger.GetDesc().position;
                sourceFound = true;
                break;
            }
        }
        if (!sourceFound && target.activationFlag.starts_with("condition_")) {
            const std::string conditionName = target.activationFlag.substr(10);
            for (const auto& condition : objects_) {
                if (condition.desc.kind == "event_condition" && condition.desc.name == conditionName) {
                    sourcePosition = WorldPositionOf(condition.desc);
                    sourceFound = true;
                    break;
                }
            }
        }
        if (sourceFound) {
            const ImU32 color = target.activeWhenFlag ? DebugDraw::kColorGreen : DebugDraw::kColorRed;
            DebugDraw::DrawLine(sourcePosition, WorldPositionOf(target), color);
        }
    }

    // Player/Enemy等のランタイム実体（マゼンタの十字。オブジェクトの白・トリガーのシアンと区別する）
    for (int i = 0; i < static_cast<int>(externalEntities_.size()); ++i) {
        const ExternalEntityRef& ref = externalEntities_[i];
        if (!ref.position) {
            continue;
        }
        bool sel = (selKind_ == SelKind::External && selIndex_ == i);
        DebugDraw::DrawCross(*ref.position, sel ? 0.6f : 0.3f, sel ? DebugDraw::kColorYellow : DebugDraw::kColorMagenta);
    }
}

void StageEditor::UpdateFreeCamera(Input* input, float dt)
{
    viewport_.UpdateCamera(input, dt, viewportFocusMode_);
}

bool StageEditor::MouseToGround(float mouseX, float mouseY, Vector3& outWorld) const
{
    return viewport_.ScreenToGround(mouseX, mouseY, outWorld);
}

void StageEditor::UpdateViewportInteraction()
{
    ImGuiIO& io = ImGui::GetIO();
    const bool insideSceneView = viewport_.Contains(io.MousePos.x, io.MousePos.y, viewportFocusMode_);

    // 編集パネル上の操作をシーンビューの選択やカメラ移動として扱わない
    if (io.WantCaptureMouse || !insideSceneView) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (viewportDragging_) {
                CommitUndoCapture(); // パネル上で離した場合もドラッグ分をここで確定する
            }
            viewportDragging_ = false;
        }
        return;
    }

    const ImVec2 m = io.MousePos;

    // マウスホイール  カメラを奥/手前へ移動（Q/Eと同じ軸、手前に回すと近づく）
    if (camera_ && io.MouseWheel != 0.0f) {
        constexpr float kWheelSpeed = 2.0f;
        camera_->GetTranslate().z += io.MouseWheel * kWheelSpeed;
    }

    // 左クリック  画面上で一番近いオブジェクト/トリガーを選択（40px以内）
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        float bestDist = 40.0f;
        SelKind bestKind = SelKind::None;
        int bestIdx = -1;

        auto consider = [&](const Vector3& worldPos, SelKind kind, int index) {
            ImVec2 s;
            if (!DebugDraw::WorldToScreen(worldPos, s)) {
                return;
            }
            float dx = s.x - m.x;
            float dy = s.y - m.y;
            float dist = std::sqrt(dx * dx + dy * dy);
            if (dist < bestDist) {
                bestDist = dist;
                bestKind = kind;
                bestIdx = index;
            }
        };

        for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
            consider(WorldPositionOf(objects_[i].desc), SelKind::Object, i);
        }
        for (int i = 0; i < static_cast<int>(triggers_.size()); ++i) {
            consider(triggers_[i].GetDesc().position, SelKind::Trigger, i);
        }
        for (int i = 0; i < static_cast<int>(externalEntities_.size()); ++i) {
            if (externalEntities_[i].position) {
                consider(*externalEntities_[i].position, SelKind::External, i);
            }
        }

        if (bestIdx >= 0) {
            selKind_ = bestKind;
            selIndex_ = bestIdx;
            if (bestKind == SelKind::Object) {
                if (!io.KeyCtrl) {
                    selectedObjectIndices_.clear();
                }
                auto selected = std::find(selectedObjectIndices_.begin(), selectedObjectIndices_.end(), bestIdx);
                if (selected == selectedObjectIndices_.end()) {
                    selectedObjectIndices_.push_back(bestIdx);
                }
            } else {
                selectedObjectIndices_.clear();
            }
            viewportDragging_ = true;

            // ドラッグ1回ぶんを1つのUndoにまとめるため、変更前をここで控える
            // （エンティティはスナップショット対象外なので、動かしても確定時に捨てられる）
            BeginUndoCapture();

            // Shift+ドラッグ(Z移動)用に、選択物の現在のZをスナップ前の生値として持つ
            if (bestKind == SelKind::Object) {
                dragRawZ_ = objects_[bestIdx].desc.position.z;
            } else if (bestKind == SelKind::Trigger) {
                dragRawZ_ = triggers_[bestIdx].GetDesc().position.z;
            } else if (externalEntities_[bestIdx].position) {
                dragRawZ_ = externalEntities_[bestIdx].position->z;
            }
        }
    }

    // ドラッグ中  通常はマウス位置（z=0平面上）へXY移動（Zは保持）、Shift中は垂直マウス移動をZ移動にする
    if (viewportDragging_ && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const bool mouseMoved = (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f);
        if (io.KeyShift || gizmoAxis_ == 3) {
            // カメラが遠いほど1pxあたりの移動量を増やし、近くでも遠くでも同じ操作感にする
            float camDist = 10.0f;
            if (camera_) {
                camDist = (std::max)(1.0f, std::abs(camera_->GetTranslate().z));
            }
            dragRawZ_ += -io.MouseDelta.y * camDist * kZDragPerPixel;
            const float snappedZ = SnapValue(dragRawZ_);
            if (selKind_ == SelKind::Object && selIndex_ >= 0 && selIndex_ < static_cast<int>(objects_.size())) {
                objects_[selIndex_].desc.position.z = snappedZ;
                if (mouseMoved) {
                    MarkUndoDirty();
                }
            } else if (selKind_ == SelKind::Trigger && selIndex_ >= 0 && selIndex_ < static_cast<int>(triggers_.size())) {
                triggers_[selIndex_].GetDesc().position.z = snappedZ;
                if (mouseMoved) {
                    MarkUndoDirty();
                }
            } else if (selKind_ == SelKind::External && selIndex_ >= 0 && selIndex_ < static_cast<int>(externalEntities_.size())) {
                Vector3* pos = externalEntities_[selIndex_].position;
                if (pos) {
                    pos->z = snappedZ;
                }
            }
        } else {
            Vector3 ground;
            if (MouseToGround(m.x, m.y, ground)) {
                // スナップはワールド座標側で丸めてから、親がいる場合はローカル座標へ逆算する
                const float wx = SnapValue(ground.x);
                const float wy = SnapValue(ground.y);
                if (selKind_ == SelKind::Object && selIndex_ >= 0 && selIndex_ < static_cast<int>(objects_.size())) {
                    ObjectDesc& desc = objects_[selIndex_].desc;
                    Vector3 parentW = ParentWorldPositionOf(desc);
                    if (gizmoAxis_ == 0 || gizmoAxis_ == 1) {
                        desc.position.x = wx - parentW.x;
                    }
                    if (gizmoAxis_ == 0 || gizmoAxis_ == 2) {
                        desc.position.y = wy - parentW.y;
                    }
                    if (mouseMoved) {
                        MarkUndoDirty();
                    }
                } else if (selKind_ == SelKind::Trigger && selIndex_ >= 0 && selIndex_ < static_cast<int>(triggers_.size())) {
                    TriggerDesc& desc = triggers_[selIndex_].GetDesc();
                    if (gizmoAxis_ == 0 || gizmoAxis_ == 1) {
                        desc.position.x = wx;
                    }
                    if (gizmoAxis_ == 0 || gizmoAxis_ == 2) {
                        desc.position.y = wy;
                    }
                    if (mouseMoved) {
                        MarkUndoDirty();
                    }
                } else if (selKind_ == SelKind::External && selIndex_ >= 0 && selIndex_ < static_cast<int>(externalEntities_.size())) {
                    Vector3* pos = externalEntities_[selIndex_].position;
                    if (pos) {
                        pos->x = wx;
                        pos->y = wy;
                    }
                }
            }
        }
    }

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        if (viewportDragging_) {
            CommitUndoCapture(); // 実際に動かしていた場合だけ1回分のUndoとして確定する
        }
        viewportDragging_ = false;
    }
}
#else
void StageEditor::RenderHierarchy() { }
void StageEditor::RenderEditorToolbar() { }
void StageEditor::RenderViewportFocusBar() { }
void StageEditor::RenderInspector() { }
void StageEditor::RenderAssetPalette() { }
void StageEditor::RenderFlagsPanel() { }
void StageEditor::RenderWorkflowPanel() { }
void StageEditor::RenderNoCodeEventPanel() { }
void StageEditor::RenderWavePanel() { }
void StageEditor::RenderStageAnalysisPanel() { }
void StageEditor::RenderDiffPanel() { }
void StageEditor::RenderEditorHelpPanel() { }
void StageEditor::DrawGizmos() { }
void StageEditor::UpdateFreeCamera(Input*, float) { }
void StageEditor::UpdateViewportInteraction() { }
void StageEditor::DrawHierarchyEntry(int, int) { }
bool StageEditor::MouseToGround(float, float, Vector3&) const { return false; }
#endif
