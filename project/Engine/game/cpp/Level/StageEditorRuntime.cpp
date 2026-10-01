/**
 * @file StageEditorRuntime.cpp
 * @brief ステージ配置物のランタイム更新・描画・実体生成を実装するファイル
 * @note StageEditor.cppからの分割ファイル実プレイ中に毎フレーム動く責務（配置物の状態更新・描画・
 * 敵/巡回/イベント条件の評価）をまとめている。クラス自体はStageEditorのまま、定義の置き場所だけを分けている
 */
#include "StageEditor.h"
#include "StageEditorUiStyle.h"
#include "FallingFloorBehavior.h"
#include "Camera.h"
#include "DirectXCommon.h"
#include "EnemyEntity.h"
#include "EnemyRegistry.h"
#include "EnemyTuning.h"
#include "FontRenderer.h"
#include "GameConstants.h"
#include "GameFlags.h"
#include "KnightEnemy.h"
#include "Model.h"
#include "ModelCommon.h"
#include "ModelManager.h"
#include "Object3d.h"
#include "ParticleManager.h"
#include "Player.h"
#include "PlayerBridge.h"
#include "SceneShared.h"
#include "ScreenFlash.h"
#include "StringUtility.h"
#include "TimeManager.h"
#include "WinApp.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <random>
using namespace engine::game;
using namespace engine;
using namespace engine::graphics;

Model* StageEditor::GetOrLoadModel(const std::string& modelPath, const std::string& texPath)
{
    const std::string key = modelPath + '|' + texPath;
    Model* model = ModelManager::GetInstance()->GetOrLoad(modelCommon_, modelPath, texPath);
    modelCache_[key] = model;
    return model;
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
    // 実体を作り直す＝ゲーム内の状態もやり直す（回収済み/破壊済み/グラフによる一時状態を初期値へ戻す）
    entry.pickupCollected = false;
    entry.breakableHp = desc.breakableHp;
    entry.breakableDestroyed = false;
    entry.visibleOverride = true;
    entry.enabledOverride = -1;
    entry.graphMoveActive = false;
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
            entry.enemy->Initialize(modelCommon_, WorldPositionOf(desc), ParseWeaponTypeName(desc.weaponType));
            entry.enemy->SetArchetype(desc.spawnType);
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
    center = ViewCenterOnGround();

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
    selectedObjectIndices_ = { selIndex_ };
}

void StageEditor::EnsureHudAnchors()
{
    auto ensureAnchor = [&](const std::string& name, const std::string& label, const Vector3& defaultPos) {
        for (const auto& entry : objects_) {
            if (entry.desc.name == name) {
                return;
            }
        }
        ObjectEntry entry;
        entry.desc.name = name;
        entry.desc.kind = "hud_anchor";
        entry.desc.text = label;
        entry.desc.position = defaultPos;
        entry.runtimeActive = true;
        objects_.push_back(std::move(entry));
    };
    // 既定位置は、これまでコード側に直書きされていた各HUDの原点座標と同じにする
    ensureAnchor("hud_anchor_controls", "操作説明", StageEditorUiStyle::kControlsHudAnchorPosition);
    ensureAnchor("hud_anchor_weapon_list", "武器選択", StageEditorUiStyle::kWeaponListHudAnchorPosition);
}

Vector2 StageEditor::GetHudAnchorPosition(const std::string& anchorName, const Vector2& fallback) const
{
    for (const auto& entry : objects_) {
        if (entry.desc.kind == "hud_anchor" && entry.desc.name == anchorName) {
            return { entry.desc.position.x, entry.desc.position.y };
        }
    }
    return fallback;
}

void StageEditor::RegisterExternalEntity(const std::string& name, Vector3* position,
    std::function<int()> getVisualPreset, std::function<void(int)> setVisualPreset,
    std::function<void(const std::string&, const std::string&)> setStaticVisualModel,
    std::function<std::string()> getStaticVisualModel, std::function<std::string()> getStaticVisualTexture)
{
    for (auto& ref : externalEntities_) {
        if (ref.name == name) {
            ref.getVisualPreset = std::move(getVisualPreset);
            ref.setVisualPreset = std::move(setVisualPreset);
            ref.setStaticVisualModel = std::move(setStaticVisualModel);
            ref.getStaticVisualModel = std::move(getStaticVisualModel);
            ref.getStaticVisualTexture = std::move(getStaticVisualTexture);
            ref.position = position; // 同名なら上書き（Scene再初期化等での再登録に備える）
            return;
        }
    }
    ExternalEntityRef ref;
    ref.name = name;
    ref.position = position;
    ref.getVisualPreset = std::move(getVisualPreset);
    ref.setVisualPreset = std::move(setVisualPreset);
    ref.setStaticVisualModel = std::move(setStaticVisualModel);
    ref.getStaticVisualModel = std::move(getStaticVisualModel);
    ref.getStaticVisualTexture = std::move(getStaticVisualTexture);
    externalEntities_.push_back(std::move(ref));
}

