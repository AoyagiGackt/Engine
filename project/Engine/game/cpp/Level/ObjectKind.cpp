/**
 * @file ObjectKind.cpp
 * @brief 配置物の種類ごとの性質・初期値・検証・デバッグ表示・インスペクタ項目の実装
 */
#include "ObjectKind.h"
#include "Camera.h"
#include "EnemyEntity.h"
#include "EnemyRegistry.h"
#include "GameFlags.h"
#include "GimmickMotion.h"
#include "KnightEnemy.h"
#include "LevelLoader.h"
#include "ModelCommon.h"
#include "Object3d.h"
#include "StageEditor.h"
#include "Weapon.h"
#include <algorithm>
#include <iterator>
#include <memory>
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

namespace {
constexpr const char* kBlockModel = "Resources/block/block.obj";
constexpr const char* kBlockTexture = "Resources/block/block.png";
constexpr const char* kPickupTexture = "Resources/Effects/circle2.png";
constexpr const char* kDefaultEnemyWeapon = "Sword";
constexpr float kBarrelScale = 0.9f;
constexpr float kPickupScale = 0.35f;
constexpr float kWorldTextScale = 1.1f;
constexpr const char* kDefaultText = "テキスト";

#ifdef USE_IMGUI
namespace EditorUi = engine::game::StageEditorUiStyle;
constexpr float kMaxConditionSeconds = 300.0f;
constexpr float kMaxPickupRadius = 10.0f;
constexpr float kMaxCameraBlendSeconds = 10.0f;
constexpr float kMaxCameraHoldSeconds = 30.0f;
constexpr float kSpawnMarkerHalf = 0.4f; // spawn_pointを示す赤い枠の半径
constexpr float kMotionEndMarkerRadius = 0.2f; // ギミック可動域の端に出す十字の大きさ
constexpr size_t kTextFieldSize = 96;

/** @brief 文字列の設定項目を1行の入力欄で編集する */
bool InputString(const char* label, std::string& value, const ObjectKind::UndoCapture& capture)
{
    char buffer[kTextFieldSize] = { };
    strncpy_s(buffer, value.c_str(), _TRUNCATE);
    if (capture(ImGui::InputText(label, buffer, sizeof(buffer)))) {
        value = buffer;
        return true;
    }
    return false;
}

/**
 * @brief 文字列の選択肢から1つ選ぶコンボ（先頭の選択肢は空文字として扱う）
 * @param selected 選び直された時だけ新しい値を書き込む（Undoの記録を値の書き換えより先に行えるよう、元の値は変えない）
 * @return 選び直されたか
 */
template <size_t N>
bool ComboWithEmptyFirst(const char* label, const std::string& current, const char* const (&items)[N], std::string& selected)
{
    int index = 0;
    for (int i = 1; i < static_cast<int>(N); ++i) {
        if (current == items[i]) {
            index = i;
            break;
        }
    }
    if (!ImGui::Combo(label, &index, items, static_cast<int>(N))) {
        return false;
    }
    selected = index == 0 ? "" : items[index];
    return true;
}
#endif

//  種類ごとのクラス

/** @brief 種類名だけを持つ汎用の種類（未知の種類名を読み込んだ時の受け皿） */
class GenericKind : public ObjectKind {
public:
    explicit GenericKind(const char* name)
        : ObjectKind(name)
    {
    }
};

/** @brief 当たり判定付きの置物 */
class PropKind : public ObjectKind {
public:
    PropKind()
        : ObjectKind(ObjectKindName::kProp)
    {
    }
    bool IsVisual() const override { return true; }
    bool IsDecoration() const override { return true; }
    void InitializeNewDesc(ObjectDesc& desc, int serial) const override
    {
        NameWithSerial(desc, "obj_", serial);
        desc.model = kBlockModel;
        desc.texture = kBlockTexture;
        desc.solid = true;
        desc.lighting = false;
    }
};

/** @brief 背景モデル（当たり判定なし、モデル未指定でもよい） */
class BackgroundKind : public ObjectKind {
public:
    BackgroundKind()
        : ObjectKind(ObjectKindName::kBackground)
    {
    }
    bool IsVisual() const override { return true; }
    bool RequiresModel() const override { return false; }
#ifdef USE_IMGUI
    void DrawVisualNote() const override
    {
        ImGui::TextDisabled("背景モデル（レベルJSONに保存）");
        ImGui::SameLine();
        EditorUI::HelpMarker("モデル・テクスチャ・位置・回転・スケールを通常のオブジェクトと同様に調整できます。当たり判定は初期状態で無効です。");
    }
#endif
};

/** @brief 動く床や扉など、有効化フラグで動き出す仕掛け */
class GimmickKind : public ObjectKind {
public:
    GimmickKind()
        : ObjectKind(ObjectKindName::kGimmick)
    {
    }
    bool IsVisual() const override { return true; }
    bool AcceptsActivationFlag() const override { return true; }
    bool AlwaysAdvancesTimer() const override { return true; }
    void InitializeNewDesc(ObjectDesc& desc, int serial) const override
    {
        NameWithActivationFlag(desc, "gimmick_", serial);
        desc.model = kBlockModel;
        desc.texture = kBlockTexture;
        desc.solid = true;
        desc.lighting = true;
    }
    const GimmickMotion* MotionOf(const ObjectDesc& desc) const override { return &GimmickMotion::Of(desc.gimmickMotion); }
    void Validate(const ObjectDesc& desc, std::vector<std::string>& issues) const override
    {
        MotionOf(desc)->Validate(desc, issues);
    }
#ifdef USE_IMGUI
    void DrawDebugRange(const ObjectDesc& d, const Vector3& world) const override
    {
        // 可動域: 往復系は両端、片道は終点まで線で示す
        const GimmickMotion* motion = MotionOf(d);
        const Vector3 axis = motion->RangeAxis(d);
        if (axis.x == 0.0f && axis.y == 0.0f && axis.z == 0.0f) {
            return;
        }
        const bool oneWay = motion->IsOneWay(d);
        const Vector3 from = oneWay ? world : world + axis * -1.0f;
        const Vector3 to = world + axis;
        DiagnosticsDraw::DrawLine(from, to, DiagnosticsDraw::kColorMagenta);
        DiagnosticsDraw::DrawCross(to, kMotionEndMarkerRadius, DiagnosticsDraw::kColorMagenta);
        if (!oneWay) {
            DiagnosticsDraw::DrawCross(from, kMotionEndMarkerRadius, DiagnosticsDraw::kColorMagenta);
        }
    }
    void DrawGameplayInspector(StageEditor& editor, int, ObjectDesc& desc, bool&, const UndoCapture& capture) const override
    {
        DrawActivationFlagField(desc, capture);
        constexpr const char* kMotions[] = { "none", "move_x", "move_y", "rotate_y", "rotate_z", "fall", "blink", "custom" };
        std::string motion;
        if (ComboWithEmptyFirst("動作プリセット", desc.gimmickMotion, kMotions, motion)) {
            RecordUndo(editor);
            desc.gimmickMotion = motion.empty() ? kMotions[0] : motion;
        }
        EditorUI::HelpMarker("customは移動方向・回転量・往復方式を自由に組み合わせる汎用動作です。プリセットに無い動きはここで作れます");
        capture(ImGui::DragFloat("動作量", &desc.motionAmount, EditorUi::kDragStepPosition));
        capture(ImGui::DragFloat("動作速度", &desc.motionSpeed, EditorUi::kDragStepPosition, 0.0f, EditorUi::kMaxSpeed));
        if (!MotionOf(desc)->IsFreeform()) {
            return;
        }
        capture(ImGui::DragFloat3("移動方向", &desc.motionAxis.x, EditorUi::kDragStepFine));
        EditorUI::HelpMarker("この方向へ動作量ぶん動きます。(0,0,0)なら移動しません");
        capture(ImGui::DragFloat3("回転量(rad)", &desc.motionRotation.x, EditorUi::kDragStepFine));
        EditorUI::HelpMarker("進行度1.0に対する各軸の回転量です。(0,0,0)なら回転しません");
        int modeIndex = static_cast<int>(desc.motionMode);
        if (ImGui::Combo("往復方式", &modeIndex, kMotionModeNames, static_cast<int>(std::size(kMotionModeNames)))) {
            RecordUndo(editor);
            desc.motionMode = static_cast<MotionMode>(modeIndex);
        }
        EditorUI::HelpMarker("loop: 波のように往復 / pingpong: 等速で往復 / once: 有効化から一度だけ動いて止まる（扉の開閉など）");
        int easeIndex = static_cast<int>(desc.motionEase);
        if (ImGui::Combo("加減速", &easeIndex, kMotionEaseNames, static_cast<int>(std::size(kMotionEaseNames)))) {
            RecordUndo(editor);
            desc.motionEase = static_cast<MotionEase>(easeIndex);
        }
    }
    void DrawRuntimeStatus(const StageEditor& editor, int index) const override
    {
        ImGui::Text("動作経過: %.2f 秒", RuntimeTimer(editor, index));
    }
#endif
};

/** @brief 地形（当たり判定必須、描画メッシュと同期したコライダーも選べる） */
class TerrainKind : public ObjectKind {
public:
    TerrainKind()
        : ObjectKind(ObjectKindName::kTerrain)
    {
    }
    bool IsVisual() const override { return true; }
    bool CountsAsGround() const override { return true; }
    void Validate(const ObjectDesc& desc, std::vector<std::string>& issues) const override
    {
        if (!desc.solid) {
            issues.push_back("Terrainの当たり判定が無効です: " + desc.name);
        }
    }
#ifdef USE_IMGUI
    void DrawVisualExtras(StageEditor& editor, ObjectDesc& desc) const override
    {
        if (ImGui::Checkbox("メッシュ同期コライダー", &desc.meshCollider)) {
            RecordUndo(editor);
            desc.solid = desc.meshCollider || desc.solid;
        }
    }
#endif
};

/** @brief 回収すると覚醒ゲージが増える収集物 */
class PickupKind : public ObjectKind {
public:
    PickupKind()
        : ObjectKind(ObjectKindName::kPickup)
    {
    }
    bool IsVisual() const override { return true; }
    bool AcceptsActivationFlag() const override { return true; }
    bool AlwaysAdvancesTimer() const override { return true; }
    const char* ListTag() const override { return "[収集物] "; }
    const char* RuntimeFlagPrefix() const override { return "pickup_"; }
    void InitializeNewDesc(ObjectDesc& desc, int serial) const override
    {
        NameWithSerial(desc, "pickup_", serial);
        desc.model = kBlockModel;
        desc.texture = kPickupTexture;
        desc.scale = { kPickupScale, kPickupScale, kPickupScale };
        desc.lighting = false;
        desc.solid = false;
    }
    bool UpdateOwnRuntime(StageEditor& editor, int index, engine::graphics::ParticleManager* pm, const Vector3& playerPos) const override
    {
        UpdatePickup(editor, index, pm, playerPos);
        return true;
    }
#ifdef USE_IMGUI
    void DrawDebugRange(const ObjectDesc& d, const Vector3& world) const override
    {
        DiagnosticsDraw::DrawSphere({ world, d.pickupRadius }, DiagnosticsDraw::kColorCyan);
    }
    void DrawGameplayInspector(StageEditor&, int, ObjectDesc& desc, bool&, const UndoCapture& capture) const override
    {
        DrawActivationFlagField(desc, capture);
        capture(ImGui::DragFloat("回収半径", &desc.pickupRadius, EditorUi::kDragStepFine, EditorUi::kMinRadius, kMaxPickupRadius));
        capture(ImGui::DragFloat("覚醒ゲージ増加量", &desc.pickupGaugeAmount, EditorUi::kDragStepRatio, 0.0f, 1.0f));
        capture(ImGui::ColorEdit4("表示色", &desc.pickupColor.x));
        EditorUI::HelpMarker("回収するとGameFlagsに pickup_<名前> が立ちます。ノードグラフのフラグ起動やGetFlagで反応できます");
    }
    void DrawRuntimeStatus(const StageEditor& editor, int index) const override
    {
        ImGui::Text("回収: %s", PickupCollected(editor, index) ? "済み" : "未");
    }
#endif
};

/** @brief 攻撃で壊せる樽や壁 */
class BreakableKind : public ObjectKind {
public:
    BreakableKind()
        : ObjectKind(ObjectKindName::kBreakable)
    {
    }
    bool IsVisual() const override { return true; }
    bool AcceptsActivationFlag() const override { return true; }
    bool AlwaysAdvancesTimer() const override { return true; }
    const char* ListTag() const override { return "[壊せる物] "; }
    const char* RuntimeFlagPrefix() const override { return "broken_"; }
    void InitializeNewDesc(ObjectDesc& desc, int serial) const override
    {
        NameWithSerial(desc, "breakable_", serial);
        desc.model = kBlockModel;
        desc.texture = kBlockTexture;
        desc.solid = false;
        desc.lighting = true;
        desc.scale = { kBarrelScale, kBarrelScale, kBarrelScale };
    }
    bool UpdateOwnRuntime(StageEditor& editor, int index, engine::graphics::ParticleManager*, const Vector3&) const override
    {
        // 見た目のトランスフォームだけ追従させる（ヒット判定と破壊はシーン側がGetBreakables()経由で行う）
        RefreshUnbrokenInstances(editor, index);
        return true;
    }
    void Validate(const ObjectDesc& desc, std::vector<std::string>& issues) const override
    {
        if (desc.breakableHp <= 0) {
            issues.push_back("壊せる物のHPが0以下です: " + desc.name);
        }
    }
#ifdef USE_IMGUI
    void DrawDebugRange(const ObjectDesc& d, const Vector3& world) const override
    {
        if (d.breakableRadius > 0.0f) {
            DiagnosticsDraw::DrawSphere({ world, d.breakableRadius }, DiagnosticsDraw::kColorOrange);
        }
    }
    void DrawGameplayInspector(StageEditor& editor, int, ObjectDesc& desc, bool& structuralDirty, const UndoCapture& capture) const override
    {
        DrawActivationFlagField(desc, capture);
        if (ImGui::InputInt("耐久(ヒット回数)", &desc.breakableHp)) {
            RecordUndo(editor);
            structuralDirty = true; // 残りHPは実体生成時に初期化するため作り直す
        }
        capture(ImGui::DragFloat("爆風半径", &desc.breakableRadius, EditorUi::kDragStepPosition, 0.0f, EditorUi::kMaxRadius));
        if (ImGui::InputInt("プレイヤーへのダメージ", &desc.breakablePlayerDamage)) {
            RecordUndo(editor);
        }
        if (ImGui::InputInt("敵へのダメージ", &desc.breakableEnemyDamage)) {
            RecordUndo(editor);
        }
        capture(ImGui::ColorEdit4("表示色", &desc.breakableColor.x));
        EditorUI::HelpMarker("壊すとGameFlagsに broken_<名前> が立ちます");
        constexpr const char* kBreakWeapons[] = { "何でも", "Sword", "Spear", "Hammer", "Dagger", "Ball", "Greatsword", "Scythe", "Axe" };
        std::string breakWeapon;
        if (ComboWithEmptyFirst("壊せる武器", desc.breakableWeapon, kBreakWeapons, breakWeapon)) {
            RecordUndo(editor);
            desc.breakableWeapon = breakWeapon;
        }
        EditorUI::HelpMarker("武器を指定すると、その武器の近接攻撃でしか壊れません。当たり判定ONと爆風半径0にすると壊せる壁になります");
    }
    void DrawRuntimeStatus(const StageEditor& editor, int index) const override
    {
        ImGui::Text("耐久: %d / %d  %s", BreakableHp(editor, index), DescAt(editor, index).breakableHp,
            BreakableDestroyed(editor, index) ? "(破壊済み)" : "");
    }
#endif
};

/** @brief 最初から置かれている一般敵 */
class EnemyBasicKind : public ObjectKind {
public:
    EnemyBasicKind()
        : ObjectKind(ObjectKindName::kEnemyBasic)
    {
    }
    bool PlacesEnemy() const override { return true; }
    bool SpawnsCombatEnemy() const override { return true; }
    const char* ListTag() const override { return "[エネミー] "; }
    void InitializeNewDesc(ObjectDesc& desc, int serial) const override
    {
        NameWithSerial(desc, "enemy_", serial);
        desc.weaponType = kDefaultEnemyWeapon;
    }
    void PrepareEnemyInstance(StageEditor& editor, int index) const override
    {
        ClearEnemyInstances(editor, index, false, true);
        EnsureBasicEnemyInstance(editor, index);
    }
#ifdef USE_IMGUI
    void DrawGameplayInspector(StageEditor& editor, int, ObjectDesc& desc, bool& structuralDirty, const UndoCapture& capture) const override
    {
        DrawEnemyGroupField(desc, capture);
        DrawPatrolRouteField(desc, capture);
        capture(ImGui::DragFloat("巡回速度", &desc.patrolSpeed, EditorUi::kDragStepFine, 0.0f, EditorUi::kMaxSpeed));
        DrawEnemyLooksFields(editor, desc, structuralDirty);
        // isStageBossはEnemyEntity生成には関わらないメタデータなのでstructuralDirtyは不要
        capture(ImGui::Checkbox("ステージボス（倒して奪取するとクリア）", &desc.isStageBoss));
    }
#endif
};

/** @brief 剣を持つナイト型の敵 */
class EnemyKnightKind : public ObjectKind {
public:
    EnemyKnightKind()
        : ObjectKind(ObjectKindName::kEnemyKnight)
    {
    }
    bool PlacesEnemy() const override { return true; }
    const char* ListTag() const override { return "[ナイト] "; }
    void InitializeNewDesc(ObjectDesc& desc, int serial) const override
    {
        NameWithSerial(desc, "knight_", serial);
    }
    void PrepareEnemyInstance(StageEditor& editor, int index) const override
    {
        ClearEnemyInstances(editor, index, true, false);
        EnsureKnightInstance(editor, index);
    }
#ifdef USE_IMGUI
    void DrawGameplayInspector(StageEditor&, int, ObjectDesc& desc, bool&, const UndoCapture& capture) const override
    {
        DrawEnemyGroupField(desc, capture);
        DrawPatrolRouteField(desc, capture);
        capture(ImGui::DragFloat("巡回速度", &desc.patrolSpeed, EditorUi::kDragStepFine, 0.0f, EditorUi::kMaxSpeed));
    }
#endif
};

/** @brief 有効化フラグが立った時に敵を出現させる地点 */
class SpawnPointKind : public ObjectKind {
public:
    SpawnPointKind()
        : ObjectKind(ObjectKindName::kSpawnPoint)
    {
    }
    bool AcceptsActivationFlag() const override { return true; }
    bool PlacesEnemy() const override { return true; }
    bool SpawnsCombatEnemy() const override { return true; }
    bool IsMarker() const override { return true; }
    bool SpawnsLater() const override { return true; }
    void InitializeNewDesc(ObjectDesc& desc, int serial) const override
    {
        NameWithActivationFlag(desc, "spawn_", serial);
        desc.weaponType = kDefaultEnemyWeapon;
    }
    void Validate(const ObjectDesc& desc, std::vector<std::string>& issues) const override
    {
        if (desc.spawnType != "basic" && !SpawnsKnight(desc)) {
            issues.push_back("SpawnPointの敵種類が不正です: " + desc.name);
        }
    }
    void PrepareEnemyInstance(StageEditor& editor, int index) const override
    {
        // 出現前は実体を作らず、有効化フラグが立ってから出す（作り終えた敵は残す）
        const bool knight = SpawnsKnight(DescAt(editor, index));
        ClearEnemyInstances(editor, index, knight, !knight);
        if (!IsRuntimeActive(editor, index)) {
            return;
        }
        if (knight) {
            EnsureKnightInstance(editor, index);
        } else {
            EnsureBasicEnemyInstance(editor, index);
        }
    }
    bool UpdateBeforeEnemy(StageEditor& editor, int index, float) const override
    {
        // 有効になった瞬間に、まだ出していない敵を出す
        if (HasEnemyInstance(editor, index)) {
            return false;
        }
        RegenerateInstances(editor, index);
        return true;
    }

private:
    /** @brief 出現する敵がナイトか（それ以外は一般敵。敵の性格名はEnemyEntityへそのまま渡す） */
    static bool SpawnsKnight(const ObjectDesc& desc) { return desc.spawnType == "knight"; }

public:
#ifdef USE_IMGUI
    void DrawDebugRange(const ObjectDesc&, const Vector3& world) const override
    {
        DiagnosticsDraw::DrawAABB({ { world.x - kSpawnMarkerHalf, world.y - kSpawnMarkerHalf, world.z - kSpawnMarkerHalf },
                                      { world.x + kSpawnMarkerHalf, world.y + kSpawnMarkerHalf, world.z + kSpawnMarkerHalf } },
            DiagnosticsDraw::kColorRed);
    }
    void DrawGameplayInspector(StageEditor& editor, int, ObjectDesc& desc, bool& structuralDirty, const UndoCapture& capture) const override
    {
        DrawActivationFlagField(desc, capture);
        const char* spawnTypes[] = { "basic", "knight" };
        int spawnTypeIndex = desc.spawnType == "knight" ? 1 : 0;
        if (ImGui::Combo("発生する敵", &spawnTypeIndex, spawnTypes, 2)) {
            RecordUndo(editor);
            desc.spawnType = spawnTypes[spawnTypeIndex];
            structuralDirty = true;
        }
        DrawEnemyGroupField(desc, capture);
        DrawEnemyLooksFields(editor, desc, structuralDirty);
    }
#endif
};

/** @brief 有効な間だけカメラを寄せる演出用の地点 */
class CameraPointKind : public ObjectKind {
public:
    CameraPointKind()
        : ObjectKind(ObjectKindName::kCameraPoint)
    {
    }
    bool AcceptsActivationFlag() const override { return true; }
    bool IsMarker() const override { return true; }
    bool HasRotationAndScale() const override { return true; }
    void InitializeNewDesc(ObjectDesc& desc, int serial) const override
    {
        NameWithActivationFlag(desc, "camera_", serial);
    }
    bool StaysActive(const ObjectDesc& desc, float runtimeTimer) const override
    {
        // 維持秒数を過ぎたら、フラグが立ったままでもカメラを返す
        return !(desc.activeWhenFlag && desc.cameraHoldSeconds > 0.0f
            && runtimeTimer > desc.activationDelay + desc.cameraHoldSeconds);
    }
    bool UpdateBeforeEnemy(StageEditor& editor, int index, float dt) const override
    {
        // 編集中は演出用カメラポイントで自由カメラを上書きしない
        if (IsEditingView(editor)) {
            return true;
        }
        Camera* camera = EditorCamera(editor);
        if (!camera) {
            return true;
        }
        const ObjectDesc& desc = DescAt(editor, index);
        const Vector3 targetPosition = WorldPosition(editor, desc);
        Vector3& cameraPosition = camera->GetTranslate();
        const float blend = desc.cameraBlendSeconds <= 0.0f ? 1.0f : (std::min)(1.0f, dt / desc.cameraBlendSeconds);
        cameraPosition.x += (targetPosition.x - cameraPosition.x) * blend;
        cameraPosition.y += (targetPosition.y - cameraPosition.y) * blend;
        cameraPosition.z += (targetPosition.z - cameraPosition.z) * blend;
        camera->SetRotate(desc.rotation);
        return true;
    }
#ifdef USE_IMGUI
    void DrawGameplayInspector(StageEditor& editor, int index, ObjectDesc& desc, bool&, const UndoCapture& capture) const override
    {
        DrawActivationFlagField(desc, capture);
        Camera* camera = EditorCamera(editor);
        if (camera && ImGui::Button("現在のビューをカメラポイントへ保存")) {
            RecordUndo(editor);
            desc.position = camera->GetTranslate();
            desc.rotation = camera->GetRotate();
            RefreshTransforms(editor, index);
        }
        if (camera && ImGui::Button("カメラポイントをプレビュー")) {
            camera->SetTranslate(WorldPosition(editor, desc));
            camera->SetRotate(desc.rotation);
        }
        capture(ImGui::DragFloat("カメラ補間秒数", &desc.cameraBlendSeconds, EditorUi::kDragStepFine, 0.0f, kMaxCameraBlendSeconds));
        capture(ImGui::DragFloat("カメラ維持秒数", &desc.cameraHoldSeconds, EditorUi::kDragStepSeconds, 0.0f, kMaxCameraHoldSeconds));
    }
#endif
};

/** @brief 敵の巡回ルートの経由地点 */
class PatrolPointKind : public ObjectKind {
public:
    PatrolPointKind()
        : ObjectKind(ObjectKindName::kPatrolPoint)
    {
    }
    bool IsMarker() const override { return true; }
    void InitializeNewDesc(ObjectDesc& desc, int serial) const override
    {
        NameWithSerial(desc, "waypoint_", serial);
    }
    /** @brief 経由地点は位置を貸すだけで、自分では何もしない */
    bool UpdateBeforeEnemy(StageEditor&, int, float) const override { return true; }
    void Validate(const ObjectDesc& desc, std::vector<std::string>& issues) const override
    {
        if (desc.patrolRoute.empty()) {
            issues.push_back("巡回ルート名が空です: " + desc.name);
        }
    }
#ifdef USE_IMGUI
    void DrawGameplayInspector(StageEditor& editor, int, ObjectDesc& desc, bool&, const UndoCapture& capture) const override
    {
        DrawPatrolRouteField(desc, capture);
        if (ImGui::InputInt("巡回順", &desc.routeOrder)) {
            RecordUndo(editor);
        }
    }
#endif
};

//  イベント条件の成立のさせ方

/** @brief 条件の成立のさせ方の基底（ステートレスで共有する） */
class ConditionRule {
public:
    /** @brief 敵グループが全滅したかを調べる関数 */
    using GroupDefeatedQuery = std::function<bool(const std::string&)>;
    virtual ~ConditionRule() = default;
    /** @brief このフレームで成立しているか @param timer 条件ごとの経過時間（必要な成立のさせ方だけ進める） */
    virtual bool IsMet(const ObjectDesc& desc, float& timer, float dt, const GroupDefeatedQuery& isGroupDefeated) const = 0;
    /** @brief 敵グループの全滅を監視するか */
    virtual bool WatchesEnemyGroup() const { return false; }
#ifdef USE_IMGUI
    /** @brief 成立のさせ方に固有の設定項目を出す */
    virtual void DrawFields(ObjectDesc&, const ObjectKind::UndoCapture&) const { }
#endif
};

/** @brief グラフやトリガーが condition_<名前> フラグを立てた時に成立する */
class ManualRule : public ConditionRule {
public:
    bool IsMet(const ObjectDesc& desc, float&, float, const GroupDefeatedQuery&) const override
    {
        return GameFlags::GetInstance()->GetFlag(std::string("condition_") + desc.name);
    }
};

/** @brief 有効になってから指定秒数たつと成立する */
class TimerRule : public ConditionRule {
public:
    bool IsMet(const ObjectDesc& desc, float& timer, float dt, const GroupDefeatedQuery&) const override
    {
        timer += dt;
        return timer >= desc.conditionSeconds;
    }
#ifdef USE_IMGUI
    void DrawFields(ObjectDesc& desc, const ObjectKind::UndoCapture& capture) const override
    {
        capture(ImGui::DragFloat("成立までの秒数", &desc.conditionSeconds, EditorUi::kDragStepSeconds, 0.0f, kMaxConditionSeconds));
    }
#endif
};

/** @brief 指定した敵グループが全滅すると成立する */
class EnemyGroupDefeatedRule : public ConditionRule {
public:
    bool IsMet(const ObjectDesc& desc, float&, float, const GroupDefeatedQuery& isGroupDefeated) const override
    {
        return isGroupDefeated(desc.enemyGroup);
    }
    bool WatchesEnemyGroup() const override { return true; }
#ifdef USE_IMGUI
    void DrawFields(ObjectDesc& desc, const ObjectKind::UndoCapture& capture) const override
    {
        InputString("監視する敵グループ", desc.enemyGroup, capture);
    }
#endif
};

/** @brief 成立のさせ方の名前と実体（インスペクタの選択肢もこの順に並ぶ） */
const std::pair<const char*, const ConditionRule*>* ConditionRules()
{
    static const ManualRule manual;
    static const TimerRule timer;
    static const EnemyGroupDefeatedRule enemyGroupDefeated;
    static const std::pair<const char*, const ConditionRule*> kRules[] = {
        { "manual", &manual },
        { "timer", &timer },
        { "enemy_group_defeated", &enemyGroupDefeated },
    };
    return kRules;
}
constexpr int kConditionRuleCount = 3;

/** @brief 名前に対応する成立のさせ方の番号（未知の名前はmanual） */
int ConditionRuleIndexOf(const std::string& name)
{
    for (int i = 0; i < kConditionRuleCount; ++i) {
        if (name == ConditionRules()[i].first) {
            return i;
        }
    }
    return 0;
}

const ConditionRule& ConditionRuleOf(const std::string& name)
{
    return *ConditionRules()[ConditionRuleIndexOf(name)].second;
}

/** @brief 条件（手動/時間/敵グループ全滅）が成立するとフラグを立てる見えない仕掛け */
class EventConditionKind : public ObjectKind {
public:
    EventConditionKind()
        : ObjectKind(ObjectKindName::kEventCondition)
    {
    }
    const char* RuntimeFlagPrefix() const override { return "condition_"; }
    void InitializeNewDesc(ObjectDesc& desc, int serial) const override
    {
        NameWithSerial(desc, "condition_", serial);
    }
    bool WatchesEnemyGroup(const ObjectDesc& desc) const override { return ConditionRuleOf(desc.conditionType).WatchesEnemyGroup(); }
    void UpdateCondition(StageEditor& editor, int index, float dt) const override
    {
        const ObjectDesc& desc = DescAt(editor, index);
        const bool met = ConditionRuleOf(desc.conditionType).IsMet(desc, MutableRuntimeTimer(editor, index), dt,
            [&editor](const std::string& group) { return IsEnemyGroupDefeated(editor, group); });
        GameFlags::GetInstance()->SetFlag(std::string(RuntimeFlagPrefix()) + desc.name, met);
    }
#ifdef USE_IMGUI
    void DrawGameplayInspector(StageEditor& editor, int, ObjectDesc& desc, bool&, const UndoCapture& capture) const override
    {
        const char* conditionTypes[kConditionRuleCount] = { };
        for (int i = 0; i < kConditionRuleCount; ++i) {
            conditionTypes[i] = ConditionRules()[i].first;
        }
        int conditionIndex = ConditionRuleIndexOf(desc.conditionType);
        if (ImGui::Combo("条件", &conditionIndex, conditionTypes, kConditionRuleCount)) {
            RecordUndo(editor);
            desc.conditionType = conditionTypes[conditionIndex];
        }
        ConditionRuleOf(desc.conditionType).DrawFields(desc, capture);
    }
#endif
};

/** @brief 画面またはワールドに置く文章 */
class UITextKind : public ObjectKind {
public:
    UITextKind()
        : ObjectKind(ObjectKindName::kUIText)
    {
    }
    const char* ListTag() const override { return "[テキスト] "; }
    bool IsScreenSpace(const ObjectDesc& desc) const override { return desc.textSpace == TextSpace::Screen; }
    void InitializeNewDesc(ObjectDesc& desc, int serial) const override
    {
        NameWithSerial(desc, "text_", serial);
        desc.textSpace = TextSpace::World;
        desc.textScale = kWorldTextScale;
        desc.text = kDefaultText;
    }
    void Validate(const ObjectDesc& desc, std::vector<std::string>& issues) const override
    {
        if (desc.text.empty()) {
            issues.push_back("表示文字列が空です: " + desc.name);
        }
    }
};

} // namespace

