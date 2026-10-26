/**
 * @file StageEditorRuntimeQueries.cpp
 * @brief 配置物の描画・衝突形状・実行時情報の取得
 */
#include "StageEditor.h"
#include "GimmickMotion.h"
#include "Camera.h"
#include "DirectXCommon.h"
#include "EnemyEntity.h"
#include "EnemyRegistry.h"
#include "EnemyTuning.h"
#include "FontRenderer.h"
#include "GameConstants.h"
#include "GameFlags.h"
#include "Input.h"
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
#include <set>
using namespace engine::graphics;

namespace engine::game {
namespace {
/**
 * @brief 点を X → Y → Z の順にオイラー角で回す（配置物の回転と同じ順）
 * @param value 回す点
 * @param rotation 各軸の回転角（ラジアン）
 */
Vector3 RotateEulerXYZ(const Vector3& value, const Vector3& rotation) {
    const float cx = std::cos(rotation.x), sx = std::sin(rotation.x);
    const float cy = std::cos(rotation.y), sy = std::sin(rotation.y);
    const float cz = std::cos(rotation.z), sz = std::sin(rotation.z);
    Vector3 result = value;
    result = { result.x, result.y * cx - result.z * sx, result.y * sx + result.z * cx };
    result = { result.x * cy + result.z * sy, result.y, -result.x * sy + result.z * cy };
    return { result.x * cz - result.y * sz, result.x * sz + result.y * cz, result.z };
}
} // namespace

/** @brief 描画資源の所有権を保持したまま色と描画行列を更新する */
void BreakableRef::SetColor(const Vector4& color) const {
    object_->SetColor(color);
    object_->Update();
}

/** @brief solid=trueのオブジェクトのワールドAABB一覧を返す（毎フレーム呼ぶ想定） */
std::vector<engine::AABB> StageEditor::GetSolidColliders() const {
    return CollectSolidColliders(true);
}

/** @brief solid=trueの配置物のワールドAABB一覧（includeEnemyBodies=falseなら敵の体を除いた地形だけ） */
std::vector<engine::AABB> StageEditor::CollectSolidColliders(bool includeEnemyBodies) const {
    // 当たり判定を持ち、今ゲーム中で有効な（回収・破壊されていない）配置物だけを対象にする
    std::vector<engine::AABB> result;
    for (const auto& entry : objects_) {
        if (!entry.desc.solid || !entry.runtimeActive || !entry.visibleOverride
            || entry.pickupCollected || entry.breakableDestroyed) {
            continue;
        }
        // 地形だけが欲しい時は敵の体を除く
        if (!includeEnemyBodies && (entry.enemy || entry.knight)) {
            continue;
        }
        // solid指定のある敵でも、死体は床や壁としてプレイヤーを押し出さない。
        if ((entry.enemy && entry.enemy->IsDefeated()) || (entry.knight && !entry.knight->IsAlive())) {
            continue;
        }
        const ObjectDesc& desc = entry.desc;
        Vector3 basePos = WorldPositionOf(desc);
        Vector3 colliderRotation = desc.rotation;
        if (const GimmickMotion* motion = ObjectKind::Of(desc.kind).MotionOf(desc)) {
            // GetSolidColliders()はUpdateObjects()より前（このフレームのruntimeTimer加算前）に呼ばれるため、
            // そのまま使うと当たり判定が見た目より1フレーム遅れ、動く床に乗った瞬間に浮いて見える。
            // このフレーム分をあらかじめ足した時刻で計算し、見た目（UpdateRuntimeEntry側）と揃える
            const float motionDelta = ShouldPauseGame()
                ? gameConstants::kFrameDeltaTime
                : (gameplayHeld_ ? 0.0f : TimeManager::GetInstance()->GetDeltaTime());
            const GimmickPose pose = motion->Evaluate(desc, entry.runtimeTimer + motionDelta);
            if (!pose.solid) {
                continue;
            }
            basePos = basePos + pose.position;
            colliderRotation = colliderRotation + pose.rotation;
        }
        // Terrainは描画メッシュの各三角形をAABBへ変換し、表示形状の変更と同期する
        if (desc.kind == objectKindName::kTerrain && desc.meshCollider) {
            const std::string key = desc.model + '|' + desc.texture;
            auto modelIt = modelCache_.find(key);
            if (modelIt != modelCache_.end()) {
                const auto& vertices = modelIt->second->GetVertices();
                const auto& indices = modelIt->second->GetIndices();
                // 頂点を拡縮 → 回転 → 配置位置への移動の順でワールド座標にする
                auto transformVertex = [&](const Vector4& vertex) {
                    const Vector3 scaled = { vertex.x * desc.scale.x, vertex.y * desc.scale.y, vertex.z * desc.scale.z };
                    return RotateEulerXYZ(scaled, desc.rotation) + basePos;
                };
                // インデックスは3つで1枚の三角形
                constexpr size_t kTriangleVertexCount = 3;
                for (size_t i = 0; i + kTriangleVertexCount <= indices.size(); i += kTriangleVertexCount) {
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
        // 列配置の各実体ごとに、回した箱の8頂点を囲む軸平行の箱を当たり判定にする
        int instanceCount = static_cast<int>(entry.instances.size());
        for (int i = 0; i < instanceCount; ++i) {
            const Vector3 pos = PlacementInstancePosition(desc, basePos, i);
            Vector3 minCorner = { FLT_MAX, FLT_MAX, FLT_MAX };
            Vector3 maxCorner = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
            for (float x : { -hx, hx }) {
                for (float y : { -hy, hy }) {
                    for (float z : { -hz, hz }) {
                        const Vector3 corner = pos + RotateEulerXYZ({ x, y, z }, colliderRotation);
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

/** @brief kind=="breakable"で未破壊の配置物一覧を返す（毎フレーム呼ぶ想定） */
std::vector<BreakableRef> StageEditor::GetBreakables() {
    std::vector<BreakableRef> result;
    for (auto& entry : objects_) {
        if (entry.desc.kind != objectKindName::kBreakable || !entry.runtimeActive || entry.breakableDestroyed || entry.instances.empty()) {
            continue;
        }
        result.emplace_back(entry.desc, WorldPositionOf(entry.desc), entry.breakableHp, entry.breakableDestroyed, entry.instances.front().get());
    }
    return result;
}

/** @brief kind=="pickup"の回収状況を返す（HUDのx / y表示用） */
void StageEditor::GetPickupCounts(int& outCollected, int& outTotal) const {
    outCollected = 0;
    outTotal = 0;
    for (const auto& entry : objects_) {
        if (entry.desc.kind != objectKindName::kPickup || !entry.desc.enabled) {
            continue;
        }
        ++outTotal;
        if (entry.pickupCollected) {
            ++outCollected;
        }
    }
}

/** @brief 配置物を今フレーム描くか（回収済み・破壊済み・点滅で消えている間・落下中の床などは描かない） */
bool StageEditor::IsEntryDrawable(const ObjectEntry& entry) const {
    if (!entry.runtimeActive || !entry.visibleOverride || entry.pickupCollected || entry.breakableDestroyed) {
        return false;
    }
    const GimmickMotion* motion = ObjectKind::Of(entry.desc.kind).MotionOf(entry.desc);
    return motion == nullptr || motion->Evaluate(entry.desc, entry.runtimeTimer).visible;
}

/** @brief ナイト型・汎用のどちらであっても敵の本体を返す（敵でなければ nullptr） */
IEnemyEntity* StageEditor::ObjectEntry::EnemyBody() const {
    if (knight) {
        return knight.get();
    }
    return enemy.get();
}

/** @brief 今フレーム描く配置物すべてに visit を呼ぶ */
void StageEditor::VisitDrawableEntries(const std::function<void(ObjectEntry&)>& visit) {
    for (auto& entry : objects_) {
        if (IsEntryDrawable(entry)) {
            visit(entry);
        }
    }
}

/** @brief 配置物の見た目モデルをシャドウマップへ描く */
void StageEditor::DrawObjectShadows() {
    VisitDrawableEntries([](ObjectEntry& entry) {
        for (auto& obj : entry.instances) {
            obj->DrawShadow();
        }
    });
}

/** @brief 生成済みオブジェクトを3D描画パスへ描画する */
void StageEditor::DrawObjects() {
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

    // 見た目のモデルと、敵の本体を描く
    VisitDrawableEntries([](ObjectEntry& entry) {
        for (auto& obj : entry.instances) {
            obj->Draw();
        }
        if (entry.knight) {
            entry.knight->Draw();
        }
        if (entry.enemy) {
            entry.enemy->Draw();
        }
    });
}

/** @brief kind=="ui_text"の配置物をFontRendererへ描画コマンドとして積む */
void StageEditor::DrawUIText(FontRenderer& font) const {
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
        return entry.desc.kind == objectKindName::kUIText && entry.runtimeActive && entry.visibleOverride && !entry.desc.text.empty();
    };
    // 画面固定の文章が同じ位置に複数有効なときは、後に置いたもの（進行の先の案内）だけを出す。
    // 区画ごとの案内を同じ場所に並べても、前の区画の文章と重なって読めなくならないようにする
    constexpr float kSameAnchorEpsilon = 0.5f;
    auto isReplacedByLaterText = [&](size_t index) {
        const ObjectDesc& desc = objects_[index].desc;
        if (desc.textSpace == TextSpace::kWorld) {
            return false;
        }
        for (size_t later = index + 1; later < objects_.size(); ++later) {
            const ObjectEntry& other = objects_[later];
            if (!isDrawableText(other) || other.desc.textSpace == TextSpace::kWorld) {
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
        if (desc.textSpace == TextSpace::kWorld) {
            sceneShared::WorldToScreen(pos.x, pos.y, camX, camY, screenX, screenY);
        }
        // {Steal} などの操作名は、最後に使った機器（キーボード/パッド）のボタン名へ置き換える
        const std::wstring rawText = StringUtility::ConvertString(desc.text);
        const Input* input = Input::GetCurrent();
        const std::wstring text = input ? input->ExpandPrompts(rawText) : rawText;
        // 明るい空やブロックの上でも読めるよう、文章の後ろに半透明の暗い板を敷く
        constexpr float kPanelPadding = 8.0f;
        constexpr Vector4 kPanelColor = { 0.0f, 0.0f, 0.05f, 0.6f }; // アルファは本文の不透明度に掛ける
        const Vector2 textSize = FontRenderer::MeasureStringW(text, desc.textScale);
        font.DrawPanel(screenX - kPanelPadding, screenY - kPanelPadding, textSize.x + kPanelPadding * 2.0f, textSize.y + kPanelPadding * 2.0f,
            { kPanelColor.x, kPanelColor.y, kPanelColor.z, kPanelColor.w * desc.textColor.w });
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

/** @brief 配置済みのナイト敵一覧を返す */
std::vector<KnightEnemy*> StageEditor::GetKnights() {
    std::vector<KnightEnemy*> result;
    for (auto& entry : objects_) {
        if (entry.knight && entry.runtimeActive) {
            result.push_back(entry.knight.get());
        }
    }
    return result;
}

/** @brief 武器種別が設定されたkind=="enemy_basic"の配置物一覧を返す（毎フレーム呼ぶ想定） */
std::vector<CombatEnemyRef> StageEditor::GetCombatEnemies() const {
    std::vector<CombatEnemyRef> result;
    for (const auto& entry : objects_) {
        // 直置きの敵に加えて、spawn_pointから生成された敵も戦闘対象にする（出現制御をレベル側で組めるように）
        const bool combatKind = ObjectKind::Of(entry.desc.kind).SpawnsCombatEnemy();
        // 武器持ちの敵に加え、武器を持たないモンスターも戦闘対象にする（武器なしの騎士だけは従来どおり対象外）
        const bool hasWeapon = !entry.desc.weaponType.empty();
        if (!entry.enemy || !entry.runtimeActive || !combatKind || (!hasWeapon && !entry.enemy->IsMonster())) {
            continue;
        }
        CombatEnemyRef ref;
        ref.name = entry.desc.name;
        ref.weaponType = entry.enemyWeapon;
        ref.hasWeapon = hasWeapon;
        ref.isStageBoss = entry.desc.isStageBoss;
        ref.enemy = entry.enemy.get();
        result.push_back(ref);
    }
    return result;
}


/** @brief 空の名前・重複した名前に一意な自動名を振る（Open直後に呼ぶ） */
void StageEditor::EnsureUniqueNames() {
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

/** @brief ワールド位置 world を desc の親から見た位置へ直す（ドラッグや親の付け替えで使う） */
Vector3 StageEditor::LocalPositionOf(const ObjectDesc& desc, const Vector3& world) const {
    return ResolveLocalPosition(desc, world, [this](const std::string& name) {
        return FindDescByName(name);
    });
}

/** @brief 親チェーンを解決したワールド位置を返す（親のZ回転も反映し、循環は深さ上限で打ち切る） */
Vector3 StageEditor::WorldPositionOf(const ObjectDesc& desc) const {
    return ResolveWorldPosition(desc, [this](const std::string& name) {
        return FindDescByName(name);
    });
}

/** @brief candidateName が selfName の子孫かどうか（親に設定すると循環になる相手の判定） */
bool StageEditor::IsDescendantOf(const std::string& candidateName, const std::string& selfName) const {
    // candidate から親を辿って self に行き着くなら子孫（親に設定すると循環する）
    const ObjectDesc* cur = FindDescByName(candidateName);
    for (int guard = 0; guard < kMaxParentDepth && cur; ++guard) {
        if (cur->parent.empty()) {
            return false;
        }
        if (cur->parent == selfName) {
            return true;
        }
        cur = FindDescByName(cur->parent);
    }
    return false;
}

} // namespace engine::game
