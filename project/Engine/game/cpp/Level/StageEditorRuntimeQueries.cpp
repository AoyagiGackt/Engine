/**
 * @file StageEditorRuntimeQueries.cpp
 * @brief 配置物の描画・衝突形状・実行時情報の取得
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

std::vector<engine::AABB> StageEditor::GetSolidColliders() const
{
    std::vector<engine::AABB> result;
    for (const auto& entry : objects_) {
        if (!entry.desc.solid || !entry.runtimeActive || !entry.visibleOverride
            || entry.pickupCollected || entry.breakableDestroyed) {
            continue;
        }
        // solid指定のある敵でも、死体は床や壁としてプレイヤーを押し出さない。
        if ((entry.enemy && entry.enemy->IsDefeated()) || (entry.knight && !entry.knight->IsAlive())) {
            continue;
        }
        const ObjectDesc& desc = entry.desc;
        Vector3 basePos = WorldPositionOf(desc);
        Vector3 colliderRotation = desc.rotation;
        if (desc.kind == "gimmick") {
            // GetSolidColliders()はUpdateObjects()より前（このフレームのruntimeTimer加算前）に呼ばれるため、
            // そのまま使うと当たり判定が見た目より1フレーム遅れ、動く床に乗った瞬間に浮いて見える。
            // このフレーム分をあらかじめ足した時刻で計算し、見た目（UpdateRuntimeEntry側）と揃える
            const float motionDelta = ShouldPauseGame() ? GameConstants::kFrameDeltaTime : TimeManager::GetInstance()->GetDeltaTime();
            const float lookaheadTimer = entry.runtimeTimer + motionDelta;
            if (desc.gimmickMotion == "fall") {
                const FallingFloorBehavior::Snapshot state = FallingFloorBehavior::Evaluate(
                    lookaheadTimer, desc.motionSpeed, desc.motionAmount);
                if (!state.solid) {
                    continue;
                }
                basePos.y += state.yOffset;
            } else {
                const GimmickOffset offset = ComputeGimmickOffset(entry, lookaheadTimer);
                basePos = basePos + offset.position;
                colliderRotation = colliderRotation + offset.rotation;
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
                            (std::max)({ a.y, b.y, c.y }),
                            (std::max)({ a.z, b.z, c.z }) + kColliderThickness } });
                }
            }
            continue;
        }
        // 回転後の表示形状を含むワールドAABBを組み立てる
        const float hx = 0.5f * std::abs(desc.scale.x);
        const float hy = 0.5f * std::abs(desc.scale.y);
        const float hz = 0.5f * std::abs(desc.scale.z);
        const float cx = std::cos(colliderRotation.x), sx = std::sin(colliderRotation.x);
        const float cy = std::cos(colliderRotation.y), sy = std::sin(colliderRotation.y);
        const float cz = std::cos(colliderRotation.z), sz = std::sin(colliderRotation.z);
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

bool StageEditor::IsEntryDrawable(const ObjectEntry& entry) const
{
    if (!entry.runtimeActive || !entry.visibleOverride || entry.pickupCollected || entry.breakableDestroyed) {
        return false;
    }
    if (entry.desc.kind == "gimmick" && entry.desc.gimmickMotion == "blink"
        && std::sin(entry.runtimeTimer * entry.desc.motionSpeed) < 0.0f) {
        return false;
    }
    if (entry.desc.kind == "gimmick" && entry.desc.gimmickMotion == "fall"
        && !FallingFloorBehavior::Evaluate(entry.runtimeTimer, entry.desc.motionSpeed,
            entry.desc.motionAmount).visible) {
        return false;
    }
    return true;
}

void StageEditor::DrawObjectShadows()
{
    for (auto& entry : objects_) {
        if (!IsEntryDrawable(entry)) {
            continue;
        }
        for (auto& obj : entry.instances) {
            obj->DrawShadow();
        }
    }
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
        if (!IsEntryDrawable(entry)) {
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
    auto isDrawableText = [](const ObjectEntry& entry) {
        return entry.desc.kind == "ui_text" && entry.runtimeActive && entry.visibleOverride && !entry.desc.text.empty();
    };
    // 画面固定の文章が同じ位置に複数有効なときは、後に置いたもの（進行の先の案内）だけを出す。
    // 区画ごとの案内を同じ場所に並べても、前の区画の文章と重なって読めなくならないようにする
    constexpr float kSameAnchorEpsilon = 0.5f;
    auto isReplacedByLaterText = [&](size_t index) {
        const ObjectDesc& desc = objects_[index].desc;
        if (desc.textSpace == "world") {
            return false;
        }
        for (size_t later = index + 1; later < objects_.size(); ++later) {
            const ObjectEntry& other = objects_[later];
            if (!isDrawableText(other) || other.desc.textSpace == "world") {
                continue;
            }
            if (std::abs(other.desc.position.x - desc.position.x) < kSameAnchorEpsilon
                && std::abs(other.desc.position.y - desc.position.y) < kSameAnchorEpsilon) {
                return true;
            }
        }
        return false;
    };
    for (size_t index = 0; index < objects_.size(); ++index) {
        const ObjectEntry& entry = objects_[index];
        const ObjectDesc& desc = entry.desc;
        if (!isDrawableText(entry) || isReplacedByLaterText(index)) {
            continue;
        }
        const Vector3 pos = WorldPositionOf(desc);
        float screenX = pos.x;
        float screenY = pos.y;
        if (desc.textSpace == "world") {
            SceneShared::WorldToScreen(pos.x, pos.y, camX, camY, screenX, screenY);
        }
        const auto text = StringUtility::ConvertString(desc.text);
        constexpr Vector4 kTextShadowColor = { 0.02f, 0.025f, 0.04f, 0.9f }; // アルファは本文の不透明度に掛ける
        if (desc.textShadow) {
            const float offset = (std::max)(1.0f, desc.textScale);
            font.DrawStringW(text, screenX + offset, screenY + offset,
                desc.textScale, { kTextShadowColor.x, kTextShadowColor.y, kTextShadowColor.z, desc.textColor.w * kTextShadowColor.w }, desc.textBold);
        }
        font.DrawStringW(text, screenX, screenY,
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
