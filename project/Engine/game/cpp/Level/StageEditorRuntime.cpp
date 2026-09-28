/**
 * @file StageEditorRuntime.cpp
 * @brief ステージ配置物のランタイム更新・描画・実体生成を実装するファイル
 * @note StageEditor.cppからの分割ファイル実プレイ中に毎フレーム動く責務（配置物の状態更新・描画・
 * 敵/巡回/イベント条件の評価）をまとめている。クラス自体はStageEditorのまま、定義の置き場所だけを分けている
 */
#include "StageEditor.h"
#include "Camera.h"
#include "DirectXCommon.h"
#include "EnemyEntity.h"
#include "EnemyRegistry.h"
#include "EnemyTuning.h"
#include "FontRenderer.h"
#include "GameFlags.h"
#include "KnightEnemy.h"
#include "Model.h"
#include "ModelCommon.h"
#include "Object3d.h"
#include "ParticleManager.h"
#include "Player.h"
#include "PlayerBridge.h"
#include "SceneShared.h"
#include "ScreenFlash.h"
#include "StringUtility.h"
#include "WinApp.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <random>
using namespace engine::game;
using namespace engine;
using namespace engine::graphics;

namespace {
constexpr float kFallFloorWarningEnd = 0.6f;
constexpr float kFallFloorRespawnStart = 3.5f;
constexpr float kFallFloorCycleEnd = 4.0f;
constexpr float kFallFloorCrackLeadTime = 0.28f; // 崩落の何秒前からひび割れの砂ぼこりを出すか

struct FallingFloorState {
    float yOffset = 0.0f;
    bool visible = true;
    bool solid = true;
    float phase = 0.0f; // サイクル内経過秒（0〜kFallFloorCycleEnd）。ひび割れ演出のタイミング判定に使う
};

// "fall" gimmicks repeatedly collapse, remain absent, then rise back into place.
FallingFloorState GetFallingFloorState(float runtimeTimer, float motionSpeed, float motionAmount)
{
    const float speed = (std::max)(motionSpeed, 0.01f);
    const float phase = std::fmod(runtimeTimer * speed, kFallFloorCycleEnd);
    (void)motionAmount;

    FallingFloorState state;
    state.phase = phase;
    if (phase < kFallFloorWarningEnd) {
        return state;
    }
    if (phase < kFallFloorRespawnStart) {
        // 床だけが高速で下降すると、通常重力のプレイヤーが空中に取り残される。
        // 壊れる床はその場で消し、同じフレームから当たり判定も無効にする。
        state.visible = false;
        state.solid = false;
        return state;
    }
    // 復帰時も途中の高さに当たり判定を出さず、元の位置へ直接戻す。
    return state;
}

std::mt19937& FallFloorDebrisRng()
{
    static std::mt19937 rng { std::random_device {}() };
    return rng;
}

// 崩落ギミックの床に、崩れる直前のひび割れ粉塵と、崩落/復帰の瞬間の土煙をまとめて出す
// （床が消えるだけだと壊れる床だと気付きにくいので、視覚的な予告と崩落感を補う）
void EmitFallingFloorDebris(ParticleManager* pm, const Vector3& worldPos, const Vector3& halfExtent,
    const FallingFloorState& state, bool& wasSolid)
{
    if (!pm) {
        wasSolid = state.solid;
        return;
    }
    auto& rng = FallFloorDebrisRng();
    std::uniform_real_distribution<float> ox(-halfExtent.x, halfExtent.x);
    std::uniform_real_distribution<float> oz(-halfExtent.z, halfExtent.z);
    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    const Vector4 kDustColor = { 0.8f, 0.72f, 0.56f, 0.75f };

    if (wasSolid && !state.solid) {
        // 崩落の瞬間：破片が飛び散り、土煙が広がる
        std::uniform_real_distribution<float> vx(-1.8f, 1.8f);
        std::uniform_real_distribution<float> vy(0.6f, 2.0f);
        for (int i = 0; i < 14; ++i) {
            const Vector3 p = { worldPos.x + ox(rng), worldPos.y + halfExtent.y, worldPos.z + oz(rng) };
            pm->EmitGravity("land_dust", p, { vx(rng), vy(rng), 0.0f },
                kDustColor, 0.55f, 0.14f + chance(rng) * 0.1f);
        }
        pm->EmitRing("land_dust", worldPos + Vector3 { 0.0f, halfExtent.y, 0.0f },
            1.2f, { kDustColor.x, kDustColor.y, kDustColor.z, 0.5f }, 14, 0.5f, 0.2f);
    } else if (!wasSolid && state.solid) {
        // 復帰の瞬間：着地のような軽い土煙
        for (int i = 0; i < 6; ++i) {
            const Vector3 p = { worldPos.x + ox(rng), worldPos.y + halfExtent.y, worldPos.z + oz(rng) };
            pm->EmitGravity("land_dust", p, { 0.0f, 0.2f + chance(rng) * 0.2f, 0.0f },
                kDustColor, 0.3f, 0.12f);
        }
    } else if (state.solid && state.phase >= kFallFloorWarningEnd - kFallFloorCrackLeadTime
        && state.phase < kFallFloorWarningEnd && chance(rng) < 0.35f) {
        // 崩落直前：ひびの隙間から砂ぼこりが間欠的に噴く（崩れる床だと気付かせる警告演出）
        const Vector3 p = { worldPos.x + ox(rng), worldPos.y + halfExtent.y + 0.02f, worldPos.z + oz(rng) };
        pm->EmitGravity("land_dust", p, { 0.0f, 0.3f + chance(rng) * 0.3f, 0.0f },
            kDustColor, 0.35f, 0.08f + chance(rng) * 0.06f);
    }
    wasSolid = state.solid;
}
} // namespace

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
    ensureAnchor("hud_anchor_controls", "操作説明", { 1020.0f, 12.0f, 0.0f });
    ensureAnchor("hud_anchor_weapon_list", "武器選択", { 12.0f, 12.0f, 0.0f });
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

