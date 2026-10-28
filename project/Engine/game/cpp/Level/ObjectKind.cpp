/**
 * @file ObjectKind.cpp
 * @brief 配置物の種類ごとの性質・初期値・検証・デバッグ表示・インスペクタ項目の実装
 */
#include "ObjectKind.h"
#include "ObjectKindSupport.h"
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
using namespace objectKindDetail;

namespace {
constexpr const char* kBlockModel = "Resources/block/block.obj";
constexpr const char* kBlockTexture = "Resources/block/block.png";
constexpr const char* kPickupTexture = "Resources/Effects/circle2.png";
constexpr const char* kDefaultEnemyWeapon = "Sword";
constexpr float kBarrelScale = 0.9f;
constexpr float kPickupScale = 0.35f;

#ifdef USE_IMGUI
namespace editorUi = engine::game::stageEditorUiStyle;
constexpr float kMaxPickupRadius = 10.0f;
using editorUi::kSpawnMarkerHalf;
using editorUi::kMotionEndMarkerRadius;
#endif

//  種類ごとのクラス

/** @brief 種類名だけを持つ汎用の種類（未知の種類名を読み込んだ時の受け皿） */
class GenericKind : public ObjectKind {
public:
    /** @brief 種類名を指定して初期化する */
    explicit GenericKind(const char* name)
        : ObjectKind(name) {
    }
};

/** @brief 当たり判定付きの置物 */
class PropKind : public ObjectKind {
public:
    /** @brief 置物の種類として初期化する */
    PropKind()
        : ObjectKind(objectKindName::kProp) {
    }
    /** @brief 描画する実体を生成する種類か返す */
    bool IsVisual() const override {
        return true;
    }
    /** @brief 装飾用の配置物か返す */
    bool IsDecoration() const override {
        return true;
    }
    /** @brief 新しく置いた置物の初期値を設定する */
    void InitializeNewDesc(ObjectDesc& desc, int serial) const override {
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
    /** @brief 背景の種類として初期化する */
    BackgroundKind()
        : ObjectKind(objectKindName::kBackground) {
    }
    /** @brief 描画する実体を生成する種類か返す */
    bool IsVisual() const override {
        return true;
    }
    /** @brief モデルの指定が必要な配置物か返す */
    bool RequiresModel() const override {
        return false;
    }
#ifdef USE_IMGUI
    /** @brief 背景の見た目についての注意書きをインスペクターに出す */
    void DrawVisualNote() const override {
        ImGui::TextDisabled("背景モデル（レベルJSONに保存）");
        ImGui::SameLine();
        editorUI::HelpMarker("モデル・テクスチャ・位置・回転・スケールを通常のオブジェクトと同様に調整できます。当たり判定は初期状態で無効です。");
    }
#endif
};

/** @brief 実体を描画し、有効化フラグで出現・作動し、待機中も動作の時間を進める種類（仕掛け・拾える物・壊せる物）の共通部分 */
class ActivatableVisualKind : public ObjectKind {
public:
    /** @brief 種類名を指定して初期化する */
    explicit ActivatableVisualKind(const char* name)
        : ObjectKind(name) {
    }
    /** @brief 描画する実体を生成する種類か返す */
    bool IsVisual() const override {
        return true;
    }
    /** @brief 有効化フラグによる制御を受け付けるか返す */
    bool AcceptsActivationFlag() const override {
        return true;
    }
    /** @brief 配置物の待機中も動作タイマーを進めるか返す */
    bool AlwaysAdvancesTimer() const override {
        return true;
    }
};

/** @brief 動く床や扉など、有効化フラグで動き出す仕掛け */
class GimmickKind : public ActivatableVisualKind {
public:
    /** @brief 仕掛けの種類として初期化する */
    GimmickKind()
        : ActivatableVisualKind(objectKindName::kGimmick) {
    }
    /** @brief 新しく置いた仕掛けの初期値を設定する */
    void InitializeNewDesc(ObjectDesc& desc, int serial) const override {
        NameWithActivationFlag(desc, "gimmick_", serial);
        desc.model = kBlockModel;
        desc.texture = kBlockTexture;
        desc.solid = true;
        desc.lighting = true;
    }
    /** @brief 設定に合った動き方（GimmickMotion）を返す */
    const GimmickMotion* MotionOf(const ObjectDesc& desc) const override {
        return &GimmickMotion::Of(desc.gimmickMotion);
    }
    /** @brief 設定を検査し、問題があれば説明文を issues へ追加する */
    void Validate(const ObjectDesc& desc, std::vector<std::string>& issues) const override {
        MotionOf(desc)->Validate(desc, issues);
    }
#ifdef USE_IMGUI
    /** @brief 仕掛けの動く範囲をデバッグ表示する */
    void DrawDebugRange(const ObjectDesc& d, const Vector3& world) const override {
        // 可動域: 往復系は両端、片道は終点まで線で示す
        const GimmickMotion* motion = MotionOf(d);
        const Vector3 axis = motion->RangeAxis(d);
        if (axis.x == 0.0f && axis.y == 0.0f && axis.z == 0.0f) {
            return;
        }
        const bool oneWay = motion->IsOneWay(d);
        const Vector3 from = oneWay ? world : world + axis * -1.0f;
        const Vector3 to = world + axis;
        diagnosticsDraw::DrawLine(from, to, diagnosticsDraw::kColorMagenta);
        diagnosticsDraw::DrawCross(to, kMotionEndMarkerRadius, diagnosticsDraw::kColorMagenta);
        if (!oneWay) {
            diagnosticsDraw::DrawCross(from, kMotionEndMarkerRadius, diagnosticsDraw::kColorMagenta);
        }
    }
    /** @brief 仕掛けのゲーム用の設定欄をインスペクターに出す */
    void DrawGameplayInspector(StageEditor& editor, int, ObjectDesc& desc, bool&, const UndoCapture& capture) const override {
        DrawActivationFlagField(desc, capture);
        constexpr const char* kMotions[] = { "none", "move_x", "move_y", "rotate_y", "rotate_z", "fall", "blink", "custom" };
        std::string motion;
        if (ComboWithEmptyFirst("動作プリセット", desc.gimmickMotion, kMotions, motion)) {
            RecordUndo(editor);
            desc.gimmickMotion = motion.empty() ? kMotions[0] : motion;
        }
        editorUI::HelpMarker("customは移動方向・回転量・往復方式を自由に組み合わせる汎用動作です。プリセットに無い動きはここで作れます");
        capture(ImGui::DragFloat("動作量", &desc.motionAmount, editorUi::kDragStepPosition));
        capture(ImGui::DragFloat("動作速度", &desc.motionSpeed, editorUi::kDragStepPosition, 0.0f, editorUi::kMaxSpeed));
        if (!MotionOf(desc)->IsFreeform()) {
            return;
        }
        capture(ImGui::DragFloat3("移動方向", &desc.motionAxis.x, editorUi::kDragStepFine));
        editorUI::HelpMarker("この方向へ動作量ぶん動きます。(0,0,0)なら移動しません");
        capture(ImGui::DragFloat3("回転量(rad)", &desc.motionRotation.x, editorUi::kDragStepFine));
        editorUI::HelpMarker("進行度1.0に対する各軸の回転量です。(0,0,0)なら回転しません");
        int modeIndex = static_cast<int>(desc.motionMode);
        if (ImGui::Combo("往復方式", &modeIndex, kMotionModeNames, static_cast<int>(std::size(kMotionModeNames)))) {
            RecordUndo(editor);
            desc.motionMode = static_cast<MotionMode>(modeIndex);
        }
        editorUI::HelpMarker("loop: 波のように往復 / pingpong: 等速で往復 / once: 有効化から一度だけ動いて止まる（扉の開閉など）");
        int easeIndex = static_cast<int>(desc.motionEase);
        if (ImGui::Combo("加減速", &easeIndex, kMotionEaseNames, static_cast<int>(std::size(kMotionEaseNames)))) {
            RecordUndo(editor);
            desc.motionEase = static_cast<MotionEase>(easeIndex);
        }
    }
    /** @brief 実行中の仕掛けの状態をインスペクターに出す */
    void DrawRuntimeStatus(const StageEditor& editor, int index) const override {
        ImGui::Text("動作経過: %.2f 秒", RuntimeTimer(editor, index));
    }
#endif
};

/** @brief 地形（当たり判定必須、描画メッシュと同期したコライダーも選べる） */
class TerrainKind : public ObjectKind {
public:
    /** @brief 地形の種類として初期化する */
    TerrainKind()
        : ObjectKind(objectKindName::kTerrain) {
    }
    /** @brief 描画する実体を生成する種類か返す */
    bool IsVisual() const override {
        return true;
    }
    /** @brief 支持面となる地形として扱うか返す */
    bool CountsAsGround() const override {
        return true;
    }
    /** @brief 設定を検査し、問題があれば説明文を issues へ追加する */
    void Validate(const ObjectDesc& desc, std::vector<std::string>& issues) const override {
        if (!desc.solid) {
            issues.push_back("Terrainの当たり判定が無効です: " + desc.name);
        }
    }
#ifdef USE_IMGUI
    /** @brief 地形の見た目の追加設定をインスペクターに出す */
    void DrawVisualExtras(StageEditor& editor, ObjectDesc& desc) const override {
        if (ImGui::Checkbox("メッシュ同期コライダー", &desc.meshCollider)) {
            RecordUndo(editor);
            desc.solid = desc.meshCollider || desc.solid;
        }
    }
#endif
};

/** @brief 回収すると覚醒ゲージが増える収集物 */
class PickupKind : public ActivatableVisualKind {
public:
    /** @brief 拾える物の種類として初期化する */
    PickupKind()
        : ActivatableVisualKind(objectKindName::kPickup) {
    }
    /** @brief 種類を一覧で識別するための表示タグを返す */
    const char* ListTag() const override {
        return "[収集物] ";
    }
    /** @brief 実行時の有効化フラグに付ける接頭辞を返す */
    const char* RuntimeFlagPrefix() const override {
        return "pickup_";
    }
    /** @brief 新しく置いた拾える物の初期値を設定する */
    void InitializeNewDesc(ObjectDesc& desc, int serial) const override {
        NameWithSerial(desc, "pickup_", serial);
        desc.model = kBlockModel;
        desc.texture = kPickupTexture;
        desc.scale = { kPickupScale, kPickupScale, kPickupScale };
        desc.lighting = false;
        desc.solid = false;
    }
    /** @brief プレイヤーとの接触を調べて拾う処理を進める */
    bool UpdateOwnRuntime(StageEditor& editor, int index, engine::graphics::ParticleManager* pm, const Vector3& playerPos) const override {
        UpdatePickup(editor, index, pm, playerPos);
        return true;
    }
#ifdef USE_IMGUI
    /** @brief 拾える範囲をデバッグ表示する */
    void DrawDebugRange(const ObjectDesc& d, const Vector3& world) const override {
        diagnosticsDraw::DrawSphere({ world, d.pickupRadius }, diagnosticsDraw::kColorCyan);
    }
    /** @brief 拾える物のゲーム用の設定欄をインスペクターに出す */
    void DrawGameplayInspector(StageEditor&, int, ObjectDesc& desc, bool&, const UndoCapture& capture) const override {
        DrawActivationFlagField(desc, capture);
        capture(ImGui::DragFloat("回収半径", &desc.pickupRadius, editorUi::kDragStepFine, editorUi::kMinRadius, kMaxPickupRadius));
        capture(ImGui::DragFloat("覚醒ゲージ増加量", &desc.pickupGaugeAmount, editorUi::kDragStepRatio, 0.0f, 1.0f));
        capture(ImGui::ColorEdit4("表示色", &desc.pickupColor.x));
        editorUI::HelpMarker("回収するとGameFlagsに pickup_<名前> が立ちます。ノードグラフのフラグ起動やGetFlagで反応できます");
    }
    /** @brief 実行中の拾える物の状態をインスペクターに出す */
    void DrawRuntimeStatus(const StageEditor& editor, int index) const override {
        ImGui::Text("回収: %s", PickupCollected(editor, index) ? "済み" : "未");
    }
#endif
};

/** @brief 攻撃で壊せる樽や壁 */
class BreakableKind : public ActivatableVisualKind {
public:
    /** @brief 壊せる物の種類として初期化する */
    BreakableKind()
        : ActivatableVisualKind(objectKindName::kBreakable) {
    }
    /** @brief 種類を一覧で識別するための表示タグを返す */
    const char* ListTag() const override {
        return "[壊せる物] ";
    }
    /** @brief 実行時の有効化フラグに付ける接頭辞を返す */
    const char* RuntimeFlagPrefix() const override {
        return "broken_";
    }
    /** @brief 新しく置いた壊せる物の初期値を設定する */
    void InitializeNewDesc(ObjectDesc& desc, int serial) const override {
        NameWithSerial(desc, "breakable_", serial);
        desc.model = kBlockModel;
        desc.texture = kBlockTexture;
        desc.solid = false;
        desc.lighting = true;
        desc.scale = { kBarrelScale, kBarrelScale, kBarrelScale };
    }
    /** @brief 壊れていない物の見た目の位置だけを追従させる */
    bool UpdateOwnRuntime(StageEditor& editor, int index, engine::graphics::ParticleManager*, const Vector3&) const override {
        // 見た目のトランスフォームだけ追従させる（ヒット判定と破壊はシーン側がGetBreakables()経由で行う）
        RefreshUnbrokenInstances(editor, index);
        return true;
    }
    /** @brief 設定を検査し、問題があれば説明文を issues へ追加する */
    void Validate(const ObjectDesc& desc, std::vector<std::string>& issues) const override {
        if (desc.breakableHp <= 0) {
            issues.push_back("壊せる物のHPが0以下です: " + desc.name);
        }
    }
#ifdef USE_IMGUI
    /** @brief 壊せる物の当たり範囲をデバッグ表示する */
    void DrawDebugRange(const ObjectDesc& d, const Vector3& world) const override {
        if (d.breakableRadius > 0.0f) {
            diagnosticsDraw::DrawSphere({ world, d.breakableRadius }, diagnosticsDraw::kColorOrange);
        }
    }
    /** @brief 壊せる物のゲーム用の設定欄をインスペクターに出す */
    void DrawGameplayInspector(StageEditor& editor, int, ObjectDesc& desc, bool& structuralDirty, const UndoCapture& capture) const override {
        DrawActivationFlagField(desc, capture);
        if (ImGui::InputInt("耐久(ヒット回数)", &desc.breakableHp)) {
            RecordUndo(editor);
            structuralDirty = true; // 残りHPは実体生成時に初期化するため作り直す
        }
        capture(ImGui::DragFloat("爆風半径", &desc.breakableRadius, editorUi::kDragStepPosition, 0.0f, editorUi::kMaxRadius));
        if (ImGui::InputInt("プレイヤーへのダメージ", &desc.breakablePlayerDamage)) {
            RecordUndo(editor);
        }
        if (ImGui::InputInt("敵へのダメージ", &desc.breakableEnemyDamage)) {
            RecordUndo(editor);
        }
        capture(ImGui::ColorEdit4("表示色", &desc.breakableColor.x));
        editorUI::HelpMarker("壊すとGameFlagsに broken_<名前> が立ちます");
        constexpr const char* kBreakWeapons[] = { "何でも", "Sword", "Spear", "Hammer", "Dagger", "Ball", "Greatsword", "Scythe", "Axe" };
        std::string breakWeapon;
        if (ComboWithEmptyFirst("壊せる武器", desc.breakableWeapon, kBreakWeapons, breakWeapon)) {
            RecordUndo(editor);
            desc.breakableWeapon = breakWeapon;
        }
        editorUI::HelpMarker("武器を指定すると、その武器の近接攻撃でしか壊れません。当たり判定ONと爆風半径0にすると壊せる壁になります");
    }
    /** @brief 実行中の壊せる物の状態をインスペクターに出す */
    void DrawRuntimeStatus(const StageEditor& editor, int index) const override {
        ImGui::Text("耐久: %d / %d  %s", BreakableHp(editor, index), DescAt(editor, index).breakableHp,
            BreakableDestroyed(editor, index) ? "(破壊済み)" : "");
    }
#endif
};

/** @brief 敵を配置する種類の共通部分（一般敵・ナイト・出現地点） */
class EnemyPlacingKind : public ObjectKind {
public:
    /** @brief 種類名を指定して初期化する */
    explicit EnemyPlacingKind(const char* name)
        : ObjectKind(name) {
    }
    /** @brief 敵を配置する種類かを返す */
    bool PlacesEnemy() const override {
        return true;
    }
};

/** @brief 全滅判定など戦闘の対象になる敵を出す種類の共通部分（一般敵・出現地点） */
class CombatEnemyKind : public EnemyPlacingKind {
public:
    /** @brief 種類名を指定して初期化する */
    explicit CombatEnemyKind(const char* name)
        : EnemyPlacingKind(name) {
    }
    /** @brief 戦闘の対象になる敵を出す種類かを返す */
    bool SpawnsCombatEnemy() const override {
        return true;
    }
};

/** @brief 最初から置かれている一般敵 */
class EnemyBasicKind : public CombatEnemyKind {
public:
    /** @brief 通常の敵の種類として初期化する */
    EnemyBasicKind()
        : CombatEnemyKind(objectKindName::kEnemyBasic) {
    }
    /** @brief 設定を検査し、問題があれば説明文を issues へ追加する */
    void Validate(const ObjectDesc& desc, std::vector<std::string>& issues) const override {
        // 空を飛ぶ敵は武器を持たせない（武器は地上の敵から奪う）
        if (!desc.weaponType.empty() && EnemyEntity::IsFlyingArchetype(desc.spawnType)) {
            issues.push_back("飛ぶ敵に武器が設定されています（武器なしにして enemyModel を付けてください）: " + desc.name);
        }
    }
    /** @brief 種類を一覧で識別するための表示タグを返す */
    const char* ListTag() const override {
        return "[エネミー] ";
    }
    /** @brief 新しく置いた敵の初期値を設定する */
    void InitializeNewDesc(ObjectDesc& desc, int serial) const override {
        NameWithSerial(desc, "enemy_", serial);
        desc.weaponType = kDefaultEnemyWeapon;
    }
    /** @brief 設定に合わせて敵の実体を作り直す（不要なら消す） */
    void PrepareEnemyInstance(StageEditor& editor, int index) const override {
        ClearEnemyInstances(editor, index, false, true);
        EnsureBasicEnemyInstance(editor, index);
    }
#ifdef USE_IMGUI
    /** @brief 敵のゲーム用の設定欄をインスペクターに出す */
    void DrawGameplayInspector(StageEditor& editor, int, ObjectDesc& desc, bool& structuralDirty, const UndoCapture& capture) const override {
        DrawEnemyTypeFields(editor, desc, structuralDirty, capture);
        DrawEnemyMovementFields(desc, capture);
        DrawEnemyLooksFields(editor, desc, structuralDirty);
        // isStageBossはEnemyEntity生成には関わらないメタデータなのでstructuralDirtyは不要
        capture(ImGui::Checkbox("ステージボス（倒して奪取するとクリア）", &desc.isStageBoss));
    }
#endif
};

/** @brief 剣を持つナイト型の敵 */
class EnemyKnightKind : public EnemyPlacingKind {
public:
    /** @brief 騎士の敵の種類として初期化する */
    EnemyKnightKind()
        : EnemyPlacingKind(objectKindName::kEnemyKnight) {
    }
    /** @brief 種類を一覧で識別するための表示タグを返す */
    const char* ListTag() const override {
        return "[ナイト] ";
    }
    /** @brief 新しく置いた騎士の初期値を設定する */
    void InitializeNewDesc(ObjectDesc& desc, int serial) const override {
        NameWithSerial(desc, "knight_", serial);
    }
    /** @brief 設定に合わせて騎士の実体を作り直す（不要なら消す） */
    void PrepareEnemyInstance(StageEditor& editor, int index) const override {
        ClearEnemyInstances(editor, index, true, false);
        EnsureKnightInstance(editor, index);
    }
#ifdef USE_IMGUI
    /** @brief 騎士のゲーム用の設定欄をインスペクターに出す */
    void DrawGameplayInspector(StageEditor&, int, ObjectDesc& desc, bool&, const UndoCapture& capture) const override {
        DrawEnemyMovementFields(desc, capture);
    }
#endif
};

/** @brief 有効化フラグが立った時に敵を出現させる地点 */
class SpawnPointKind : public ActivatableMarker<CombatEnemyKind> {
public:
    /** @brief 敵の出現地点の種類として初期化する */
    SpawnPointKind()
        : ActivatableMarker(objectKindName::kSpawnPoint) {
    }
    /** @brief 条件が満たされるまで敵を出さずに待つ種類かを返す */
    bool SpawnsLater() const override {
        return true;
    }
    /** @brief 新しく置いた出現地点の初期値を設定する */
    void InitializeNewDesc(ObjectDesc& desc, int serial) const override {
        NameWithActivationFlag(desc, "spawn_", serial);
        desc.weaponType = kDefaultEnemyWeapon;
    }
    /** @brief 設定を検査し、問題があれば説明文を issues へ追加する */
    void Validate(const ObjectDesc& desc, std::vector<std::string>& issues) const override {
        if (FindSpawnTypeIndex(desc.spawnType) < 0) {
            issues.push_back("SpawnPointの敵種類が不正です: " + desc.name);
        }
    }
    /** @brief 設定に合わせて出現させる敵の実体を用意する */
    void PrepareEnemyInstance(StageEditor& editor, int index) const override {
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
    /** @brief 有効になった瞬間に、まだ出していない敵を出す（出した時 true） */
    bool UpdateBeforeEnemy(StageEditor& editor, int index, float) const override {
        // 有効になった瞬間に、まだ出していない敵を出す
        if (HasEnemyInstance(editor, index)) {
            return false;
        }
        RegenerateInstances(editor, index);
        return true;
    }

private:
    /** @brief 出現する敵がナイトか（それ以外は一般敵。敵の性格名はEnemyEntityへそのまま渡す） */
    static bool SpawnsKnight(const ObjectDesc& desc) {
        const int typeIndex = FindSpawnTypeIndex(desc.spawnType);
        return typeIndex >= 0 && kSpawnTypes[typeIndex].knight;
    }

public:
#ifdef USE_IMGUI
    /** @brief 出現地点の位置をデバッグ表示する */
    void DrawDebugRange(const ObjectDesc&, const Vector3& world) const override {
        diagnosticsDraw::DrawAABB({ { world.x - kSpawnMarkerHalf, world.y - kSpawnMarkerHalf, world.z - kSpawnMarkerHalf },
                                      { world.x + kSpawnMarkerHalf, world.y + kSpawnMarkerHalf, world.z + kSpawnMarkerHalf } },
            diagnosticsDraw::kColorRed);
    }
    /** @brief 出現地点のゲーム用の設定欄をインスペクターに出す */
    void DrawGameplayInspector(StageEditor& editor, int, ObjectDesc& desc, bool& structuralDirty, const UndoCapture& capture) const override {
        DrawActivationFlagField(desc, capture);
        const char* spawnTypeNames[std::size(kSpawnTypes)] = {};
        for (size_t i = 0; i < std::size(kSpawnTypes); ++i) {
            spawnTypeNames[i] = kSpawnTypes[i].name;
        }
        int spawnTypeIndex = (std::max)(FindSpawnTypeIndex(desc.spawnType), 0);
        if (ImGui::Combo("発生する敵", &spawnTypeIndex, spawnTypeNames, static_cast<int>(std::size(spawnTypeNames)))) {
            RecordUndo(editor);
            desc.spawnType = kSpawnTypes[spawnTypeIndex].name;
            structuralDirty = true;
        }
        DrawEnemyGroupField(desc, capture);
        DrawEnemyLooksFields(editor, desc, structuralDirty);
    }
#endif
};

} // namespace

/** @brief 3D投影せず画面座標のまま扱う配置物か（screen 座標の ui_text） */
bool IsScreenAnchorObject(const ObjectDesc& desc) {
    return ObjectKind::Of(desc.kind).IsScreenSpace(desc);
}

/** @brief 種類名に対応する種類を返す（未知の名前は名前だけ持つ汎用の種類） */
const ObjectKind& ObjectKind::Of(const std::string& name) {
    // 登録済みの種類は名前で引けるよう一覧にしておく（各種類は初回だけ作る）
    static const PropKind kProp;
    static const BackgroundKind kBackground;
    static const GimmickKind kGimmick;
    static const TerrainKind kTerrain;
    static const PickupKind kPickup;
    static const BreakableKind kBreakable;
    static const EnemyBasicKind kEnemyBasic;
    static const EnemyKnightKind kEnemyKnight;
    static const SpawnPointKind kSpawnPoint;
    // 位置の目印・条件・文字の種類は別ファイルで作ったものを借りる
    const ObjectKind& cameraPoint = objectKindDetail::CameraPoint();
    const ObjectKind& patrolPoint = objectKindDetail::PatrolPoint();
    const ObjectKind& eventCondition = objectKindDetail::EventCondition();
    const ObjectKind& uiText = objectKindDetail::UIText();
    // 名前で探す対象の一覧（新しい種類を足すときはここにも並べる）
    static const ObjectKind* const kKinds[] = {
        &kProp, &kBackground, &kGimmick, &kTerrain, &kPickup, &kBreakable, &kEnemyBasic,
        &kEnemyKnight, &kSpawnPoint, &cameraPoint, &patrolPoint, &eventCondition, &uiText,
    };
    // 名前が一致する登録済みの種類を返す
    for (const ObjectKind* kind : kKinds) {
        if (name == kind->name_) {
            return *kind;
        }
    }
    // 未知の種類名は名前ごとに汎用の種類を作って使い回す（新規配置時の名前付けに種類名が要るため）
    static std::vector<std::pair<std::string, std::unique_ptr<GenericKind>>> sUnknownKinds;
    // 一度作った名前ならそれを返す
    for (const auto& [unknownName, kind] : sUnknownKinds) {
        if (unknownName == name) {
            return *kind;
        }
    }
    // 初めての名前は登録する。種類は一覧に入れた名前の文字列を指すので、追加してから作る
    sUnknownKinds.emplace_back(name, nullptr);
    sUnknownKinds.back().second = std::make_unique<GenericKind>(sUnknownKinds.back().first.c_str());
    return *sUnknownKinds.back().second;
}


} // namespace engine::game
