/**
 * @file ObjectKind.h
 * @brief レベル配置物の種類（prop/gimmick/enemy_basic など）ごとの性質と振る舞いを表すクラス群
 * @note 種類名の文字列を見るのは ObjectKind::Of() だけにし、種類による違いは派生クラスの仮想関数で表す
 */
#pragma once
#include "Vector3.h"
#include <functional>
#include <string>
#include <vector>

namespace engine::graphics {
class Camera;
class ParticleManager;
}

namespace engine::game {
struct ObjectDesc;
class StageEditor;
class GimmickMotion;

/** @brief レベルJSONの kind に書く種類名（保存形式の一部なので値を変えない） */
namespace ObjectKindName {
    inline constexpr const char* kProp = "prop";
    inline constexpr const char* kBackground = "background";
    inline constexpr const char* kGimmick = "gimmick";
    inline constexpr const char* kTerrain = "terrain";
    inline constexpr const char* kPickup = "pickup";
    inline constexpr const char* kBreakable = "breakable";
    inline constexpr const char* kEnemyBasic = "enemy_basic";
    inline constexpr const char* kEnemyKnight = "enemy_knight";
    inline constexpr const char* kSpawnPoint = "spawn_point";
    inline constexpr const char* kCameraPoint = "camera_point";
    inline constexpr const char* kPatrolPoint = "patrol_point";
    inline constexpr const char* kEventCondition = "event_condition";
    inline constexpr const char* kUIText = "ui_text";
}

/**
 * @brief 配置物の種類ごとの性質と振る舞いの基底
 * @note 種類はステートレスで、同じ種類の配置物は同じインスタンスを共有する。
 * StageEditorの内部（配置物の実行時状態やカメラ）に触る処理は、基底のprotectedな窓口を通す
 */
class ObjectKind {
public:
    /** @brief インスペクタの1項目を編集した時にUndoへ記録する関数（変更があったかを受け取り、そのまま返す） */
    using UndoCapture = std::function<bool(bool)>;

    virtual ~ObjectKind() = default;

    /** @brief 種類名に対応する種類を返す（未知の名前は名前だけ持つ汎用の種類） */
    static const ObjectKind& Of(const std::string& name);

    // 性質

    /** @brief 見た目のモデル（Object3d）を生成する種類か */
    virtual bool IsVisual() const { return false; }
    /** @brief モデルの指定が必須か（背景は空でもよい） */
    virtual bool RequiresModel() const { return IsVisual(); }
    /** @brief インスペクタで回転とスケールを編集できるか */
    virtual bool HasRotationAndScale() const { return IsVisual(); }
    /** @brief 有効化フラグを持ち、イベント接続の対象になれるか */
    virtual bool AcceptsActivationFlag() const { return false; }
    /** @brief 敵を配置する種類か（敵グループの集計や制作ガイドに使う） */
    virtual bool PlacesEnemy() const { return false; }
    /** @brief 有効化されてから敵を出現させる種類か（出現前の敵も敵グループに数える） */
    virtual bool SpawnsLater() const { return false; }
    /** @brief 本編の戦闘対象になる敵を生み出す種類か */
    virtual bool SpawnsCombatEnemy() const { return false; }
    /** @brief モデルを持たない目印（位置だけを使う）か */
    virtual bool IsMarker() const { return false; }
    /** @brief 地面として数えるか（制作ガイド用） */
    virtual bool CountsAsGround() const { return false; }
    /** @brief ただの置物か（デバッグ表示で敵や仕掛けと色を分ける） */
    virtual bool IsDecoration() const { return false; }
    /** @brief 有効化フラグが無い時も経過時間を進めるか（動きや回転の演出に使う） */
    virtual bool AlwaysAdvancesTimer() const { return false; }
    /** @brief 画面座標に置かれているか（3D空間へ投影しない） */
    virtual bool IsScreenSpace(const ObjectDesc&) const { return false; }
    /** @brief 時間で動く仕掛けなら、その動き方を返す（動かない種類はnullptr） */
    virtual const GimmickMotion* MotionOf(const ObjectDesc&) const { return nullptr; }

    // 実行時の振る舞い（indexはStageEditorが持つ配置物の番号）

    /** @brief 実体の作り直し時に、敵の実体を用意する（敵を置かない種類は敵の実体を片付ける） */
    virtual void PrepareEnemyInstance(StageEditor& editor, int index) const;
    /** @brief 有効化の判定後に、種類固有の理由でまだ有効でいられるか（カメラの維持時間切れなど） */
    virtual bool StaysActive(const ObjectDesc&, float) const { return true; }
    /** @brief 毎フレーム、配置物の更新より先に条件を評価してGameFlagsへ反映する */
    virtual void UpdateCondition(StageEditor&, int, float) const { }
    /**
     * @brief 敵の更新より前に行う種類固有の更新
     * @return trueなら、この配置物のこのフレームの更新はここで終わり
     */
    virtual bool UpdateBeforeEnemy(StageEditor&, int, float) const { return false; }
    /**
     * @brief 敵でない配置物の、種類固有の更新（回収判定や見た目の追従）
     * @return trueなら、この配置物のこのフレームの更新はここで終わり
     */
    virtual bool UpdateOwnRuntime(StageEditor&, int, engine::graphics::ParticleManager*, const Vector3&) const { return false; }
    /** @brief 敵グループの全滅を監視する条件か（制作ガイドの集計用） */
    virtual bool WatchesEnemyGroup(const ObjectDesc&) const { return false; }

