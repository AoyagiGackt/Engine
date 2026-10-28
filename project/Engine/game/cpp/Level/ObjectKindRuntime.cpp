/** @file ObjectKindRuntime.cpp
 * @brief 配置物の種類からランタイム実体へアクセスする操作
 */
#include "ObjectKind.h"
#include "Camera.h"
#include "EnemyEntity.h"
#include "EnemyRegistry.h"
#include "GameFlags.h"
#include "GameRules.h"
#include "GimmickMotion.h"
#include "KnightEnemy.h"
#include "LevelLoader.h"
#include "ModelCommon.h"
#include "Object3d.h"
#include "RunData.h"
#include "StageEditor.h"
#include "Weapon.h"
#include "WeaponManager.h"
#include <algorithm>
#include <iterator>
#include <memory>
#include <random>
#include <string>
#include <utility>
#ifdef USE_IMGUI
#include "DiagnosticsDraw.h"
#include "EditorUI.h"
#include "StageEditorUiStyle.h"
#include <cstring>
#include <imgui.h>
#endif

namespace engine::game {
using namespace engine::graphics;
/** @brief 実体の作り直し時に、敵の実体を用意する（敵を置かない種類は敵の実体を片付ける） */
void ObjectKind::PrepareEnemyInstance(StageEditor& editor, int index) const {
    ClearEnemyInstances(editor, index, false, false);
}

/** @brief 指定した配置物の設定を返す */
const ObjectDesc& ObjectKind::DescAt(const StageEditor& editor, int index) {
    return editor.objects_[index].desc;
}

/** @brief 指定した配置物の動きの経過時間を返す */
float ObjectKind::RuntimeTimer(const StageEditor& editor, int index) {
    return editor.objects_[index].runtimeTimer;
}

/** @brief 指定した配置物の動きの経過時間を書き換えられる形で返す */
float& ObjectKind::MutableRuntimeTimer(StageEditor& editor, int index) {
    return editor.objects_[index].runtimeTimer;
}

/** @brief ステージエディタのカメラを返す */
Camera* ObjectKind::EditorCamera(StageEditor& editor) {
    return editor.camera_;
}

/** @brief 設定の位置・回転・拡縮を実行中の実体へ反映し直す */
void ObjectKind::RefreshTransforms(StageEditor& editor, int index) {
    editor.RefreshTransforms(editor.objects_[index]);
}

/** @brief 配置物のワールド座標を返す（親があれば親の位置を足す） */
Vector3 ObjectKind::WorldPosition(const StageEditor& editor, const ObjectDesc& desc) {
    return editor.WorldPositionOf(desc);
}

/** @brief 指定した配置物が実行中に有効になっているかを返す */
bool ObjectKind::IsRuntimeActive(const StageEditor& editor, int index) {
    return editor.IsRuntimeActive(editor.objects_[index].desc);
}

/** @brief エディタで編集用の視点を使っているかを返す */
bool ObjectKind::IsEditingView(const StageEditor& editor) {
    return editor.visible_ && !editor.playTestMode_;
}

/** @brief 指定した配置物の敵の実体がもう作られているかを返す */
bool ObjectKind::HasEnemyInstance(const StageEditor& editor, int index) {
    const auto& entry = editor.objects_[index];
    return entry.knight || entry.enemy;
}

/** @brief 配置物の構造が変わった後に実体を作り直す */
void ObjectKind::RegenerateInstances(StageEditor& editor, int index) {
    editor.RegenerateInstances(editor.objects_[index]);
}

/** @brief 敵の実体を片付ける（keep～がtrueの方は残す） */
void ObjectKind::ClearEnemyInstances(StageEditor& editor, int index, bool keepKnight, bool keepBasic) {
    auto& entry = editor.objects_[index];
    if (!keepKnight) {
        entry.knight.reset();
    }
    if (!keepBasic) {
        editor.UnregisterEnemyEntity(entry);
        entry.enemy.reset();
    }
}

/** @brief ナイトの実体が無ければ作る */
void ObjectKind::EnsureKnightInstance(StageEditor& editor, int index) {
    auto& entry = editor.objects_[index];
    if (entry.knight) {
        return;
    }
    entry.enemyInstanceId = editor.AllocateEnemyInstanceId();
    entry.knight = std::make_unique<KnightEnemy>();
    entry.knight->Initialize(editor.modelCommon_, editor.WorldPositionOf(entry.desc));
}

namespace {
/**
 * @brief 敵に持たせる武器を決める（ラン中の道中の武器持ちは、プレイヤーがまだ持っていない候補から抽選。ボスと武器なしは配置どおり）
 * @note 持っている武器ばかり落ちると持ち替えが促せないので、まだ持っていない物を優先する（全部持っていれば候補全体から）
 */
WeaponType ChooseEnemyWeapon(const ObjectDesc& desc) {
    const WeaponType placed = ParseWeaponTypeName(desc.weaponType);
    const GameRulesData& rules = GameRules::GetInstance()->Get();
    if (!rules.randomEnemyWeapons || desc.weaponType.empty() || desc.isStageBoss || !RunData::GetInstance()->IsRunActive()
        || rules.enemyWeaponPool.empty()) {
        return placed;
    }
    std::vector<WeaponType> candidates;
    for (const std::string& name : rules.enemyWeaponPool) {
        const WeaponType type = ParseWeaponTypeName(name);
        if (!WeaponManager::GetInstance()->Owns(type)) {
            candidates.push_back(type);
        }
    }
    if (candidates.empty()) {
        for (const std::string& name : rules.enemyWeaponPool) {
            candidates.push_back(ParseWeaponTypeName(name));
        }
    }
    static std::mt19937 sRng { std::random_device { }() };
    std::uniform_int_distribution<size_t> pick(0, candidates.size() - 1);
    return candidates[pick(sRng)];
}
} // namespace

/** @brief 一般敵の実体が無ければ作り、EnemyRegistryへ登録する */
void ObjectKind::EnsureBasicEnemyInstance(StageEditor& editor, int index) {
    auto& entry = editor.objects_[index];
    if (entry.enemy) {
        return;
    }
    const ObjectDesc& desc = entry.desc;
    entry.enemyWeapon = ChooseEnemyWeapon(desc);
    entry.enemyInstanceId = editor.AllocateEnemyInstanceId();
    entry.enemy = std::make_unique<EnemyEntity>();
    entry.enemy->Initialize(editor.modelCommon_, editor.WorldPositionOf(desc), entry.enemyWeapon);
    entry.enemy->SetArchetype(desc.spawnType);
    entry.enemy->SetStatScales(desc.enemyHpScale, desc.enemyDamageScale, desc.enemySpeedScale);
    if (!desc.enemyModel.empty()) {
        entry.enemy->SetMonsterVisual(editor.modelCommon_, desc.enemyModel);
    }
    EnemyRegistry::GetInstance()->Register(desc.name, entry.enemy.get());
}

/** @brief 回収済みでなければ回収判定と演出を進める */
void ObjectKind::UpdatePickup(StageEditor& editor, int index, engine::graphics::ParticleManager* pm, const Vector3& playerPos) {
    auto& entry = editor.objects_[index];
    if (!entry.pickupCollected) {
        editor.UpdatePickupEntry(entry, pm, playerPos);
    }
}

/** @brief 壊れていなければ見た目を配置へ追従させる */
void ObjectKind::RefreshUnbrokenInstances(StageEditor& editor, int index) {
    auto& entry = editor.objects_[index];
    if (entry.breakableDestroyed) {
        return;
    }
    editor.RefreshTransforms(entry);
    for (auto& obj : entry.instances) {
        obj->Update();
    }
}

/** @brief 敵グループの敵が1体以上いて、全員倒されたか（出現待ちの敵がいれば未達） */
bool ObjectKind::IsEnemyGroupDefeated(const StageEditor& editor, const std::string& group) {
    bool foundEnemy = false;
    bool defeated = true;
    for (const auto& enemyEntry : editor.objects_) {
        if (enemyEntry.desc.enemyGroup != group) {
            continue;
        }
        if (enemyEntry.knight) {
            foundEnemy = true;
            defeated &= !enemyEntry.knight->IsAlive();
        } else if (enemyEntry.enemy) {
            foundEnemy = true;
            defeated &= enemyEntry.enemy->IsDefeated();
        } else if (Of(enemyEntry.desc.kind).SpawnsLater()) {
            // まだ出現していない敵がいる
            foundEnemy = true;
            defeated = false;
        }
    }
    return foundEnemy && defeated;
}

}