std::vector<engine::AABB> StageEditor::GetSolidColliders() const
{
    std::vector<engine::AABB> result;
    for (const auto& entry : objects_) {
        if (!entry.desc.solid || !entry.runtimeActive || !entry.visibleOverride
            || entry.pickupCollected || entry.breakableDestroyed) {
            continue;
        }
        const ObjectDesc& desc = entry.desc;
        Vector3 basePos = WorldPositionOf(desc);
        if (desc.kind == "gimmick") {
            if (desc.gimmickMotion == "fall") {
                const FallingFloorState state = GetFallingFloorState(
                    entry.runtimeTimer, desc.motionSpeed, desc.motionAmount);
                if (!state.solid) {
                    continue;
                }
                basePos.y += state.yOffset;
            } else {
                basePos = basePos + ComputeGimmickOffset(entry).position;
            }
        }
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
    // レベル紐付きグラフは配置物の状態が確定した後に進める（編集停止中はロジックも止める）
    if (!ShouldPauseGame()) {
        levelGraphs_.Update(kRuntimeDeltaSeconds);
    }
}

StageEditor::GimmickOffset StageEditor::ComputeGimmickOffset(const ObjectEntry& entry) const
{
    GimmickOffset offset;
    const ObjectDesc& desc = entry.desc;
    if (desc.kind != "gimmick") {
        return offset;
    }
    const float phase = entry.runtimeTimer * desc.motionSpeed;
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

void StageEditor::UpdateGraphMove(ObjectEntry& entry, float dt)
{
    if (!entry.graphMoveActive) {
        return;
    }
    entry.graphMoveTimer += dt;
    const float t = entry.graphMoveDuration <= 0.0f
        ? 1.0f
        : std::clamp(entry.graphMoveTimer / entry.graphMoveDuration, 0.0f, 1.0f);
    entry.desc.position = {
        entry.graphMoveFrom.x + (entry.graphMoveTo.x - entry.graphMoveFrom.x) * t,
        entry.graphMoveFrom.y + (entry.graphMoveTo.y - entry.graphMoveFrom.y) * t,
        entry.graphMoveFrom.z + (entry.graphMoveTo.z - entry.graphMoveFrom.z) * t
    };
    if (t >= 1.0f) {
        entry.graphMoveActive = false;
    }
}

void StageEditor::UpdatePickupEntry(ObjectEntry& entry, ParticleManager* pm, const Vector3& playerPos)
{
    constexpr float kPickupPulseSpeed = 5.0f; // 脈動の速さ
    constexpr float kPickupPulseAmplitude = 0.15f; // 脈動によるスケールの揺れ幅
    constexpr float kPickupSpinSpeed = 1.5f; // 回転の速さ（ラジアン毎秒）
    constexpr float kPickupRingRadius = 3.0f; // 回収時に広がるリングの半径
    constexpr int kPickupRingCount = 18;
    constexpr float kPickupRingLifetime = 0.45f;
    constexpr float kPickupRingSize = 0.3f;
    constexpr float kPickupFlashAlpha = 0.2f;
    constexpr float kPickupFlashSeconds = 0.08f;

    const ObjectDesc& desc = entry.desc;
    const Vector3 worldPos = WorldPositionOf(desc);
    if (!ShouldPauseGame()) {
        const float dx = playerPos.x - worldPos.x;
        const float dy = playerPos.y - worldPos.y;
        if (dx * dx + dy * dy <= desc.pickupRadius * desc.pickupRadius) {
            entry.pickupCollected = true;
            GameFlags::GetInstance()->SetFlag("pickup_" + desc.name, true);
            if (Player* player = PlayerBridge::GetInstance()->Get()) {
                player->ChargeAwakenGauge(desc.pickupGaugeAmount);
            }
            if (pm) {
                pm->EmitRing("awaken_aura", worldPos, kPickupRingRadius, desc.pickupColor,
                    kPickupRingCount, kPickupRingLifetime, kPickupRingSize);
            }
            ScreenFlash::GetInstance()->Request(
                { desc.pickupColor.x, desc.pickupColor.y, desc.pickupColor.z, kPickupFlashAlpha }, kPickupFlashSeconds);
            return;
        }
    }

    // 未回収の間は回転と脈動で拾える物だと分かるようにする（位置は親子追従のためRefreshTransformsで先に反映する）
    RefreshTransforms(entry);
    const float pulse = 1.0f - kPickupPulseAmplitude + std::sin(entry.runtimeTimer * kPickupPulseSpeed) * kPickupPulseAmplitude;
    for (auto& obj : entry.instances) {
        obj->SetRotation({ desc.rotation.x, desc.rotation.y + entry.runtimeTimer * kPickupSpinSpeed, desc.rotation.z });
        obj->SetScale(desc.scale * pulse);
        obj->SetColor({ desc.pickupColor.x, desc.pickupColor.y * pulse, desc.pickupColor.z, desc.pickupColor.w });
        obj->Update();
    }
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

bool StageEditor::FindObjectWorldPosition(const std::string& name, Vector3& outPosition) const
{
    for (const auto& entry : objects_) {
        if (entry.desc.name == name) {
            outPosition = WorldPositionOf(entry.desc);
            return true;
        }
    }
    return false;
}

std::vector<BreakableRef> StageEditor::GetBreakables()
{
    std::vector<BreakableRef> result;
    for (auto& entry : objects_) {
        if (entry.desc.kind != "breakable" || !entry.runtimeActive || entry.breakableDestroyed || entry.instances.empty()) {
            continue;
        }
        BreakableRef ref;
        ref.desc = &entry.desc;
        ref.position = WorldPositionOf(entry.desc);
        ref.hp = &entry.breakableHp;
        ref.destroyed = &entry.breakableDestroyed;
        ref.object = entry.instances.front().get();
        result.push_back(ref);
    }
    return result;
}

void StageEditor::GetPickupCounts(int& outCollected, int& outTotal) const
{
    outCollected = 0;
    outTotal = 0;
    for (const auto& entry : objects_) {
        if (entry.desc.kind != "pickup" || !entry.desc.enabled) {
            continue;
        }
        ++outTotal;
        if (entry.pickupCollected) {
            ++outCollected;
        }
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
        // グラフのSetObjectEnabledによる上書きは有効化フラグより優先する
        if (entry.enabledOverride >= 0) {
            entry.runtimeActive = entry.enabledOverride == 1;
            if (entry.runtimeActive) {
                entry.runtimeTimer += dt;
            }
            continue;
        }
        if (desc.activationFlag.empty()) {
            entry.runtimeActive = true;
            if (desc.kind == "gimmick" || desc.kind == "pickup" || desc.kind == "breakable") {
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
    if (entry.desc.kind == "patrol_point" || (entry.enabledOverride < 0 && !IsRuntimeActive(entry.desc))) {
        return;
    }
    if (entry.knight || entry.enemy) {
        UpdateEnemyEntry(entry, pm, playerPos);
        return;
    }
    if (!ShouldPauseGame()) {
        UpdateGraphMove(entry, dt);
    }
    if (entry.desc.kind == "pickup") {
        if (!entry.pickupCollected) {
            UpdatePickupEntry(entry, pm, playerPos);
        }
        return;
    }
    if (entry.desc.kind == "breakable") {
        // 見た目のトランスフォームだけ追従させる（ヒット判定と破壊はシーン側がGetBreakables()経由で行う）
        if (!entry.breakableDestroyed) {
            RefreshTransforms(entry);
            for (auto& obj : entry.instances) {
                obj->Update();
            }
        }
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
        if (entry.desc.gimmickMotion == "fall") {
            const FallingFloorState fallState = GetFallingFloorState(entry.runtimeTimer,
                entry.desc.motionSpeed, entry.desc.motionAmount);
            entry.desc.position.y += fallState.yOffset;
            const Vector3 halfExtent = { 0.5f * std::abs(entry.desc.scale.x),
                0.5f * std::abs(entry.desc.scale.y), 0.5f * std::abs(entry.desc.scale.z) };
            EmitFallingFloorDebris(pm, WorldPositionOf(entry.desc), halfExtent, fallState, entry.fallFloorWasSolid);
        } else {
            const GimmickOffset offset = ComputeGimmickOffset(entry);
            entry.desc.position = entry.desc.position + offset.position;
            entry.desc.rotation = entry.desc.rotation + offset.rotation;
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
    // 詠唱（回復発動までのタメ）の間に被弾すると中断できる。数値はenemy_params.jsonのbasic節で調整する
    const BasicEnemyTuning& tuning = EnemyTuning::GetInstance()->Basic();
    const float kHealerPulseSeconds = tuning.healerPulseSeconds;
    const float kHealerCastSeconds = tuning.healerCastSeconds;
    const float kHealerRange = tuning.healerRange;
    const int kHealerAmount = tuning.healerAmount;
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
            entry.enemy->Update(playerPos.x);
            entry.desc.position = entry.enemy->GetPosition();
            if (entry.enemy->IsHealer() && !entry.enemy->IsDefeated()) {
                const int hpNow = entry.enemy->GetHp();
                if (entry.healChanneling && hpNow < entry.healChannelHpAtStart) {
                    // 詠唱中にHPが減った＝回復を中断された。サイクルを最初からやり直させる
                    // （プレイヤーの直接攻撃に限らず、爆発バレルの爆風などダメージ源は問わない仕様）
                    entry.healChanneling = false;
                    entry.runtimeTimer = 0.0f;
                    if (pm) {
                        pm->EmitHitStar("hit_spark", entry.enemy->GetPosition() + Vector3 { 0.0f, 0.7f, 0.0f },
                            { 1.0f, 0.9f, 0.35f, 1.0f });
                    }
                } else {
                    entry.runtimeTimer += 1.0f / 60.0f;
                    if (!entry.healChanneling && entry.runtimeTimer >= kHealerPulseSeconds - kHealerCastSeconds) {
                        // 詠唱開始。ここから完了までの間に被弾すると上のブロックで中断される
                        entry.healChanneling = true;
                        entry.healChannelHpAtStart = hpNow;
                        if (pm) {
                            pm->EmitRing("heal_pulse", entry.enemy->GetPosition() + Vector3 { 0.0f, 0.7f, 0.0f },
                                kHealerRange * 0.5f, { 0.3f, 1.0f, 0.4f, 0.4f }, 16, kHealerCastSeconds, 0.16f);
                        }
                    }
                    if (entry.runtimeTimer >= kHealerPulseSeconds) {
                        entry.runtimeTimer = 0.0f;
                        entry.healChanneling = false;
                        const Vector3 healerPos = entry.enemy->GetPosition();
                        // 回復対象のHP状態にかかわらず、回復行動そのものを緑の波で知らせる。
                        if (pm) {
                            pm->EmitRing("heal_pulse", healerPos + Vector3 { 0.0f, 0.7f, 0.0f },
                                kHealerRange, { 0.2f, 1.0f, 0.35f, 0.75f }, 24, 0.65f, 0.22f);
                        }
                        bool healedAnyAlly = false;
                        for (auto& ally : objects_) {
                            if (!ally.enemy || ally.enemy.get() == entry.enemy.get() || ally.enemy->IsDefeated()) continue;
                            const Vector3 allyPos = ally.enemy->GetPosition();
                            const float dx = allyPos.x - healerPos.x;
                            const float dy = allyPos.y - healerPos.y;
                            if (dx * dx + dy * dy <= kHealerRange * kHealerRange) {
                                const int hpBefore = ally.enemy->GetHp();
                                ally.enemy->Heal(kHealerAmount);
                                if (ally.enemy->GetHp() > hpBefore) {
                                    healedAnyAlly = true;
                                    if (pm) {
                                        const Vector3 effectPos = allyPos + Vector3 { 0.0f, 0.65f, 0.0f };
                                        pm->EmitRing("heal_pulse", effectPos, 1.4f,
                                            { 0.25f, 1.0f, 0.4f, 0.9f }, 12, 0.55f, 0.16f);
                                        pm->EmitBurst("heal_pulse", effectPos,
                                            { 0.55f, 1.0f, 0.65f, 0.85f }, 8, 0.7f, 0.12f, true);
                                    }
                                }
                            }
                        }
                        if (healedAnyAlly && pm) {
                            pm->EmitBurst("heal_pulse", healerPos + Vector3 { 0.0f, 0.7f, 0.0f },
                                { 0.4f, 1.0f, 0.55f, 1.0f }, 14, 1.0f, 0.18f, true);
                        }
                    }
                }
            }
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
        if (!entry.runtimeActive || !entry.visibleOverride || entry.pickupCollected || entry.breakableDestroyed) {
            continue;
        }
        if (entry.desc.kind == "gimmick" && entry.desc.gimmickMotion == "blink"
            && std::sin(entry.runtimeTimer * entry.desc.motionSpeed) < 0.0f) {
            continue;
        }
        if (entry.desc.kind == "gimmick" && entry.desc.gimmickMotion == "fall"
            && !GetFallingFloorState(entry.runtimeTimer, entry.desc.motionSpeed,
                entry.desc.motionAmount).visible) {
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

void StageEditor::DrawUIText(FontRenderer& font) const
{
    // エディタ編集中だけ、テキスト表示チェックボックスoffで配置物の邪魔にならないよう隠す（実プレイ中は常に表示）
    if (visible_ && !showUIText_) {
        return;
    }
    float camX = 0.0f;
    float camY = 0.0f;
    if (camera_) {
        const Vector3& camPos = camera_->GetTranslate();
        camX = camPos.x;
        camY = camPos.y;
    }
    for (const auto& entry : objects_) {
        const ObjectDesc& desc = entry.desc;
        if (desc.kind != "ui_text" || !entry.runtimeActive || desc.text.empty()) {
            continue;
        }
        const Vector3 pos = WorldPositionOf(desc);
        float screenX = pos.x;
        float screenY = pos.y;
        if (desc.textSpace == "world") {
            SceneShared::WorldToScreen(pos.x, pos.y, camX, camY, screenX, screenY);
        }
        font.DrawStringW(StringUtility::ConvertString(desc.text), screenX, screenY,
            desc.textScale, desc.textColor, desc.textBold);
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

std::vector<CombatEnemyRef> StageEditor::GetCombatEnemies() const
{
    std::vector<CombatEnemyRef> result;
    for (const auto& entry : objects_) {
        // 直置きの敵に加えて、spawn_pointから生成された敵も戦闘対象にする（出現制御をレベル側で組めるように）
        const bool combatKind = entry.desc.kind == "enemy_basic" || entry.desc.kind == "spawn_point";
        if (!entry.enemy || !entry.runtimeActive || !combatKind || entry.desc.weaponType.empty()) {
            continue;
        }
        CombatEnemyRef ref;
        ref.name = entry.desc.name;
        ref.weaponType = ParseWeaponTypeName(entry.desc.weaponType);
        ref.isStageBoss = entry.desc.isStageBoss;
        ref.enemy = entry.enemy.get();
        result.push_back(ref);
    }
    return result;
}
