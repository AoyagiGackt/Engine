/**
 * @file StageEditorRuntimeBehavior.cpp
 * @brief 配置物・敵・イベントの実行時更新
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
#include <cmath>
#include <random>
using namespace engine::game;
using namespace engine;
using namespace engine::graphics;

namespace {
// 回復役の敵の演出
constexpr Vector3 kHealerEffectOffset = { 0.0f, 0.7f, 0.0f }; // 足元から演出を出す位置
constexpr Vector3 kHealedAllyEffectOffset = { 0.0f, 0.65f, 0.0f };
constexpr Vector4 kHealInterruptedColor = { 1.0f, 0.9f, 0.35f, 1.0f };
constexpr float kCastRingRangeRatio = 0.5f; // 詠唱中のリングを回復範囲のどれだけまで広げるか
constexpr Vector4 kCastRingColor = { 0.3f, 1.0f, 0.4f, 0.4f };
constexpr int kCastRingCount = 16;
constexpr float kCastRingSize = 0.16f;
constexpr Vector4 kPulseRingColor = { 0.2f, 1.0f, 0.35f, 0.75f };
constexpr int kPulseRingCount = 24;
constexpr float kPulseRingLifetime = 0.65f;
constexpr float kPulseRingSize = 0.22f;
constexpr float kAllyRingSpeed = 1.4f;
constexpr Vector4 kAllyRingColor = { 0.25f, 1.0f, 0.4f, 0.9f };
constexpr int kAllyRingCount = 12;
constexpr float kAllyRingLifetime = 0.55f;
constexpr float kAllyRingSize = 0.16f;
constexpr Vector4 kAllyBurstColor = { 0.55f, 1.0f, 0.65f, 0.85f };
constexpr int kAllyBurstCount = 8;
constexpr float kAllyBurstLifetime = 0.7f;
constexpr float kAllyBurstSize = 0.12f;
constexpr Vector4 kHealerBurstColor = { 0.4f, 1.0f, 0.55f, 1.0f };
constexpr int kHealerBurstCount = 14;
constexpr float kHealerBurstLifetime = 1.0f;
constexpr float kHealerBurstSize = 0.18f;
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
            const FallingFloorBehavior::Snapshot fallState = FallingFloorBehavior::Evaluate(entry.runtimeTimer,
                entry.desc.motionSpeed, entry.desc.motionAmount);
            entry.desc.position.y += fallState.yOffset;
            const Vector3 halfExtent = { 0.5f * std::abs(entry.desc.scale.x),
                0.5f * std::abs(entry.desc.scale.y), 0.5f * std::abs(entry.desc.scale.z) };
            FallingFloorBehavior::EmitDebris(pm, WorldPositionOf(entry.desc), halfExtent, fallState, entry.fallFloorWasSolid);
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
                        pm->EmitHitStar("hit_spark", entry.enemy->GetPosition() + kHealerEffectOffset,
                            kHealInterruptedColor);
                    }
                } else {
                    entry.runtimeTimer += GameConstants::kFrameDeltaTime;
                    if (!entry.healChanneling && entry.runtimeTimer >= kHealerPulseSeconds - kHealerCastSeconds) {
                        // 詠唱開始。ここから完了までの間に被弾すると上のブロックで中断される
                        entry.healChanneling = true;
                        entry.healChannelHpAtStart = hpNow;
                        if (pm) {
                            pm->EmitRing("heal_pulse", entry.enemy->GetPosition() + kHealerEffectOffset,
                                kHealerRange * kCastRingRangeRatio, kCastRingColor, kCastRingCount, kHealerCastSeconds, kCastRingSize);
                        }
                    }
                    if (entry.runtimeTimer >= kHealerPulseSeconds) {
                        entry.runtimeTimer = 0.0f;
                        entry.healChanneling = false;
                        const Vector3 healerPos = entry.enemy->GetPosition();
                        // 回復対象のHP状態にかかわらず、回復行動そのものを緑の波で知らせる。
                        if (pm) {
                            pm->EmitRing("heal_pulse", healerPos + kHealerEffectOffset,
                                kHealerRange, kPulseRingColor, kPulseRingCount, kPulseRingLifetime, kPulseRingSize);
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
                                        const Vector3 effectPos = allyPos + kHealedAllyEffectOffset;
                                        pm->EmitRing("heal_pulse", effectPos, kAllyRingSpeed,
                                            kAllyRingColor, kAllyRingCount, kAllyRingLifetime, kAllyRingSize);
                                        pm->EmitBurst("heal_pulse", effectPos,
                                            kAllyBurstColor, kAllyBurstCount, kAllyBurstLifetime, kAllyBurstSize, true);
                                    }
                                }
                            }
                        }
                        if (healedAnyAlly && pm) {
                            pm->EmitBurst("heal_pulse", healerPos + kHealerEffectOffset,
                                kHealerBurstColor, kHealerBurstCount, kHealerBurstLifetime, kHealerBurstSize, true);
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
        const float step = (std::max)(0.0f, entry.desc.patrolSpeed) * GameConstants::kFrameDeltaTime;
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