const ObjectKind& ObjectKind::Of(const std::string& name)
{
    static const PropKind prop;
    static const BackgroundKind background;
    static const GimmickKind gimmick;
    static const TerrainKind terrain;
    static const PickupKind pickup;
    static const BreakableKind breakable;
    static const EnemyBasicKind enemyBasic;
    static const EnemyKnightKind enemyKnight;
    static const SpawnPointKind spawnPoint;
    static const CameraPointKind cameraPoint;
    static const PatrolPointKind patrolPoint;
    static const EventConditionKind eventCondition;
    static const UITextKind uiText;
    static const ObjectKind* const kKinds[] = {
        &prop, &background, &gimmick, &terrain, &pickup, &breakable, &enemyBasic,
        &enemyKnight, &spawnPoint, &cameraPoint, &patrolPoint, &eventCondition, &uiText,
    };
    for (const ObjectKind* kind : kKinds) {
        if (name == kind->name_) {
            return *kind;
        }
    }
    // 未知の種類名は名前ごとに汎用の種類を作って使い回す（新規配置時の名前付けに種類名が要るため）
    static std::vector<std::pair<std::string, std::unique_ptr<GenericKind>>> unknownKinds;
    for (const auto& [unknownName, kind] : unknownKinds) {
        if (unknownName == name) {
            return *kind;
        }
    }
    unknownKinds.emplace_back(name, nullptr);
    unknownKinds.back().second = std::make_unique<GenericKind>(unknownKinds.back().first.c_str());
    return *unknownKinds.back().second;
}

