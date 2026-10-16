/**
 * @file StageEditorRuntime.cpp
 * @brief ステージ配置物のランタイム更新・描画・実体生成を実装するファイル
 * @note StageEditor.cppからの分割ファイル実プレイ中に毎フレーム動く責務（配置物の状態更新・描画・
 * 敵/巡回/イベント条件の評価）をまとめている。クラス自体はStageEditorのまま、定義の置き場所だけを分けている
 */
#include "StageEditor.h"
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
#include <limits>
#include <cmath>
#include <random>
using namespace engine::game;
using namespace engine;
using namespace engine::graphics;

namespace {
/** @brief 敵の体のうち壁と当たる範囲（足元の床に掛からないよう、位置より少し上から頭まで） */
engine::AABB EnemyWallBody(const Vector3& pos, const EnemySeparationTuning& tuning)
{
    return { { pos.x - tuning.bodyHalfWidth, pos.y + tuning.wallBandBottom, pos.z },
        { pos.x + tuning.bodyHalfWidth, pos.y + tuning.wallBandTop, pos.z } };
}

/** @brief 体がブロックと横に重なっているか（奥行きは敵の位置の1点で見る） */
bool OverlapsWall(const engine::AABB& body, const engine::AABB& wall)
{
    return body.max.y > wall.min.y && body.min.y < wall.max.y
        && body.min.z >= wall.min.z && body.max.z <= wall.max.z
        && body.max.x > wall.min.x && body.min.x < wall.max.x;
}

/** @brief 壁にめり込んでいれば、入り込みの浅い側へ押し出した後のX座標を返す */
float PushOutOfWalls(const Vector3& pos, const std::vector<engine::AABB>& walls, const EnemySeparationTuning& tuning)
{
    Vector3 resolved = pos;
    for (const engine::AABB& wall : walls) {
        const engine::AABB body = EnemyWallBody(resolved, tuning);
        if (!OverlapsWall(body, wall)) {
            continue;
        }
        const float pushLeft = wall.min.x - body.max.x; // 負の値: 左へ押し出す量
        const float pushRight = wall.max.x - body.min.x; // 正の値: 右へ押し出す量
        resolved.x += (std::abs(pushLeft) < std::abs(pushRight)) ? pushLeft : pushRight;
    }
    return resolved.x;
}

/** @brief 指定した向き（左=-1, 右=+1）のすぐ横に壁があるか（その向きへは押されても動けない） */
bool WallOnSide(const Vector3& pos, float side, const std::vector<engine::AABB>& walls, const EnemySeparationTuning& tuning)
{
    // 体のその向きの端だけを細い帯で調べる（反対側の壁に触れていても誤判定しない）
    const float margin = tuning.wallContactMargin;
    engine::AABB probe = EnemyWallBody(pos, tuning);
    const float edgeX = side < 0.0f ? probe.min.x : probe.max.x;
    probe.min.x = edgeX - margin;
    probe.max.x = edgeX + margin;
    return std::any_of(walls.begin(), walls.end(), [&probe](const engine::AABB& wall) { return OverlapsWall(probe, wall); });
}
} // namespace

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

    const ObjectKind& kind = ObjectKind::Of(desc.kind);
    kind.PrepareEnemyInstance(*this, IndexOf(entry));
    if (kind.PlacesEnemy() || kind.IsMarker()) {
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
    int instanceCount = (desc.type == PlacementType::Row) ? (std::max)(1, desc.count) : 1;
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
        if (desc.type == PlacementType::Row) {
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
    entry.desc.type = PlacementType::Static;
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
    // シーンが進行を止めている間（ポーズ・開始演出）は時間を進めず、動く仕掛けもその場で止める
    const float kRuntimeDeltaSeconds = ShouldPauseGame()
        ? kEditorPreviewDeltaSeconds
        : (gameplayHeld_ ? 0.0f : TimeManager::GetInstance()->GetDeltaTime());

    // 条件、接続先の有効化、実体の順で更新して同じフレーム内に結果を反映する
    EvaluateEventConditions(kRuntimeDeltaSeconds);
    UpdateRuntimeActivation(kRuntimeDeltaSeconds);
    // ヒットストップ中（時間が止まっている間）は押し離しも止め、止まった敵が滑って見えないようにする
    const bool separateEnemies = !ShouldPauseGame() && !gameplayHeld_ && kRuntimeDeltaSeconds > 0.0f;
    // 地形はこのフレームの動く床の位置で作る（GetSolidColliders と同じく実体の更新前に集める）
    const std::vector<engine::AABB> walls = separateEnemies ? CollectSolidColliders(false) : std::vector<engine::AABB> {};
    for (auto& entry : objects_) {
        UpdateRuntimeEntry(entry, pm, playerPos, kRuntimeDeltaSeconds);
    }
    if (separateEnemies) {
        SeparateOverlappingEnemies(walls);
    }
    // レベル紐付きグラフは配置物の状態が確定した後に進める（編集停止中はロジックも止める）
    if (!ShouldPauseGame() && !gameplayHeld_) {
        levelGraphs_.Update(kRuntimeDeltaSeconds);
    }
}

void StageEditor::SeparateOverlappingEnemies(const std::vector<engine::AABB>& walls)
{
    const EnemySeparationTuning& tuning = EnemyTuning::GetInstance()->Separation();
    const float pushRate = std::clamp(tuning.pushRate, 0.0f, 1.0f);

    // 押し離しの対象になる敵（汎用敵とナイトを同じ体として扱う）
    struct Body {
        ObjectEntry* entry;
        IEnemyEntity* enemy;
        bool anchored; ///< 押されても動かない（ボス）
        bool wallLeft = false; ///< 左に壁が接していて左へは動けない
        bool wallRight = false; ///< 右に壁が接していて右へは動けない
        bool moved = false;
        /** @brief 指定した向き（左=-1, 右=+1）へ押された時に動けるか */
        bool CanMove(float side) const { return !anchored && !(side < 0.0f ? wallLeft : wallRight); }
    };
    std::vector<Body> bodies;
    for (auto& entry : objects_) {
        IEnemyEntity* enemy = entry.knight ? static_cast<IEnemyEntity*>(entry.knight.get()) : entry.enemy.get();
        if (entry.runtimeActive && enemy != nullptr && enemy->BlocksOtherEnemies()) {
            bodies.push_back({ &entry, enemy, entry.desc.isStageBoss });
        }
    }

    // 自分の移動で壁にめり込んだ敵を先に押し戻し、壁際の敵は壁の方へは押されないようにする
    constexpr float kLeft = -1.0f;
    constexpr float kRight = 1.0f;
    for (Body& body : bodies) {
        Vector3& pos = body.enemy->GetPositionRef();
        const float resolvedX = PushOutOfWalls(pos, walls, tuning);
        body.moved = resolvedX != pos.x;
        pos.x = resolvedX;
        body.wallLeft = WallOnSide(pos, kLeft, walls, tuning);
        body.wallRight = WallOnSide(pos, kRight, walls, tuning);
    }

    for (size_t i = 0; i < bodies.size(); ++i) {
        for (size_t j = i + 1; j < bodies.size(); ++j) {
            Body& a = bodies[i];
            Body& b = bodies[j];
            Vector3& posA = a.enemy->GetPositionRef();
            Vector3& posB = b.enemy->GetPositionRef();
            const float dx = posB.x - posA.x;
            const float distanceX = std::abs(dx);
            if (distanceX >= tuning.minDistanceX || std::abs(posB.y - posA.y) >= tuning.minDistanceY) {
                continue;
            }
            // 真上に重なっている時は並び順で向きを決め、2体が同じ側へ逃げないようにする
            const float direction = distanceX > 0.0f ? (dx > 0.0f ? 1.0f : -1.0f) : 1.0f;
            const float push = (tuning.minDistanceX - distanceX) * pushRate;
            // A は -direction、B は +direction へずれる。片方が動けない時はもう片方が全部ずれ、両方動ける時は半分ずつ
            const bool canMoveA = a.CanMove(-direction);
            const bool canMoveB = b.CanMove(direction);
            if (!canMoveA && !canMoveB) {
                continue;
            }
            const float shareA = !canMoveB ? 1.0f : (!canMoveA ? 0.0f : 0.5f);
            const float shareB = 1.0f - shareA;
            posA.x -= direction * push * shareA;
            posB.x += direction * push * shareB;
            a.moved = a.moved || shareA > 0.0f;
            b.moved = b.moved || shareB > 0.0f;
        }
    }

    for (Body& body : bodies) {
        if (!body.moved) {
            continue;
        }
        // 押し離しで壁へ入った分も戻してから見た目へ反映する
        Vector3& pos = body.enemy->GetPositionRef();
        pos.x = PushOutOfWalls(pos, walls, tuning);
        body.enemy->RefreshVisualTransforms();
        // Inspector表示・親子追従の基準にするため現在地を書き戻す（JSON保存はしない）
        body.entry->desc.position = body.enemy->GetPosition();
    }
}

float StageEditor::FindCorpseFloorY(const Vector3& position) const
{
    // 体の幅に掛かるブロックのうち、足元より下（段差の許容まで）で一番高い上面を床とする
    constexpr float kStepTolerance = 0.12f;
    const float halfWidth = EnemyTuning::GetInstance()->Separation().bodyHalfWidth;
    const float footOffset = EnemyTuning::GetInstance()->Basic().corpseFootOffset;
    const float feetY = position.y - footOffset;
    float floorTop = (std::numeric_limits<float>::lowest)();
    for (const engine::AABB& block : CollectSolidColliders(false)) {
        const bool overlapX = position.x + halfWidth > block.min.x && position.x - halfWidth < block.max.x;
        const bool overlapZ = position.z >= block.min.z && position.z <= block.max.z;
        if (overlapX && overlapZ && block.max.y <= feetY + kStepTolerance) {
            floorTop = (std::max)(floorTop, block.max.y);
        }
    }
    if (floorTop == (std::numeric_limits<float>::lowest)()) {
        return position.y;
    }
    return floorTop + footOffset;
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
