/** @file ObjectKindMarkers.cpp
 * @brief カメラ地点、巡回地点、イベント条件と文章の種類定義
 */
#include "ObjectKind.h"
#include "Camera.h"
#include "EnemyEntity.h"
#include "EnemyRegistry.h"
#include "EnemyTuning.h"
#include "EnemyTypeLibrary.h"
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

#include "ObjectKindSupport.h"
namespace engine::game {
using namespace engine::graphics;
using namespace objectKindDetail;
namespace {
#ifdef USE_IMGUI
namespace editorUi = engine::game::stageEditorUiStyle;
#endif
constexpr float kMaxConditionSeconds = 300.0f;
constexpr float kMaxCameraBlendSeconds = 10.0f;
constexpr float kMaxCameraHoldSeconds = 30.0f;
constexpr float kWorldTextScale = 1.1f;
constexpr const char* kDefaultText = "テキスト";
/** @brief 有効な間だけカメラを寄せる演出用の地点 */
class CameraPointKind : public ActivatableMarker<ObjectKind> {
public:
    /** @brief カメラ地点の種類として初期化する */
    CameraPointKind()
        : ActivatableMarker(objectKindName::kCameraPoint) {
    }
    /** @brief 回転と拡大縮小の設定を受け付けるか返す */
    bool HasRotationAndScale() const override {
        return true;
    }
    /** @brief 新しく置いたカメラ地点の初期値を設定する */
    void InitializeNewDesc(ObjectDesc& desc, int serial) const override {
        NameWithActivationFlag(desc, "camera_", serial);
    }
    /** @brief 実行中の時刻でまだ有効かを返す */
    bool StaysActive(const ObjectDesc& desc, float runtimeTimer) const override {
        // 維持秒数を過ぎたら、フラグが立ったままでもカメラを返す
        return !(desc.activeWhenFlag && desc.cameraHoldSeconds > 0.0f
            && runtimeTimer > desc.activationDelay + desc.cameraHoldSeconds);
    }
    /** @brief カメラをカメラ地点の位置へ寄せる（編集中は何もしない） */
    bool UpdateBeforeEnemy(StageEditor& editor, int index, float dt) const override {
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
        Vector3 cameraPosition = camera->GetTranslate();
        const float blend = desc.cameraBlendSeconds <= 0.0f ? 1.0f : (std::min)(1.0f, dt / desc.cameraBlendSeconds);
        cameraPosition.x += (targetPosition.x - cameraPosition.x) * blend;
        cameraPosition.y += (targetPosition.y - cameraPosition.y) * blend;
        cameraPosition.z += (targetPosition.z - cameraPosition.z) * blend;
        camera->SetTranslate(cameraPosition);
        camera->SetRotate(desc.rotation);
        return true;
    }
#ifdef USE_IMGUI
    /** @brief カメラ地点のゲーム用の設定欄をインスペクターに出す */
    void DrawGameplayInspector(StageEditor& editor, int index, ObjectDesc& desc, bool&, const UndoCapture& capture) const override {
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
        capture(ImGui::DragFloat("カメラ補間秒数", &desc.cameraBlendSeconds, editorUi::kDragStepFine, 0.0f, kMaxCameraBlendSeconds));
        capture(ImGui::DragFloat("カメラ維持秒数", &desc.cameraHoldSeconds, editorUi::kDragStepSeconds, 0.0f, kMaxCameraHoldSeconds));
    }
#endif
};

/** @brief 敵の巡回ルートの経由地点 */
class PatrolPointKind : public ObjectKind {
public:
    /** @brief 巡回地点の種類として初期化する */
    PatrolPointKind()
        : ObjectKind(objectKindName::kPatrolPoint) {
    }
    /** @brief 実体の描画を持たない編集用地点か返す */
    bool IsMarker() const override {
        return true;
    }
    /** @brief 新しく置いた巡回地点の初期値を設定する */
    void InitializeNewDesc(ObjectDesc& desc, int serial) const override {
        NameWithSerial(desc, "waypoint_", serial);
    }
    /** @brief 経由地点は位置を貸すだけで、自分では何もしない */
    bool UpdateBeforeEnemy(StageEditor&, int, float) const override {
        return true;
    }
    /** @brief 設定を検査し、問題があれば説明文を issues へ追加する */
    void Validate(const ObjectDesc& desc, std::vector<std::string>& issues) const override {
        if (desc.patrolRoute.empty()) {
            issues.push_back("巡回ルート名が空です: " + desc.name);
        }
    }
#ifdef USE_IMGUI
    /** @brief 巡回地点のゲーム用の設定欄をインスペクターに出す */
    void DrawGameplayInspector(StageEditor& editor, int, ObjectDesc& desc, bool&, const UndoCapture& capture) const override {
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
    /** @brief 派生クラスを基底クラスから正しく破棄できるようにする仮想デストラクタ */
    virtual ~ConditionRule() = default;
    /** @brief このフレームで成立しているか @param timer 条件ごとの経過時間（必要な成立のさせ方だけ進める） */
    virtual bool IsMet(const ObjectDesc& desc, float& timer, float dt, const GroupDefeatedQuery& isGroupDefeated) const = 0;
    /** @brief 敵グループの全滅を監視するか */
    virtual bool WatchesEnemyGroup() const {
        return false;
    }
#ifdef USE_IMGUI
    /** @brief 成立のさせ方に固有の設定項目を出す */
    virtual void DrawFields(ObjectDesc&, const ObjectKind::UndoCapture&) const { }
#endif
};

/** @brief グラフやトリガーが condition_<名前> フラグを立てた時に成立する */
class ManualRule : public ConditionRule {
public:
    /** @brief グラフやトリガーが条件フラグを立てたかを返す */
    bool IsMet(const ObjectDesc& desc, float&, float, const GroupDefeatedQuery&) const override {
        return GameFlags::GetInstance()->GetFlag(ConditionFlagName(desc.name));
    }
};

/** @brief 有効になってから指定秒数たつと成立する */
class TimerRule : public ConditionRule {
public:
    /** @brief 待ち時間を進め、時間が経ったら満たされたとして返す */
    bool IsMet(const ObjectDesc& desc, float& timer, float dt, const GroupDefeatedQuery&) const override {
        timer += dt;
        return timer >= desc.conditionSeconds;
    }
#ifdef USE_IMGUI
    /** @brief この条件の設定欄をインスペクターに出す */
    void DrawFields(ObjectDesc& desc, const ObjectKind::UndoCapture& capture) const override {
        capture(ImGui::DragFloat("成立までの秒数", &desc.conditionSeconds, editorUi::kDragStepSeconds, 0.0f, kMaxConditionSeconds));
    }
#endif
};

/** @brief 指定した敵グループが全滅すると成立する */
class EnemyGroupDefeatedRule : public ConditionRule {
public:
    /** @brief 指定したグループの敵がすべて倒されたかを返す */
    bool IsMet(const ObjectDesc& desc, float&, float, const GroupDefeatedQuery& isGroupDefeated) const override {
        return isGroupDefeated(desc.enemyGroup);
    }
    /** @brief 成立条件として敵グループの全滅を監視するか返す */
    bool WatchesEnemyGroup() const override {
        return true;
    }
#ifdef USE_IMGUI
    /** @brief 対象の敵グループを選ぶ欄をインスペクターに出す */
    void DrawFields(ObjectDesc& desc, const ObjectKind::UndoCapture& capture) const override {
        InputString("監視する敵グループ", desc.enemyGroup, capture);
    }
#endif
};

/** @brief 成立のさせ方の名前と実体（インスペクタの選択肢もこの順に並ぶ） */
const std::pair<const char*, const ConditionRule*>* ConditionRules() {
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
int ConditionRuleIndexOf(const std::string& name) {
    for (int i = 0; i < kConditionRuleCount; ++i) {
        if (name == ConditionRules()[i].first) {
            return i;
        }
    }
    return 0;
}

/** @brief 保存された条件名に対応する成立条件を返す */
const ConditionRule& ConditionRuleOf(const std::string& name) {
    return *ConditionRules()[ConditionRuleIndexOf(name)].second;
}

/** @brief 条件（手動/時間/敵グループ全滅）が成立するとフラグを立てる見えない仕掛け */
class EventConditionKind : public ObjectKind {
public:
    /** @brief イベント条件の種類として初期化する */
    EventConditionKind()
        : ObjectKind(objectKindName::kEventCondition) {
    }
    /** @brief 実行時の有効化フラグに付ける接頭辞を返す */
    const char* RuntimeFlagPrefix() const override {
        return kConditionFlagPrefix.data();
    }
    /** @brief 新しく置いたイベント条件の初期値を設定する */
    void InitializeNewDesc(ObjectDesc& desc, int serial) const override {
        NameWithSerial(desc, kConditionFlagPrefix.data(), serial);
    }
    /** @brief 成立条件として敵グループの全滅を監視するか返す */
    bool WatchesEnemyGroup(const ObjectDesc& desc) const override {
        return ConditionRuleOf(desc.conditionType).WatchesEnemyGroup();
    }
    /** @brief 成立条件を調べ、結果を実行中のフラグへ書き込む */
    void UpdateCondition(StageEditor& editor, int index, float dt) const override {
        const ObjectDesc& desc = DescAt(editor, index);
        const bool met = ConditionRuleOf(desc.conditionType).IsMet(desc, MutableRuntimeTimer(editor, index), dt,
            [&editor](const std::string& group) {
                return IsEnemyGroupDefeated(editor, group);
            });
        GameFlags::GetInstance()->SetFlag(std::string(RuntimeFlagPrefix()) + desc.name, met);
    }
#ifdef USE_IMGUI
    /** @brief イベント条件のゲーム用の設定欄をインスペクターに出す */
    void DrawGameplayInspector(StageEditor& editor, int, ObjectDesc& desc, bool&, const UndoCapture& capture) const override {
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
    /** @brief UI文字の種類として初期化する */
    UITextKind()
        : ObjectKind(objectKindName::kUIText) {
    }
    /** @brief 種類を一覧で識別するための表示タグを返す */
    const char* ListTag() const override {
        return "[テキスト] ";
    }
    /** @brief 画面座標で置く文字かを返す */
    bool IsScreenSpace(const ObjectDesc& desc) const override {
        return desc.textSpace == TextSpace::kScreen;
    }
    /** @brief 新しく置いたUI文字の初期値を設定する */
    void InitializeNewDesc(ObjectDesc& desc, int serial) const override {
        NameWithSerial(desc, "text_", serial);
        desc.textSpace = TextSpace::kWorld;
        desc.textScale = kWorldTextScale;
        desc.text = kDefaultText;
    }
    /** @brief 設定を検査し、問題があれば説明文を issues へ追加する */
    void Validate(const ObjectDesc& desc, std::vector<std::string>& issues) const override {
        if (desc.text.empty()) {
            issues.push_back("表示文字列が空です: " + desc.name);
        }
    }
};

}
/** @brief CameraPointの共有定義を取得する */
const ObjectKind& objectKindDetail::CameraPoint() {
    static const CameraPointKind kind;
    return kind;
}

/** @brief PatrolPointの共有定義を取得する */
const ObjectKind& objectKindDetail::PatrolPoint() {
    static const PatrolPointKind kind;
    return kind;
}

/** @brief EventConditionの共有定義を取得する */
const ObjectKind& objectKindDetail::EventCondition() {
    static const EventConditionKind kind;
    return kind;
}

/** @brief UITextの共有定義を取得する */
const ObjectKind& objectKindDetail::UIText() {
    static const UITextKind kind;
    return kind;
}

/** @brief エディタで新しく置いた時の初期値を入れる */
void ObjectKind::InitializeNewDesc(ObjectDesc& desc, int serial) const {
    desc.name = std::string(name_) + "_" + std::to_string(serial);
}

/** @brief 名前を接頭辞+連番にする */
void ObjectKind::NameWithSerial(ObjectDesc& desc, const char* prefix, int serial) {
    desc.name = prefix + std::to_string(serial);
}

/** @brief 名前を接頭辞+連番にし、同名の有効化フラグ（名前_active）も付ける */
void ObjectKind::NameWithActivationFlag(ObjectDesc& desc, const char* prefix, int serial) {
    NameWithSerial(desc, prefix, serial);
    desc.activationFlag = desc.name + "_active";
}

#ifdef USE_IMGUI
/** @brief 有効化フラグの入力欄をインスペクターに出す */
void ObjectKind::DrawActivationFlagField(ObjectDesc& desc, const UndoCapture& capture) {
    InputString("有効化フラグ", desc.activationFlag, capture);
    editorUI::HelpMarker("空なら常時有効です。イベントトリガーが同名のフラグを立てると有効になります");
}

/** @brief 敵グループ名の入力欄をインスペクターに出す */
void ObjectKind::DrawEnemyGroupField(ObjectDesc& desc, const UndoCapture& capture) {
    InputString("敵グループ", desc.enemyGroup, capture);
}

/** @brief 巡回ルート名の入力欄をインスペクターに出す */
void ObjectKind::DrawPatrolRouteField(ObjectDesc& desc, const UndoCapture& capture) {
    InputString("巡回ルート名", desc.patrolRoute, capture);
}

/** @brief 配置した敵に共通の、グループ・巡回ルート・巡回速度の欄をインスペクターに出す */
void ObjectKind::DrawEnemyMovementFields(ObjectDesc& desc, const UndoCapture& capture) {
    DrawEnemyGroupField(desc, capture);
    DrawPatrolRouteField(desc, capture);
    capture(ImGui::DragFloat("巡回速度", &desc.patrolSpeed, editorUi::kDragStepFine, 0.0f, editorUi::kMaxSpeed));
}

/** @brief 敵の見た目（モデル・武器）の設定欄をインスペクターに出す */
void ObjectKind::DrawEnemyLooksFields(StageEditor& editor, ObjectDesc& desc, bool& structuralDirty) {
    /** @brief 選択変更前にUndoを記録して見た目の設定を更新し、敵実体の再生成を要求する */
    auto renderLookChoice = [&](const char* label, std::string& value, const auto& options) {
        std::string selected;
        if (ComboWithEmptyFirst(label, value, options, selected)) {
            RecordUndo(editor);
            value = std::move(selected);
            structuralDirty = true;
        }
    };
    // 空="武器を持たない一般敵"。指定すると倒して奪取できるようになる（GamePlayScene参照）
    // spawn_pointも同じ設定を持ち、出現した敵がそのまま戦闘対象になる
    constexpr const char* kWeaponTypes[] = { "なし", "Sword", "Spear", "Hammer", "Dagger", "Ball", "Greatsword", "Scythe", "Axe" };
    renderLookChoice("奪取可能な武器", desc.weaponType, kWeaponTypes);
    editorUI::HelpMarker("なしの騎士はプレイヤーの攻撃対象になりません（本編の戦闘対象は武器持ちの敵とモンスターです）");

    // 見た目を騎士からモンスターに差し替える。モンスターは武器を持たず、倒すと消えて覚醒ゲージになる
    // 選択肢は enemy_params.json の visuals から作るので、「敵を作る」で足した見た目もここに出る
    std::vector<std::string> looks = { "騎士" };
    for (std::string& name : EnemyTuning::GetInstance()->VisualNames()) {
        looks.push_back(std::move(name));
    }
    std::string selectedLook;
    if (ComboFromList("見た目", desc.enemyModel, looks, looks, selectedLook)) {
        RecordUndo(editor);
        desc.enemyModel = std::move(selectedLook);
        structuralDirty = true;
    }
    editorUI::HelpMarker("モンスターは武器を持たない敵です（奪取可能な武器はなしにしておく）。倒すと消えて覚醒ゲージが溜まります");
}

/** @brief 敵の種類・動き方・強さの倍率の欄を出す（エディタの「敵を作る」で作った種類を当てられる） */
void ObjectKind::DrawEnemyTypeFields(StageEditor& editor, ObjectDesc& desc, bool& structuralDirty, const UndoCapture& capture) {
    // 種類を選ぶと、その種類の値を配置物へ写す（ステージのJSONだけで敵が再現できるように、参照ではなく写しにする）
    std::vector<std::string> typeLabels = { "（指定なし）" };
    std::vector<std::string> typeIds = { "" };
    for (const EnemyTypeDef& type : EnemyTypeLibrary::GetInstance()->Types()) {
        typeLabels.push_back(type.label.empty() ? type.id : type.label);
        typeIds.push_back(type.id);
    }
    std::string selectedType;
    if (ComboFromList("敵の種類", desc.enemyType, typeLabels, typeIds, selectedType)) {
        RecordUndo(editor);
        if (const EnemyTypeDef* type = EnemyTypeLibrary::GetInstance()->Find(selectedType)) {
            EnemyTypeLibrary::ApplyTo(*type, desc);
        } else {
            desc.enemyType.clear();
        }
        structuralDirty = true;
    }
    editorUI::HelpMarker("ツールバーの「敵を作る」で作った種類です。選ぶと動き方・見た目・武器・強さがまとめて入ります（あとから個別に変えてもかまいません）");

    // 動き方は敵の性格名（EnemyEntity が名前から選ぶ）
    static const std::vector<std::string> kBehaviorLabels = { "歩く", "飛ぶ", "回復役" };
    static const std::vector<std::string> kBehaviorValues = { "basic", "flying", "healer" };
    const std::string currentBehavior = desc.spawnType == "basic" ? std::string() : desc.spawnType;
    std::string selectedBehavior;
    if (ComboFromList("動き方", currentBehavior, kBehaviorLabels, kBehaviorValues, selectedBehavior)) {
        RecordUndo(editor);
        desc.spawnType = selectedBehavior.empty() ? "basic" : selectedBehavior;
        structuralDirty = true;
    }

    // 強さの倍率は敵を作り直した時に反映するので、変えたら作り直しを頼む
    constexpr float kMinStatScale = 0.1f;
    constexpr float kMaxStatScale = 10.0f;
    bool scaleChanged = false;
    scaleChanged |= capture(ImGui::DragFloat("HPの倍率", &desc.enemyHpScale, editorUi::kDragStepRatio, kMinStatScale, kMaxStatScale));
    scaleChanged |= capture(ImGui::DragFloat("攻撃力の倍率", &desc.enemyDamageScale, editorUi::kDragStepRatio, kMinStatScale, kMaxStatScale));
    scaleChanged |= capture(ImGui::DragFloat("速さの倍率", &desc.enemySpeedScale, editorUi::kDragStepRatio, kMinStatScale, kMaxStatScale));
    if (scaleChanged) {
        structuralDirty = true;
    }
}

/** @brief 変更前のステージの状態を Undo 用に記録する */
void ObjectKind::RecordUndo(StageEditor& editor) {
    editor.RecordUndoSnapshotNow();
}

/** @brief 実行中の拾える物がもう拾われたかを返す */
bool ObjectKind::PickupCollected(const StageEditor& editor, int index) {
    return editor.objects_[index].pickupCollected;
}

/** @brief 実行中の壊せる物の残りHPを返す */
int ObjectKind::BreakableHp(const StageEditor& editor, int index) {
    return editor.objects_[index].breakableHp;
}

/** @brief 実行中の壊せる物が壊れたかを返す */
bool ObjectKind::BreakableDestroyed(const StageEditor& editor, int index) {
    return editor.objects_[index].breakableDestroyed;
}
#endif

//  StageEditorの内部へ触る窓口

}