void ObjectKind::InitializeNewDesc(ObjectDesc& desc, int serial) const
{
    desc.name = std::string(name_) + "_" + std::to_string(serial);
}

void ObjectKind::NameWithSerial(ObjectDesc& desc, const char* prefix, int serial)
{
    desc.name = prefix + std::to_string(serial);
}

void ObjectKind::NameWithActivationFlag(ObjectDesc& desc, const char* prefix, int serial)
{
    NameWithSerial(desc, prefix, serial);
    desc.activationFlag = desc.name + "_active";
}

#ifdef USE_IMGUI
void ObjectKind::DrawActivationFlagField(ObjectDesc& desc, const UndoCapture& capture)
{
    InputString("有効化フラグ", desc.activationFlag, capture);
    EditorUI::HelpMarker("空なら常時有効です。イベントトリガーが同名のフラグを立てると有効になります");
}

void ObjectKind::DrawEnemyGroupField(ObjectDesc& desc, const UndoCapture& capture)
{
    InputString("敵グループ", desc.enemyGroup, capture);
}

void ObjectKind::DrawPatrolRouteField(ObjectDesc& desc, const UndoCapture& capture)
{
    InputString("巡回ルート名", desc.patrolRoute, capture);
}

void ObjectKind::DrawEnemyLooksFields(StageEditor& editor, ObjectDesc& desc, bool& structuralDirty)
{
    // 空="武器を持たない一般敵"。指定すると倒して奪取できるようになる（GamePlayScene参照）
    // spawn_pointも同じ設定を持ち、出現した敵がそのまま戦闘対象になる
    constexpr const char* kWeaponTypes[] = { "なし", "Sword", "Spear", "Hammer", "Dagger", "Ball", "Greatsword", "Scythe", "Axe" };
    std::string weaponType;
    if (ComboWithEmptyFirst("奪取可能な武器", desc.weaponType, kWeaponTypes, weaponType)) {
        RecordUndo(editor);
        desc.weaponType = weaponType;
        structuralDirty = true; // 武器種別はEnemyEntity生成時にしか反映できないため実体を作り直す
    }
    EditorUI::HelpMarker("なしの騎士はプレイヤーの攻撃対象になりません（本編の戦闘対象は武器持ちの敵とモンスターです）");

    // 見た目を騎士からモンスターに差し替える。モンスターは武器を持たず、倒すと消えて覚醒ゲージになる
    constexpr const char* kEnemyModels[] = { "騎士", "Slime", "Bat", "Dragon", "Skeleton" };
    std::string enemyModel;
    if (ComboWithEmptyFirst("見た目", desc.enemyModel, kEnemyModels, enemyModel)) {
        RecordUndo(editor);
        desc.enemyModel = enemyModel;
        structuralDirty = true; // 見た目もEnemyEntity生成時に決まるため実体を作り直す
    }
    EditorUI::HelpMarker("モンスターは武器を持たない敵です（奪取可能な武器はなしにしておく）。倒すと消えて覚醒ゲージが溜まります");
}

