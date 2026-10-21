/**
 * @file StageEditorRuntimeBehavior.cpp
 * @brief 配置物・敵・イベントの実行時更新
 */
#include "StageEditor.h"
#include "CombatTuning.h"
#include "SceneVisualTuning.h"
#include "GimmickMotion.h"
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
using namespace engine::graphics;

namespace engine::game {

/** @brief グラフのMoveObjectによる補間移動を1フレーム進める */
void StageEditor::UpdateGraphMove(ObjectEntry& entry, float dt)
{
    if (!entry.graphMoveActive)
    {
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
    if (t >= 1.0f)
    {
        entry.graphMoveActive = false;
    }
}

/** @brief pickup配置物の回収判定と演出（プレイヤーが半径内に入ったら回収し、覚醒ゲージを増やす） */
void StageEditor::UpdatePickupEntry(ObjectEntry& entry, ParticleManager* pm, const Vector3& playerPos)
{

    const ObjectDesc& desc = entry.desc;
    const Vector3 worldPos = WorldPositionOf(desc);
    if (!ShouldPauseGame())
    {
        const float dx = playerPos.x - worldPos.x;
        const float dy = playerPos.y - worldPos.y;
        if (dx * dx + dy * dy <= desc.pickupRadius * desc.pickupRadius)
        {
            entry.pickupCollected = true;
            GameFlags::GetInstance()->SetFlag("pickup_" + desc.name, true);
            if (Player* player = PlayerBridge::GetInstance()->Get())
            {
                player->ChargeAwakenGauge(desc.pickupGaugeAmount);
            }
            if (pm)
            {
                pm->EmitRing("awaken_aura", worldPos, SceneVisualTuning::Level().pickupRingRadius, desc.pickupColor,
                    SceneVisualTuning::Level().pickupRingCount, SceneVisualTuning::Level().pickupRingLifetime, SceneVisualTuning::Level().pickupRingSize);
            }
            /** @brief シングルトンインスタンスを返す（初回呼び出しで生成される） */
            ScreenFlash::GetInstance()->Request(
                { desc.pickupColor.x, desc.pickupColor.y, desc.pickupColor.z, SceneVisualTuning::Level().pickupFlashAlpha }, SceneVisualTuning::Level().pickupFlashSeconds);
            return;
        }
    }

    // 未回収の間は回転と脈動で拾える物だと分かるようにする（位置は親子追従のためRefreshTransformsで先に反映する）
    RefreshTransforms(entry);
    const float pulse = 1.0f - SceneVisualTuning::Level().pickupPulseAmplitude + std::sin(entry.runtimeTimer * SceneVisualTuning::Level().pickupPulseSpeed) * SceneVisualTuning::Level().pickupPulseAmplitude;
    for (auto& obj : entry.instances)
    {
        obj->SetRotation({ desc.rotation.x, desc.rotation.y + entry.runtimeTimer * SceneVisualTuning::Level().pickupSpinSpeed, desc.rotation.z });
        obj->SetScale(desc.scale * pulse);
        obj->SetColor({ desc.pickupColor.x, desc.pickupColor.y * pulse, desc.pickupColor.z, desc.pickupColor.w });
        obj->Update();
    }
}

/** @brief ノーコード条件を評価して対応するゲームフラグへ反映する */
void StageEditor::EvaluateEventConditions(float dt)
{
    // ノーコード条件を先に評価し、接続先が参照するゲームフラグへ反映する
    for (int index = 0; index < static_cast<int>(objects_.size()); ++index)
    {
        if (objects_[index].desc.enabled)
        {
            ObjectKind::Of(objects_[index].desc.kind).UpdateCondition(*this, index, dt);
        }
    }
}

/** @brief 有効化フラグと遅延から全配置物の実行状態を更新する */
void StageEditor::UpdateRuntimeActivation(float dt)
{
    // 対象ごとの遅延を評価する。有効化は成立後に待ち、無効化は成立後も遅延中だけ維持する
    for (auto& entry : objects_)
    {
        const ObjectDesc& desc = entry.desc;
        if (!desc.enabled)
        {
            entry.runtimeActive = false;
            continue;
        }
        // グラフのSetObjectEnabledによる上書きは有効化フラグより優先する
        if (entry.enabledOverride >= 0)
        {
            entry.runtimeActive = entry.enabledOverride == 1;
            if (entry.runtimeActive)
            {
                entry.runtimeTimer += dt;
            }
            continue;
        }
        // フラグに結び付いていない物は常に有効（時間で動く種類だけタイマーを進める）
        if (desc.activationFlag.empty())
        {
            entry.runtimeActive = true;
            if (ObjectKind::Of(desc.kind).AlwaysAdvancesTimer())
            {
                entry.runtimeTimer += dt;
            }
            continue;
        }
        // フラグが立っている間だけタイマーを進め、遅延を過ぎたかで有効/無効を決める
        const bool flagValue = GameFlags::GetInstance()->GetFlag(desc.activationFlag);
        if (flagValue)
        {
            entry.runtimeTimer += dt;
        }
        else
        {
            entry.runtimeTimer = 0.0f;
        }
        entry.runtimeActive = desc.activeWhenFlag
            ? flagValue && entry.runtimeTimer >= desc.activationDelay
            : !flagValue || entry.runtimeTimer < desc.activationDelay;
        // 種類ごとの終了条件（一度きりの動きを終えた等）を満たしたら止める
        if (!ObjectKind::Of(desc.kind).StaysActive(desc, entry.runtimeTimer))
        {
            entry.runtimeActive = false;
        }
    }
}

/** @brief 実行中の配置物一件へ種別固有の更新を適用する */
void StageEditor::UpdateRuntimeEntry(ObjectEntry& entry, ParticleManager* pm,
    const Vector3& playerPos, float dt)
{
    // 有効な物だけを動かす。種類ごとの処理 → 敵 → グラフ移動 → 種類独自の処理 の順に任せる
    if (!entry.runtimeActive)
    {
        return;
    }
    const ObjectKind& kind = ObjectKind::Of(entry.desc.kind);
    const int index = IndexOf(entry);
    // 種類が自分で処理を終えたら、以降の共通処理は行わない
    if (kind.UpdateBeforeEnemy(*this, index, dt))
    {
        return;
    }
    // 敵の実体を持つ配置物は敵の更新だけを行う
    if (entry.knight || entry.enemy)
    {
        UpdateEnemyEntry(entry, pm, playerPos);
        return;
    }
    // グラフから指示された移動は、ゲームが止まっている間は進めない
    if (!ShouldPauseGame())
    {
        UpdateGraphMove(entry, dt);
    }
    // 種類独自の処理で完結した場合も、ギミックの動きは適用しない
    if (kind.UpdateOwnRuntime(*this, index, pm, playerPos))
    {
        return;
    }

    // 子も一緒に回す仕掛けの子は、親側で同じ一時回転中に更新する。ここで更新し直すと元位置へ戻ってしまう。
    if (!entry.desc.parent.empty())
    {
        for (const auto& candidate : objects_)
        {
            const GimmickMotion* parentMotion = ObjectKind::Of(candidate.desc.kind).MotionOf(candidate.desc);
            if (candidate.desc.name == entry.desc.parent && parentMotion && parentMotion->CarriesChildren())
            {
                return;
            }
        }
    }

    // 一時的なギミック変形だけを描画実体へ渡し、保存対象の編集値は維持する
    const Vector3 authoredPosition = entry.desc.position;
    const Vector3 authoredRotation = entry.desc.rotation;
    const GimmickMotion* motion = kind.MotionOf(entry.desc);
    if (motion)
    {
        // 経過時間から今のずれを求め、編集値に一時的に足す
        const GimmickPose pose = motion->Evaluate(entry.desc, entry.runtimeTimer);
        entry.desc.position = entry.desc.position + pose.position;
        entry.desc.rotation = entry.desc.rotation + pose.rotation;
        // 動きに合わせた演出がある種類なら、今の位置で出す
        motion->EmitEffects(pm, entry.desc, WorldPositionOf(entry.desc), entry.runtimeTimer, entry.fallFloorWasSolid);
    }
    // ずらした値で描画用の行列を作る
    RefreshTransforms(entry);
    // 子を一緒に運ぶ動きなら、親がずれている今のうちに子の行列も作り直す
    if (motion && motion->CarriesChildren())
    {
        for (auto& child : objects_)
        {
            if (child.desc.parent == entry.desc.name)
            {
                RefreshTransforms(child);
                for (auto& obj : child.instances)
                {
                    obj->Update();
                }
            }
        }
    }
    // 保存用の編集値を元に戻し、描画用の行列を更新する
    entry.desc.position = authoredPosition;
    entry.desc.rotation = authoredRotation;
    for (auto& obj : entry.instances)
    {
        obj->Update();
    }
}

/** @brief enemy系エントリ1つぶんの毎フレーム処理 */
void StageEditor::UpdateEnemyEntry(ObjectEntry& entry, ParticleManager* pm, const Vector3& playerPos)
{
    // 詠唱（回復発動までのタメ）の間に被弾すると中断できる。数値はenemy_params.jsonのbasic節で調整する
    const BasicEnemyTuning& tuning = EnemyTuning::GetInstance()->Basic();
    const float kHealerPulseSeconds = tuning.healerPulseSeconds;
    const float kHealerCastSeconds = tuning.healerCastSeconds;
    const float kHealerRange = tuning.healerRange;
    const int kHealerAmount = tuning.healerAmount;
    Vector3 worldPos = WorldPositionOf(entry.desc);
    // 編集中と、シーンが進行を止めている間（ポーズ・開始演出）は敵のAIと重力を進めない
    const bool holdEnemy = ShouldPauseGame() || gameplayHeld_;
    if (!holdEnemy && UpdatePatrol(entry))
    {
        return;
    }
    if (entry.knight)
    {
        if (holdEnemy)
        {
            // 編集中はAI/重力を進めず、desc.positionへドラッグされた位置だけ反映する
            entry.knight->SetPosition(worldPos);
            entry.knight->RefreshVisualTransforms();
        }
        else
        {
            entry.knight->Update(pm, playerPos);
            // Inspector表示・親子追従の基準にするため現在地を書き戻す（JSON保存はしない）
            entry.desc.position = entry.knight->GetPosition();
        }
    }
    else if (entry.enemy)
    {
        if (holdEnemy)
        {
            entry.enemy->SetPosition(worldPos);
            entry.enemy->FaceTowardX(playerPos.x);
            entry.enemy->RefreshVisualTransforms();
        }
        else
        {
            entry.enemy->Update(playerPos.x);
            // 宙で倒れた敵は、真下の床まで落とす（床探しは倒れた時に一度だけ）
            if (entry.enemy->NeedsCorpseFloor())
            {
                entry.enemy->DropCorpseTo(FindCorpseFloorY(entry.enemy->GetPosition()));
            }
            entry.desc.position = entry.enemy->GetPosition();
            if (entry.enemy->IsHealer() && !entry.enemy->IsDefeated())
            {
                const int hpNow = entry.enemy->GetHp();
                if (entry.healChanneling && hpNow < entry.healChannelHpAtStart)
                {
                    // 詠唱中にHPが減った＝回復を中断された。サイクルを最初からやり直させる
                    // （プレイヤーの直接攻撃に限らず、爆発バレルの爆風などダメージ源は問わない仕様）
                    entry.healChanneling = false;
                    entry.runtimeTimer = 0.0f;
                    if (pm)
                    {
                        pm->EmitHitStar("hit_spark", entry.enemy->GetPosition() + SceneVisualTuning::Level().healerEffectOffset,
                            SceneVisualTuning::Level().healInterruptedColor);
                    }
                }
                else
                {
                    entry.runtimeTimer += GameConstants::kFrameDeltaTime;
                    if (!entry.healChanneling && entry.runtimeTimer >= kHealerPulseSeconds - kHealerCastSeconds)
                    {
                        // 詠唱開始。ここから完了までの間に被弾すると上のブロックで中断される
                        entry.healChanneling = true;
                        entry.healChannelHpAtStart = hpNow;
                        if (pm)
                        {
                            pm->EmitRing("heal_pulse", entry.enemy->GetPosition() + SceneVisualTuning::Level().healerEffectOffset,
                                kHealerRange * SceneVisualTuning::Level().castRingRangeRatio, SceneVisualTuning::Level().castRingColor, SceneVisualTuning::Level().castRingCount, kHealerCastSeconds, SceneVisualTuning::Level().castRingSize);
                        }
                    }
                    if (entry.runtimeTimer >= kHealerPulseSeconds)
                    {
                        entry.runtimeTimer = 0.0f;
                        entry.healChanneling = false;
                        const Vector3 healerPos = entry.enemy->GetPosition();
                        // 回復対象のHP状態にかかわらず、回復行動そのものを緑の波で知らせる。
                        if (pm)
                        {
                            pm->EmitRing("heal_pulse", healerPos + SceneVisualTuning::Level().healerEffectOffset,
                                kHealerRange, SceneVisualTuning::Level().pulseRingColor, SceneVisualTuning::Level().pulseRingCount, SceneVisualTuning::Level().pulseRingLifetime, SceneVisualTuning::Level().pulseRingSize);
                        }
                        bool healedAnyAlly = false;
                        for (auto& ally : objects_)
                        {
                            if (!ally.enemy || ally.enemy.get() == entry.enemy.get() || ally.enemy->IsDefeated())
                            {
                                continue;
                            }
                            const Vector3 allyPos = ally.enemy->GetPosition();
                            const float dx = allyPos.x - healerPos.x;
                            const float dy = allyPos.y - healerPos.y;
                            if (dx * dx + dy * dy <= kHealerRange * kHealerRange)
                            {
                                const int hpBefore = ally.enemy->GetHp();
                                ally.enemy->Heal(kHealerAmount);
                                if (ally.enemy->GetHp() > hpBefore)
                                {
                                    healedAnyAlly = true;
                                    if (pm)
                                    {
                                        const Vector3 effectPos = allyPos + SceneVisualTuning::Level().healedAllyEffectOffset;
                                        pm->EmitRing("heal_pulse", effectPos, SceneVisualTuning::Level().allyRingSpeed,
                                            SceneVisualTuning::Level().allyRingColor, SceneVisualTuning::Level().allyRingCount, SceneVisualTuning::Level().allyRingLifetime, SceneVisualTuning::Level().allyRingSize);
                                        pm->EmitBurst("heal_pulse", effectPos,
                                            SceneVisualTuning::Level().allyBurstColor, SceneVisualTuning::Level().allyBurstCount, SceneVisualTuning::Level().allyBurstLifetime, SceneVisualTuning::Level().allyBurstSize, true);
                                    }
                                }
                            }
                        }
                        if (healedAnyAlly && pm)
                        {
                            pm->EmitBurst("heal_pulse", healerPos + SceneVisualTuning::Level().healerEffectOffset,
                                SceneVisualTuning::Level().healerBurstColor, SceneVisualTuning::Level().healerBurstCount, SceneVisualTuning::Level().healerBurstLifetime, SceneVisualTuning::Level().healerBurstSize, true);
                        }
                    }
                }
            }
        }
    }
}

/** @brief 敵に設定された巡回ルートへ沿って位置を更新する */
bool StageEditor::UpdatePatrol(ObjectEntry& entry)
{
    // 巡回ルートを持たない敵は巡回しない（false を返して通常の動きに任せる）
    if (entry.desc.patrolRoute.empty())
    {
        return false;
    }
    // 同じルート名の有効な巡回地点を集め、順番の値で並べる
    std::vector<const ObjectDesc*> points;
    for (const auto& candidate : objects_)
    {
        if (candidate.desc.enabled && candidate.desc.kind == ObjectKindName::kPatrolPoint
            && candidate.desc.patrolRoute == entry.desc.patrolRoute)
        {
            points.push_back(&candidate.desc);
        }
    }
    if (points.empty())
    {
        return false;
    }
    std::sort(points.begin(), points.end(), [](const ObjectDesc* lhs, const ObjectDesc* rhs) {
        return lhs->routeOrder < rhs->routeOrder;
    });
    // 今向かっている地点に着いたら次の地点へ、まだなら巡回速度で近づく
    entry.patrolTargetIndex %= static_cast<int>(points.size());
    const Vector3 target = WorldPositionOf(*points[entry.patrolTargetIndex]);
    IEnemyEntity* enemy = entry.EnemyBody();
    Vector3 position = enemy->GetPosition();
    const float dx = target.x - position.x;
    const float dy = target.y - position.y;
    const float dz = target.z - position.z;
    const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (distance <= CombatTune().waypointArrivalDistance)
    {
        entry.patrolTargetIndex = (entry.patrolTargetIndex + 1) % static_cast<int>(points.size());
    }
    else
    {
        const float step = (std::max)(0.0f, entry.desc.patrolSpeed) * GameConstants::kFrameDeltaTime;
        const float ratio = (std::min)(1.0f, step / distance);
        position.x += dx * ratio;
        position.y += dy * ratio;
        position.z += dz * ratio;
    }
    // 敵本体と配置情報の両方へ位置を書き戻し、見た目の行列を更新する
    enemy->SetPosition(position);
    entry.desc.position = position;
    enemy->RefreshVisualTransforms();
    return true;
}

} // namespace engine::game