void StageEditor::RegisterExternalObject(const std::string& name, Object3d* object,
    std::function<void()> onDelete, std::function<void()> onDuplicate)
{
    if (!object) {
        return;
    }
    Vector3* position = &object->GetTransform().translate;
    for (auto& ref : externalEntities_) {
        if (ref.name == name) {
            ref.position = position;
            ref.object = object;
            ref.onDelete = std::move(onDelete);
            ref.onDuplicate = std::move(onDuplicate);
            return;
        }
    }
    ExternalEntityRef ref;
    ref.name = name;
    ref.position = position;
    ref.object = object;
    ref.onDelete = std::move(onDelete);
    ref.onDuplicate = std::move(onDuplicate);
    externalEntities_.push_back(std::move(ref));
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
    // 編集プレビュー中（プレイテスト前）はTimeManagerがゲーム停止用に0倍速へ落としているため、
    // その間もギミックのプレビューが動き続けるよう固定delta（従来通り）を使う。
    // プレイ中（プレイテスト/実プレイ）はヒットストップ・時間倍率と歩調を合わせないと、
    // ヒットストップ中に見た目（動く床など）だけ進んでプレイヤーの当たり判定と食い違い、
    // 解除後に浮いて見える／めり込んで見える原因になる
    constexpr float kEditorPreviewDeltaSeconds = 1.0f / 60.0f;
    const float kRuntimeDeltaSeconds = ShouldPauseGame()
        ? kEditorPreviewDeltaSeconds
        : TimeManager::GetInstance()->GetDeltaTime();

    // 条件、接続先の有効化、実体の順で更新して同じフレーム内に結果を反映する
    EvaluateEventConditions(kRuntimeDeltaSeconds);
    UpdateRuntimeActivation(kRuntimeDeltaSeconds);
    for (auto& entry : objects_) {
        UpdateRuntimeEntry(entry, pm, playerPos, kRuntimeDeltaSeconds);
    }
    // レベル紐付きグラフは配置物の状態が確定した後に進める（編集停止中はロジックも止める）
    if (!ShouldPauseGame()) {
        levelGraphs_.Update(kRuntimeDeltaSeconds);
    }
}

StageEditor::GimmickOffset StageEditor::ComputeGimmickOffset(const ObjectEntry& entry, float timerOverride) const
{
    GimmickOffset offset;
    const ObjectDesc& desc = entry.desc;
    if (desc.kind != "gimmick") {
        return offset;
    }
    const float timer = timerOverride >= 0.0f ? timerOverride : entry.runtimeTimer;
    const float phase = timer * desc.motionSpeed;
    if (desc.gimmickMotion == "move_y") {
        offset.position.y = std::sin(phase) * desc.motionAmount;
    } else if (desc.gimmickMotion == "move_x") {
        offset.position.x = std::sin(phase) * desc.motionAmount;
    } else if (desc.gimmickMotion == "rotate_y") {
        offset.rotation.y = phase;
    } else if (desc.gimmickMotion == "rotate_z") {
        offset.rotation.z = phase;
    } else if (desc.gimmickMotion == "custom") {
        // 進行度t（-1〜1または0〜1）をモードごとに求め、移動方向×量と回転量へ同じtを掛ける
        float t = 0.0f;
        if (desc.motionMode == "pingpong") {
            // 等速の三角波（0→1→0）。phaseは1周期=2として折り返す
            constexpr float kPingPongPeriod = 2.0f;
            const float cycle = std::fmod(std::abs(phase), kPingPongPeriod);
            t = cycle <= 1.0f ? cycle : kPingPongPeriod - cycle;
        } else if (desc.motionMode == "once") {
            t = std::clamp(phase, 0.0f, 1.0f);
        } else {
            t = std::sin(phase);
        }
        if (desc.motionEase == "smooth") {
            // 符号を保ったままsmoothstepで加減速を付ける（loopのsin波はそのまま滑らかなので対象外）
            const float sign = t < 0.0f ? -1.0f : 1.0f;
            const float mag = std::abs(t);
            t = sign * (mag * mag * (3.0f - 2.0f * mag));
        }
        offset.position = desc.motionAxis * (desc.motionAmount * t);
        offset.rotation = desc.motionRotation * t;
    }
    return offset;
}

StageEditor::ObjectEntry* StageEditor::FindEntryByName(const std::string& name)
{
    for (auto& entry : objects_) {
        if (entry.desc.name == name) {
            return &entry;
        }
    }
    return nullptr;
}

bool StageEditor::SetObjectVisibleByName(const std::string& name, bool visible)
{
    ObjectEntry* entry = FindEntryByName(name);
    if (!entry) {
        return false;
    }
    entry->visibleOverride = visible;
    if (entry->enemy) {
        entry->enemy->SetVisible(visible);
    }
    return true;
}

bool StageEditor::TeleportObjectByName(const std::string& name, const Vector3& position)
{
    ObjectEntry* entry = FindEntryByName(name);
    if (!entry) {
        return false;
    }
    entry->graphMoveActive = false;
    entry->desc.position = position;
    entry->authoredPosition = position;
    if (entry->knight) {
        entry->knight->GetPositionRef() = WorldPositionOf(entry->desc);
        entry->knight->RefreshVisualTransforms();
    } else if (entry->enemy) {
        entry->enemy->GetPositionRef() = WorldPositionOf(entry->desc);
        entry->enemy->RefreshVisualTransforms();
    } else {
        RefreshTransforms(*entry);
        for (auto& obj : entry->instances) {
            obj->Update();
        }
    }
    return true;
}

bool StageEditor::MoveObjectByName(const std::string& name, const Vector3& target, float seconds)
{
    ObjectEntry* entry = FindEntryByName(name);
    if (!entry) {
        return false;
    }
    if (seconds <= 0.0f) {
        return TeleportObjectByName(name, target);
    }
    entry->graphMoveActive = true;
    entry->graphMoveFrom = entry->desc.position;
    entry->graphMoveTo = target;
    entry->graphMoveTimer = 0.0f;
    entry->graphMoveDuration = seconds;
    return true;
}

bool StageEditor::SetObjectEnabledByName(const std::string& name, bool enabled)
{
    ObjectEntry* entry = FindEntryByName(name);
    if (!entry) {
        return false;
    }
    entry->enabledOverride = enabled ? 1 : 0;
    return true;
}
