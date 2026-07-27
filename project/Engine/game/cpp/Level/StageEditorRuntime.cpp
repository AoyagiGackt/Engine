/**
 * @file StageEditorRuntime.cpp
 * @brief ステージ配置物のランタイム更新・描画・実体生成を実装するファイル
 * @note StageEditor.cppからの分割ファイル実プレイ中に毎フレーム動く責務（配置物の状態更新・描画・
 * 敵/巡回/イベント条件の評価）をまとめている。クラス自体はStageEditorのまま、定義の置き場所だけを分けている
 */
#include "StageEditor.h"
#include "DirectXCommon.h"
#include "EnemyEntity.h"
#include "EnemyRegistry.h"
#include "GameFlags.h"
#include "KnightEnemy.h"
#include "Model.h"
#include "ModelCommon.h"
#include "Object3d.h"
#include "ParticleManager.h"
#include "WinApp.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
using namespace engine::game;
using namespace engine;
using namespace engine::graphics;

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
    selectedObjectIndices_ = { selIndex_ };
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

void StageEditor::RegisterExternalObject(const std::string& name, Object3d* object, std::function<void()> onDelete)
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
            return;
        }
    }
    ExternalEntityRef ref;
    ref.name = name;
    ref.position = position;
    ref.object = object;
    ref.onDelete = std::move(onDelete);
    externalEntities_.push_back(std::move(ref));
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
                    result.push_back({ { (std::min)({ a.x, b.x, c.x }) - kColliderThickness,
                                           (std::min)({ a.y, b.y, c.y }) - kColliderThickness,
                                           (std::min)({ a.z, b.z, c.z }) - kColliderThickness },
                        { (std::max)({ a.x, b.x, c.x }) + kColliderThickness,
                            (std::max)({ a.y, b.y, c.y }) + kColliderThickness,
                            (std::max)({ a.z, b.z, c.z }) + kColliderThickness } });
                }
            }
            continue;
        }
        // 回転後の表示形状を含むワールドAABBを組み立てる
        const float hx = 0.5f * std::abs(desc.scale.x);
        const float hy = 0.5f * std::abs(desc.scale.y);
        const float hz = 0.5f * std::abs(desc.scale.z);
        const float cx = std::cos(desc.rotation.x), sx = std::sin(desc.rotation.x);
        const float cy = std::cos(desc.rotation.y), sy = std::sin(desc.rotation.y);
        const float cz = std::cos(desc.rotation.z), sz = std::sin(desc.rotation.z);
        const auto rotateExtent = [&](Vector3 value) {
            value = { value.x, value.y * cx - value.z * sx, value.y * sx + value.z * cx };
            value = { value.x * cy + value.z * sy, value.y, -value.x * sy + value.z * cy };
            return Vector3 { value.x * cz - value.y * sz, value.x * sz + value.y * cz, value.z };
        };

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
            Vector3 minCorner = { FLT_MAX, FLT_MAX, FLT_MAX };
            Vector3 maxCorner = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
            for (float x : { -hx, hx }) {
                for (float y : { -hy, hy }) {
                    for (float z : { -hz, hz }) {
                        const Vector3 corner = pos + rotateExtent({ x, y, z });
                        minCorner = { (std::min)(minCorner.x, corner.x), (std::min)(minCorner.y, corner.y), (std::min)(minCorner.z, corner.z) };
                        maxCorner = { (std::max)(maxCorner.x, corner.x), (std::max)(maxCorner.y, corner.y), (std::max)(maxCorner.z, corner.z) };
                    }
                }
            }
            result.push_back({ minCorner, maxCorner });
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

    // rotate_z の子は親側で同じ一時回転中に更新する。ここで更新し直すと元位置へ戻ってしまう。
    if (!entry.desc.parent.empty()) {
        for (const auto& candidate : objects_) {
            if (candidate.desc.name == entry.desc.parent && candidate.desc.kind == "gimmick"
                && candidate.desc.gimmickMotion == "rotate_z") {
                return;
            }
        }
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
        } else if (entry.desc.gimmickMotion == "rotate_z") {
            entry.desc.rotation.z += phase;
        }
    }
    RefreshTransforms(entry);
    if (entry.desc.gimmickMotion == "rotate_z") {
        for (auto& child : objects_) {
            if (child.desc.parent == entry.desc.name) {
                RefreshTransforms(child);
                for (auto& obj : child.instances) {
                    obj->Update();
                }
            }
        }
    }
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