void ObjectKind::RecordUndo(StageEditor& editor)
{
    editor.RecordUndoSnapshotNow();
}

bool ObjectKind::PickupCollected(const StageEditor& editor, int index)
{
    return editor.objects_[index].pickupCollected;
}

int ObjectKind::BreakableHp(const StageEditor& editor, int index)
{
    return editor.objects_[index].breakableHp;
}

bool ObjectKind::BreakableDestroyed(const StageEditor& editor, int index)
{
    return editor.objects_[index].breakableDestroyed;
}
#endif

//  StageEditorの内部へ触る窓口

void ObjectKind::PrepareEnemyInstance(StageEditor& editor, int index) const
{
    ClearEnemyInstances(editor, index, false, false);
}

const ObjectDesc& ObjectKind::DescAt(const StageEditor& editor, int index)
{
    return editor.objects_[index].desc;
}

float ObjectKind::RuntimeTimer(const StageEditor& editor, int index)
{
    return editor.objects_[index].runtimeTimer;
}

float& ObjectKind::MutableRuntimeTimer(StageEditor& editor, int index)
{
    return editor.objects_[index].runtimeTimer;
}

Camera* ObjectKind::EditorCamera(StageEditor& editor)
{
    return editor.camera_;
}

void ObjectKind::RefreshTransforms(StageEditor& editor, int index)
{
    editor.RefreshTransforms(editor.objects_[index]);
}