    // 振る舞い

    /** @brief 階層一覧で名前の前に付けるタグ（無ければ空文字） */
    virtual const char* ListTag() const { return ""; }
    /** @brief 状態をGameFlagsへ書き出す時のフラグ名の接頭辞（書き出さない種類はnullptr） */
    virtual const char* RuntimeFlagPrefix() const { return nullptr; }
    /** @brief エディタで新しく置いた時の初期値を入れる @param serial 名前の連番 */
    virtual void InitializeNewDesc(ObjectDesc& desc, int serial) const;
    /** @brief 種類固有の設定ミスを issues へ追加する */
    virtual void Validate(const ObjectDesc&, std::vector<std::string>&) const { }
#ifdef USE_IMGUI
    /** @brief デバッグ表示で判定範囲や可動域を描く */
    virtual void DrawDebugRange(const ObjectDesc&, const Vector3&) const { }
    /** @brief 見た目パネルの先頭に種類の説明を出す */
    virtual void DrawVisualNote() const { }
    /** @brief 見た目パネルの末尾に種類固有の項目を出す */
    virtual void DrawVisualExtras(StageEditor&, ObjectDesc&) const { }
    /** @brief ゲーム動作パネルに種類固有の項目を出す */
    virtual void DrawGameplayInspector(StageEditor&, int, ObjectDesc&, bool&, const UndoCapture&) const { }
    /** @brief ゲーム中の状態パネルに種類固有の状態を出す */
    virtual void DrawRuntimeStatus(const StageEditor&, int) const { }
#endif

protected:
    explicit ObjectKind(const char* name)
        : name_(name)
    {
    }
    /** @brief 名前を「接頭辞+連番」にする */
    static void NameWithSerial(ObjectDesc& desc, const char* prefix, int serial);
    /** @brief 名前を「接頭辞+連番」にし、同名の有効化フラグ（名前_active）も付ける */
    static void NameWithActivationFlag(ObjectDesc& desc, const char* prefix, int serial);

    // StageEditorの内部へ触る窓口（ObjectKindだけがStageEditorのfriend）
    static const ObjectDesc& DescAt(const StageEditor& editor, int index);
    static float RuntimeTimer(const StageEditor& editor, int index);
    static float& MutableRuntimeTimer(StageEditor& editor, int index);
    static engine::graphics::Camera* EditorCamera(StageEditor& editor);
    static void RefreshTransforms(StageEditor& editor, int index);
    static Vector3 WorldPosition(const StageEditor& editor, const ObjectDesc& desc);
    static bool IsRuntimeActive(const StageEditor& editor, int index);
    static bool IsEditingView(const StageEditor& editor);
    static bool HasEnemyInstance(const StageEditor& editor, int index);
    static void RegenerateInstances(StageEditor& editor, int index);
    /** @brief 敵の実体を片付ける（keep～がtrueの方は残す） */
    static void ClearEnemyInstances(StageEditor& editor, int index, bool keepKnight, bool keepBasic);
    /** @brief ナイトの実体が無ければ作る */
    static void EnsureKnightInstance(StageEditor& editor, int index);
    /** @brief 一般敵の実体が無ければ作り、EnemyRegistryへ登録する */
    static void EnsureBasicEnemyInstance(StageEditor& editor, int index);
    /** @brief 回収済みでなければ回収判定と演出を進める */
    static void UpdatePickup(StageEditor& editor, int index, engine::graphics::ParticleManager* pm, const Vector3& playerPos);
    /** @brief 壊れていなければ見た目を配置へ追従させる */
    static void RefreshUnbrokenInstances(StageEditor& editor, int index);
    /** @brief 敵グループの敵が1体以上いて、全員倒されたか（出現待ちの敵がいれば未達） */
    static bool IsEnemyGroupDefeated(const StageEditor& editor, const std::string& group);

#ifdef USE_IMGUI
    // 複数の種類で共有するインスペクタ項目（並び順はどの種類でも同じになるよう、呼ぶ側で順に並べる）
    static void DrawActivationFlagField(ObjectDesc& desc, const UndoCapture& capture);
    static void DrawEnemyGroupField(ObjectDesc& desc, const UndoCapture& capture);
    static void DrawPatrolRouteField(ObjectDesc& desc, const UndoCapture& capture);
    static void DrawEnemyLooksFields(StageEditor& editor, ObjectDesc& desc, bool& structuralDirty);

    static void RecordUndo(StageEditor& editor);
    static bool PickupCollected(const StageEditor& editor, int index);
    static int BreakableHp(const StageEditor& editor, int index);
    static bool BreakableDestroyed(const StageEditor& editor, int index);
#endif

private:
    const char* name_;
};

} // namespace engine::game
