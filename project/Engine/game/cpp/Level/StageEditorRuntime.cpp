/**
 * @file StageEditorRuntime.cpp
 * @brief ステージ配置物のランタイム更新・描画・実体生成を実装するファイル
 * @note StageEditor.cppからの分割ファイル実プレイ中に毎フレーム動く責務（配置物の状態更新・描画・
 * 敵/巡回/イベント条件の評価）をまとめている。クラス自体はStageEditorのまま、定義の置き場所だけを分けている
 */
#include "StageEditor.h"
#include "GroundSupport.h"
#include "EnemySeparation.h"
#include <unordered_map>
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
#include "ObjectKind.h"
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
using namespace engine::graphics;



namespace engine::game {
/** @brief モデル+テクスチャの組み合わせをキャッシュから探し、無ければロードして登録する */
Model* StageEditor::GetOrLoadModel(const std::string& modelPath, const std::string& texPath) {
    const std::string key = modelPath + '|' + texPath;
    Model* model = ModelManager::GetInstance()->GetOrLoad(modelCommon_, modelPath, texPath);
    modelCache_[key] = model;
    return model;
}

/** @brief モデル/軸/個数など構造が変わったときの再構築（instances/knight/enemyを作り直す） */
void StageEditor::RegenerateInstances(ObjectEntry& entry) {
    // 構造変更で既存実体を作り直す場合だけGPUの参照完了を待つ
    if (!entry.instances.empty() || entry.knight || entry.enemy) {
        WaitForGpuIdle();
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
    // ゲーム側で無効にした配置物は、敵の登録を外して実体を持たない
    if (!desc.enabled) {
        UnregisterEnemyEntity(entry);
        entry.knight.reset();
        entry.enemy.reset();
        return;
    }

    // 敵は種類ごとの準備に任せ、目印だけの種類は見た目を作らない
    const ObjectKind& kind = ObjectKind::Of(desc.kind);
    kind.PrepareEnemyInstance(*this, IndexOf(entry));
    if (kind.PlacesEnemy() || kind.IsMarker()) {
        return;
    }

    // モデルを持つ種類だけ、列配置の個数ぶん実体を作る
    if (desc.model.empty()) {
        return;
    }

    Model* model = GetOrLoadModel(desc.model, desc.texture);

    auto spawnOne = [&](const Vector3& pos) {
        entry.instances.push_back(levelLoader::CreatePlacedObject(modelCommon_, model, desc, pos));
    };

    // 位置はRefreshTransforms()が毎フレーム上書きするため、ここでは個数分の生成だけが本質
    int instanceCount = (std::max)(1, PlacementCopyCount(desc));
    for (int i = 0; i < instanceCount; ++i) {
        spawnOne(desc.position);
    }
    RefreshTransforms(entry);
}

/** @brief ファクトリが生成した配置データをレベル実体へ追加する */
void StageEditor::AppendGeneratedContent(StageEditorGeneratedContent content) {
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

/** @brief 位置/回転/スケールだけを既存instancesへ反映する軽量パス（kind=="prop"専用） */
void StageEditor::RefreshTransforms(ObjectEntry& entry) {
    const ObjectDesc& desc = entry.desc;
    Vector3 basePos = WorldPositionOf(desc); // 親がいる場合は親のワールド位置＋ローカルオフセット

    for (int i = 0; i < static_cast<int>(entry.instances.size()); ++i) {
        const Vector3 pos = PlacementInstancePosition(desc, basePos, i);
        entry.instances[i]->SetPosition(pos);
        entry.instances[i]->SetRotation(desc.rotation);
        entry.instances[i]->SetScale(desc.scale);
        entry.instances[i]->SetEnableLighting(desc.lighting);
    }
}

/** @brief 画面中央(z=0平面)に新規の配置物(prop)を1つ追加して選択状態にする（+ボタン/アセットパレット共通） */
void StageEditor::AddPropAtScreenCenter(const std::string& model, const std::string& texture) {
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
    entry.desc.type = PlacementType::kStatic;
    entry.desc.position = center;
    entry.desc.model = model;
    entry.desc.texture = texture;
    objects_.push_back(std::move(entry));
    RegenerateInstances(objects_.back());
    SelectLastObject();
}

/** @brief 「敵を作る」で作った種類の敵を画面中央に置いて選択する */
void StageEditor::PlaceEnemyOfType(const EnemyTypeDef& type) {
    // 置いてから種類の値を写し、写した設定で敵の実体を作り直す
    const int index = AddObjectAt(objectKindName::kEnemyBasic, ViewCenterOnGround());
    ObjectEntry& entry = objects_[index];
    EnemyTypeLibrary::ApplyTo(type, entry.desc);
    RegenerateInstances(entry);
    statusMessage_ = (type.label.empty() ? type.id : type.label) + " を置きました";
    statusTimer_ = StageEditor::kStatusShortSeconds;
}

/** @brief 最後に追加した配置物だけを選択した状態にする */
void StageEditor::SelectLastObject() {
    selKind_ = SelKind::kObject;
    selIndex_ = static_cast<int>(objects_.size()) - 1;
    selectedObjectIndices_ = { selIndex_ };
}

/** @brief レベルJSON外の実体の位置操作と見た目変更の操作をエディタへ登録する */
void StageEditor::RegisterExternalEntity(const std::string& name, IPositionTarget* positionTarget,
    std::function<int()> getVisualPreset, std::function<void(int)> setVisualPreset,
    std::function<void(const std::string&, const std::string&)> setStaticVisualModel,
    std::function<std::string()> getStaticVisualModel, std::function<std::string()> getStaticVisualTexture) {
    ExternalEntityRef ref;
    ref.name = name;
    ref.positionTarget = positionTarget;
    ref.getVisualPreset = std::move(getVisualPreset);
    ref.setVisualPreset = std::move(setVisualPreset);
    ref.setStaticVisualModel = std::move(setStaticVisualModel);
    ref.getStaticVisualModel = std::move(getStaticVisualModel);
    ref.getStaticVisualTexture = std::move(getStaticVisualTexture);
    const auto found = std::find_if(externalEntities_.begin(), externalEntities_.end(), [&](const ExternalEntityRef& entry) {
        return entry.name == name;
    });
    if (found != externalEntities_.end()) {
        *found = std::move(ref);
    } else {
        externalEntities_.push_back(std::move(ref));
    }
}

/** @brief enabledとactivationFlagを評価してゲーム側で有効な配置か返す */
bool StageEditor::IsRuntimeActive(const ObjectDesc& desc) const {
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

/** @brief 配置オブジェクトのギミックとトランスフォームを1フレーム分進める（Draw 前に毎フレーム呼ぶ） */
void StageEditor::UpdateObjects(ParticleManager* pm, const Vector3& playerPos) {
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
        SeparateOverlappingEnemies(walls, enemiesAvoidPlayer_ ? std::optional<Vector3>(playerPos) : std::nullopt);
    }
    // レベル紐付きグラフは配置物の状態が確定した後に進める（編集停止中はロジックも止める）
    if (!ShouldPauseGame() && !gameplayHeld_) {
        levelGraphs_.Update(kRuntimeDeltaSeconds);
    }
}

/** @brief 配置された敵を位置補正へ渡し、移動結果を配置情報へ反映する */
void StageEditor::SeparateOverlappingEnemies(const std::vector<engine::AABB>& walls, const std::optional<Vector3>& playerPosition) {
    std::vector<EnemySeparation::Target> targets;
    std::unordered_map<IEnemyEntity*, ObjectEntry*> entries;
    for (auto& entry : objects_) {
        IEnemyEntity* enemy = entry.EnemyBody();
        if (entry.runtimeActive && enemy != nullptr && enemy->BlocksOtherEnemies()) {
            targets.emplace_back(enemy, entry.desc.isStageBoss);
            entries.emplace(enemy, &entry);
        }
    }
    for (IEnemyEntity* enemy : EnemySeparation::Resolve(targets, walls, playerPosition)) {
        entries.at(enemy)->desc.position = enemy->GetPosition();
    }
}

/** @brief 敵の位置の真下にある床の上で、倒れた体を置く高さを返す（床が無ければ今の高さ） */
float StageEditor::FindCorpseFloorY(const Vector3& position) const {
    const float halfWidth = EnemyTuning::GetInstance()->Separation().bodyHalfWidth;
    const float footOffset = EnemyTuning::GetInstance()->Basic().corpseFootOffset;
    const auto floor = FindSupportingFloor(position, halfWidth, 0.0f, footOffset, CollectSolidColliders(false));
    return floor ? *floor + footOffset : position.y;
}

/** @brief 名前から配置物を探す（無ければnullptr） */
StageEditor::ObjectEntry* StageEditor::FindEntryByName(const std::string& name) {
    for (auto& entry : objects_) {
        if (entry.desc.name == name) {
            return &entry;
        }
    }
    return nullptr;
}

/** @brief 名前から配置物の設定を探す（無ければ nullptr） */
const ObjectDesc* StageEditor::FindDescByName(const std::string& name) const {
    for (const auto& entry : objects_) {
        if (entry.desc.name == name) {
            return &entry.desc;
        }
    }
    return nullptr;
}

/** @brief 配置物の表示/非表示を切り替える（レベルJSONには保存しない一時状態） */
bool StageEditor::SetObjectVisibleByName(const std::string& name, bool visible) {
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

/** @brief 配置物のローカル位置を即座に書き換える */
bool StageEditor::TeleportObjectByName(const std::string& name, const Vector3& position) {
    ObjectEntry* entry = FindEntryByName(name);
    if (!entry) {
        return false;
    }
    entry->graphMoveActive = false;
    entry->desc.position = position;
    entry->authoredPosition = position;
    if (entry->knight) {
        entry->knight->SetPosition(WorldPositionOf(entry->desc));
        entry->knight->RefreshVisualTransforms();
    } else if (entry->enemy) {
        entry->enemy->SetPosition(WorldPositionOf(entry->desc));
        entry->enemy->RefreshVisualTransforms();
    } else {
        RefreshTransforms(*entry);
        for (auto& obj : entry->instances) {
            obj->Update();
        }
    }
    return true;
}

/** @brief 配置物のローカル位置をseconds秒かけて目標へ補間移動させる（0以下なら即座） */
bool StageEditor::MoveObjectByName(const std::string& name, const Vector3& target, float seconds) {
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

/** @brief 配置物の有効/無効を上書きする（activationFlagより優先。レベルJSONには保存しない） */
bool StageEditor::SetObjectEnabledByName(const std::string& name, bool enabled) {
    ObjectEntry* entry = FindEntryByName(name);
    if (!entry) {
        return false;
    }
    entry->enabledOverride = enabled ? 1 : 0;
    return true;
}

} // namespace engine::game