Vector3 ObjectKind::WorldPosition(const StageEditor& editor, const ObjectDesc& desc)
{
    return editor.WorldPositionOf(desc);
}

bool ObjectKind::IsRuntimeActive(const StageEditor& editor, int index)
{
    return editor.IsRuntimeActive(editor.objects_[index].desc);
}

bool ObjectKind::IsEditingView(const StageEditor& editor)
{
    return editor.visible_ && !editor.playTestMode_;
}

bool ObjectKind::HasEnemyInstance(const StageEditor& editor, int index)
{
    const auto& entry = editor.objects_[index];
    return entry.knight || entry.enemy;
}

void ObjectKind::RegenerateInstances(StageEditor& editor, int index)
{
    editor.RegenerateInstances(editor.objects_[index]);
}

void ObjectKind::ClearEnemyInstances(StageEditor& editor, int index, bool keepKnight, bool keepBasic)
{
    auto& entry = editor.objects_[index];
    if (!keepKnight) {
        entry.knight.reset();
    }
    if (!keepBasic) {
        editor.UnregisterEnemyEntity(entry);
        entry.enemy.reset();
    }
}

void ObjectKind::EnsureKnightInstance(StageEditor& editor, int index)
{
    auto& entry = editor.objects_[index];
    if (entry.knight) {
        return;
    }
    entry.knight = std::make_unique<KnightEnemy>();
    entry.knight->Initialize(editor.modelCommon_, editor.WorldPositionOf(entry.desc));
}

void ObjectKind::EnsureBasicEnemyInstance(StageEditor& editor, int index)
{
    auto& entry = editor.objects_[index];
    if (entry.enemy) {
        return;
    }
    const ObjectDesc& desc = entry.desc;
    entry.enemy = std::make_unique<EnemyEntity>();
    entry.enemy->Initialize(editor.modelCommon_, editor.WorldPositionOf(desc), ParseWeaponTypeName(desc.weaponType));
    entry.enemy->SetArchetype(desc.spawnType);
    if (!desc.enemyModel.empty()) {
        entry.enemy->SetMonsterVisual(editor.modelCommon_, desc.enemyModel);
    }
    EnemyRegistry::GetInstance()->Register(desc.name, entry.enemy.get());
}

void ObjectKind::UpdatePickup(StageEditor& editor, int index, engine::graphics::ParticleManager* pm, const Vector3& playerPos)
{
    auto& entry = editor.objects_[index];
    if (!entry.pickupCollected) {
        editor.UpdatePickupEntry(entry, pm, playerPos);
    }
}

void ObjectKind::RefreshUnbrokenInstances(StageEditor& editor, int index)
{
    auto& entry = editor.objects_[index];
    if (entry.breakableDestroyed) {
        return;
    }
    editor.RefreshTransforms(entry);
    for (auto& obj : entry.instances) {
        obj->Update();
    }
}

bool ObjectKind::IsEnemyGroupDefeated(const StageEditor& editor, const std::string& group)
{
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

} // namespace engine::game
