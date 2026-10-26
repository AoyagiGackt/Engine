/// @file Player.h
/// @brief プレイヤーキャラクターの物理・アクション・スタイルシステムを定義するファイル
#pragma once
#include "AfterImageRenderer.h"
#include "IPositionTarget.h"
#include "AutoComboAssist.h"
#include "Animation.h"
#include "CollisionConfig.h"
#include "GunCombo.h"
#include "ComboMotion.h"
#include "MeleeCombo.h"
#include "Model.h"
#include "Object3d.h"
#include "Skeleton.h"
#include "SkinCommon.h"
#include "SkinnedModel.h"
#include "SkinnedObject3d.h"
#include <algorithm>
#include <bitset>
#include <memory>
#include <vector>
namespace engine {
class Input;
}
namespace engine::graphics {
class ModelCommon;
}

namespace engine::game {
using engine::AABB;
using engine::Collider;
using engine::Input;
using engine::graphics::Model;
using engine::graphics::ModelCommon;
using engine::graphics::Object3d;
using engine::graphics::Skeleton;
using engine::graphics::SkinCommon;
using engine::graphics::SkinnedModel;
using engine::graphics::SkinnedObject3d;

enum class WeaponType; // Weapon.h で定義
enum class GunType; // Weapon.h で定義

/// @brief プレイヤーキャラクターを制御するクラス
/// @note スタイリッシュアクション（コンボ・ブリンク・連射・覚醒乱舞）と
/// ローグライト用のスキル補正（SkillMods）を統合管理する
class Player : public IPositionTarget {
public:
    // ══════════════════════════════════════════════════════
    // 公開型とライフサイクル
    // ══════════════════════════════════════════════════════

    /// @brief ローグライトのスキルによる各種パラメータ補正を保持する構造体
    /// @note RunData のスキル一覧を ApplySkillMods() に渡して適用する
    struct SkillMods {
        float blinkDistMult = 1.0f; ///< ブリンク距離の倍率
        int comboMaxBonus = 0; ///< コンボ最大数への加算
        float fireIntervalMult = 1.0f; ///< 連射間隔の倍率（<1 = 速く）
        float gaugeChargeMult = 1.0f; ///< 覚醒ゲージ蓄積量の倍率
        float speedMult = 1.0f; ///< 移動速度の倍率
        float jumpMult = 1.0f; ///< ジャンプ力の倍率
        int juggleMaxBonus = 0; ///< 乱舞スラッシュ数への加算
    };

    /// @brief プレイヤーを初期化する
    /// @param modelCommon モデル共通設定のポインタ
    void Initialize(ModelCommon* modelCommon);

    /// @brief 入力に基づいてプレイヤーの物理とアクションを毎フレーム更新する
    /// @param input    入力マネージャー
    /// @param enemyPos 乱舞スラッシュのターゲット座標（デフォルトは原点）
    void Update(Input* input, const Vector3& enemyPos = { }, bool enemySolid = false, bool useStageFloor = false);

    /** @brief プレイヤーモデルを描画する */
    void Draw();

    /// @brief 見た目のトランスフォーム行列だけを再計算する（ゲームロジックは一切進めない）
    /// @note Object3dのUpdate()はカメラのVP行列込みで定数バッファを書くため、
    /// ステージエディタ中など本体Updateを止めたままカメラだけ動く状況でこれを呼ばないと
    /// 古いカメラ行列のまま描画されて、モデルが画面に張り付いて見える
    void RefreshVisualTransforms();
    /**
     * @brief 演出用に、入力と物理を進めずに見た目だけを通常どおり進める（向き・待機/落下のアニメ・武器の付け方）
     * @param airborne 宙にいるなら true（落下のアニメにする）
     * @param bodyRotation 向きの回転に足す体の回転（ラジアン。x を負にすると後ろへのけぞる）
     * @param moving 歩行アニメを選ぶなら true（既定は待機）
     * @note 開始演出の降下など、シーンが位置を直接動かしている間に毎フレーム呼ぶ
     */
    void UpdateCinematicPose(bool airborne, const Vector3& bodyRotation = {}, bool moving = false);
    /** @brief 入口演出用に本体の倍率と本体・装備の透明度を設定する */
    void SetPortalAppearance(float scale, float alpha);
    /** @brief やられ演出を始める（攻撃モーションを打ち切り、武器を手から放して構えないアニメにする。銃は隠す） */
    void BeginDefeatPose();
    /** @brief 手放した近接武器の位置とZ軸回りの回転を決める（BeginDefeatPose の後、UpdateCinematicPose の前に毎フレーム呼ぶ） */
    void SetDroppedWeaponPose(const Vector3& position, float spinZ);
    /** @brief 演出中のアニメの再生速度（1=通常、0で止める。倒れたまま動かなくする時は0） */
    void SetCinematicAnimationSpeed(float speed);
    /** @brief 演出の間だけ、加算モーションの代わりに掛けるポーズレイヤー（nullptr で加算モーションへ戻す。レイヤーは呼び出し側が持ち続ける） */
    void SetCinematicPoseLayer(const engine::graphics::IPoseLayer* layer);
    /**
     * @brief 乱舞中でない時、渡した敵のどれとも一定距離以上重ならないようX座標を押し出す
     * @note プレイヤーが敵を押しのけず、敵の手前で止まるようにする（地形との判定 ResolveBlockCollision より前に呼ぶ）
     */
    void ResolveEnemyOverlaps(const std::vector<Vector3>& enemyPositions);

    /// @brief ステージ上のsolidブロックとの当たり判定を解決する（Update()の後に毎フレーム呼ぶ）
    /// @param blocks StageEditor::GetSolidColliders()等で得たワールドAABB一覧
    /// @note 上に乗れば着地・横から当たれば壁として押し出す。ブロックを動かせば次フレームから追従する
    void ResolveBlockCollision(const std::vector<AABB>& blocks);
    /**
     * @brief 指定した位置の真下にある床へ立った時の位置を返す（足元より下で一番高いブロックの上面に足を合わせる）
     * @return 真下に床が無ければ、指定した位置をそのまま返す
     * @note 開始演出の着地点など、物理を進めずに床へ立たせたい時に使う
     */
    Vector3 FindStandingPosition(const Vector3& position, const std::vector<AABB>& blocks) const;

    /** @brief 乱舞フェーズを強制終了する（外部から撃破時などに呼ぶ） */
    void EndRampage();

    /** @brief 武器奪取の刺突モーションを頭から再生する（外部トリガー用、既存の斬撃を流用） */
    void PlayStealStab();

    Player();
    ~Player();
    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;

    /// @brief ローグライトのスキル補正を適用する
    /// @param mods RunData のスキル一覧から計算した補正値
    void ApplySkillMods(const SkillMods& mods) {
        skillMods_ = mods;
    }

    /** @brief 現在のワールド座標を返す */
    Vector3 GetPosition() const override {
        return pos_;
    }
    /** @brief スポーン位置を上書きする（Initialize 直後に呼ぶこと） */
    void SetPosition(const Vector3& pos) override {
        pos_ = pos;
    }
    void SetHorizontalBounds(float minX, float maxX);
    /// @brief 水面のY座標を設定する（WaterPool::GetSurfaceY() の値を渡す）
    /// @note 呼ばない場合は水中判定が無効のまま（水なしステージ用のデフォルト）
    void SetWaterLevel(float waterLevelY) {
        waterLevel_ = waterLevelY;
    }
    /** @brief 残像・分身演出用の静的モデル（ボーンなし、現在のフォームの見た目）のポインタを返す */
    Model* GetModel() const {
        return rig_->staticModel;
    }

    /// @brief プレイヤーの現在位置から AABB コライダーを生成して返す
    /// @return 当たり判定に使用する Collider
    Collider GetCollider() const;
    /// @brief 被弾時の無敵時間を開始する
    /// @note 敵の攻撃判定がヒットした際に GamePlayScene 側から呼ぶ
    void OnHit();
    /** @brief 被弾直後の無敵時間中、または回避の無敵中かどうかを返す */
    bool IsInvincible() const {
        return invincibleTimer_ > 0.0f || dodgeActive_ || warpActive_;
    }

    // ── 回避（前後ステップ。全区間無敵、コンボからキャンセル可）──
    /// @brief 回避ボタンの押下を先行入力として記憶する
    /// @param input 入力マネージャー
    /// @note ヒットストップ中はUpdate()が呼ばれず押下が捨てられるため、シーン側から毎フレーム（停止中も）呼ぶ。
    /// 記憶した押下はkDodgeBufferFrames_の間だけ有効で、次にUpdate()が回った時に回避として消費される
    void BufferDodgeInput(Input* input);
    /** @brief 回避モーション中か（この間は敵の攻撃を受けない） */
    bool IsDodging() const {
        return dodgeActive_;
    }
    /** @brief このフレームに回避を開始したか */
    bool JustDodged() const {
        return HasFrameEvent(FrameEvent::kDodged);
    }
    /// @brief 直近の回避で連続回避（スパム）が何回続いているかを返す
    /// @note 0なら間隔を空けた回避。2以上は危険のない場面での連打とみなしてスタイル評価を下げる材料にする
    int GetDodgeSpamCount() const {
        return dodgeSpamCount_;
    }
    /// @brief 回避中に敵の攻撃を紙一重で避けた（ジャスト回避）ことを1回だけ成立させる
    /// @return 今回の回避でまだ報酬を受け取っていなければtrue（呼び出し側が加点・演出を行う）
    bool ConsumeJustDodge();
    /// @brief ジャスト回避直後の強化窓を開く（この間の攻撃は威力が上がる。回避を攻めの起点にするため）
    /// @param seconds 強化が続く秒数
    void BeginJustDodgeWindow(float seconds) {
        justDodgeWindowTimer_ = (std::max)(justDodgeWindowTimer_, seconds);
    }
    /** @brief ジャスト回避直後の強化窓の中か */
    bool IsJustDodgeWindowActive() const {
        return justDodgeWindowTimer_ > 0.0f;
    }

    // ── テレポート斬り（機動アクション。通常時は覚醒ゲージ少量消費+クールタイム、
    //    覚醒中はゲージ消費なし・距離延長・クールタイム短縮の強化版）──
    /** @brief テレポート中か（この間は敵の攻撃を受けない） */
    bool IsWarping() const {
        return warpActive_;
    }

    /// @brief 現在の武器と覚醒状態から攻撃ダメージ倍率を返す（覚醒中はweapons.jsonのawakened.damageMult）
    /// @note 素手・非覚醒時は1.0
    float GetAwakenedDamageMult() const;
    /** @brief 現在の武器と覚醒状態から固有技の判定範囲倍率を返す（非覚醒時は1.0） */
    float GetAwakenedSkillRadiusMult() const;
    /** @brief 現在の武器と覚醒状態からノックバック倍率を返す（非覚醒時は1.0） */
    float GetAwakenedKnockbackMult() const;

    /** @brief 地面に立っているかを返す */
    bool IsOnGround() const {
        return onGround_;
    }
    /** @brief 見た目の種類を設定する */
    void SetVisualPreset(int preset) {
        visualPreset_ = std::clamp(preset, -1, 1);
    }
    /** @brief 見た目の種類を返す */
    int GetVisualPreset() const {
        return visualPreset_;
    }
    /** @brief 差し替え用の静的モデル（とテクスチャ）を設定する。texturePathを省略すると白テクスチャになる */
    void SetStaticVisualModel(const std::string& modelPath, const std::string& texturePath = "");
    /** @brief 固定の見た目に使うモデルのパスを返す */
    const std::string& GetStaticVisualModelPath() const {
        return staticOverrideModelPath_;
    }
    /** @brief 固定の見た目に使うテクスチャのパスを返す */
    const std::string& GetStaticVisualTexturePath() const {
        return staticOverrideTexturePath_;
    }
    /** @brief このフレームにジャンプしたかを返す */
    bool JustJumped() const {
        return HasFrameEvent(FrameEvent::kJumped);
    }
    /** @brief このフレームに着地したかを返す */
    bool JustLanded() const {
        return HasFrameEvent(FrameEvent::kLanded);
    }
    /** @brief このフレームに水へ入ったかを返す */
    bool JustEnteredWater() const {
        return HasFrameEvent(FrameEvent::kEnteredWater);
    }
    /** @brief このフレームに水から出たかを返す */
    bool JustExitedWater() const {
        return HasFrameEvent(FrameEvent::kExitedWater);
    }

    /** @brief 覚醒ゲージの現在値を返す（0.0〜1.0） */
    float GetAwakenGauge() const {
        return awakenGauge_;
    }
    /** @brief 覚醒状態かどうかを返す */
    bool IsAwakened() const {
        return isAwakened_;
    }

    /// @brief 攻撃が実際に命中した際にシーン側の当たり判定から呼び、覚醒ゲージを加算する
    /// @note 空振りでは溜まらないよう、振った瞬間ではなく命中確定フレームで呼ぶこと
    /// @param amount 加算量（skillMods_.gaugeChargeMult 込みで計算される）
    void ChargeAwakenGauge(float amount);

    // スタイル技フラグ（その1フレームだけ true）
    /** @brief このフレームにコンボの攻撃が当たったかを返す */
    bool JustComboHit() const {
        return HasFrameEvent(FrameEvent::kComboHit);
    }
    /** @brief このフレームのコンボヒットが覚醒中の追加ヒット（多段の2発目以降）か */
    bool JustComboExtraHit() const {
        return HasFrameEvent(FrameEvent::kComboExtraHit);
    }
    /** @brief このフレームに武器の持ち替え攻撃が当たったかを返す */
    bool JustWeaponSwitchHit() const {
        return HasFrameEvent(FrameEvent::kWeaponSwitchHit);
    }
    /** @brief 今のコンボの段を返す（1始まり。打ち上げ技は0） */
    int GetComboStep() const {
        return comboStep_;
    }
    /** @brief このフレームに近接攻撃の段を振り始めたかを返す */
    bool JustStartedSwing() const {
        return HasFrameEvent(FrameEvent::kSwingStarted);
    }
    /** @brief このフレームに銃を撃ったかを返す */
    bool JustFired() const {
        return HasFrameEvent(FrameEvent::kFired);
    }
    /** @brief このフレームに覚醒したかを返す */
    bool JustAwakened() const {
        return HasFrameEvent(FrameEvent::kAwakened);
    }
    /** @brief 最後に入力した左右の向きを返す（+1=右、-1=左） */
    float GetLastDirX() const {
        return lastDirX_;
    }
    /** @brief 指定位置の方向へ向きだけ強制的に振り向かせる（ロックオン中、移動入力に関係なく敵の方を向かせる用） */
    void FaceTarget(const Vector3& targetPos) {
        lastDirX_ = (targetPos.x >= pos_.x) ? 1.0f : -1.0f;
    }
    /** @brief スキル補正込みのコンボ最大数を返す（現在の武器の地上コンボ段数基準） */
    int GetComboMax() const;

    /// @brief 進行中の近接攻撃の定義を返す（攻撃していなければ nullptr）
    /// @note JustComboHit() のフレームにダメージ・ノックバック・打ち上げ・技IDの参照に使う
    const MeleeAttackDef* GetActiveMeleeAttack() const {
        return meleeCombo_.GetActive();
    }
    /** @brief 近接コンボのモーション中か */
    bool IsMeleeAttacking() const {
        return meleeCombo_.IsAttacking();
    }
    /** @brief 固有技の連撃（剣舞など）を自動で出し切っている最中か（武器の疲労の対象外にする判定に使う） */
    bool IsSkillSequenceActive() const {
        return meleeCombo_.IsSequenceActive();
    }
    /// @brief 現在表示中の近接武器の手元ワールド座標を返す（武器トレイル演出用）
    /// @note 武器が表示されていない場合はプレイヤーの胸元あたりの位置を返す
    Vector3 GetActiveWeaponWorldPosition() const;
    /**
     * @brief 手に持っている近接武器の種類と、武器モデル空間→ワールドの行列を返す（刃の軌跡演出用）
     * @param outType   表示中の武器の種類
     * @param outMatrix 武器モデルのローカル座標に掛けるとワールド座標になる行列
     * @return 武器を手のボーンに持っている時だけtrue（非表示・投擲中はfalse）
     */
    bool GetHeldWeaponTransform(WeaponType& outType, Matrix4x4& outMatrix) const;

    /**
     * @brief 近接の段を指定時刻の姿勢で見せる（モーション編集のプレビュー用）
     * @param attack         見せる段
     * @param normalizedTime 段の経過割合（0〜1）
     * @note Update() を呼ばずに見た目だけを評価する。ヒット・前進・入力は一切発生しない
     */
    void PreviewMeleePose(const MeleeAttackDef* attack, float normalizedTime);
    /** @brief プレビューを終えて攻撃していない状態へ戻す */
    void EndMeleePreview();
    /** @brief 加算モーションの保存キー（今表示しているリグのモデルファイル名） */
    const char* GetMotionRigId() const {
        return rig_->motionRigId;
    }
    /** @brief 加算モーションの掛かり具合（確認表示用）。トラック数と、そのうちリグにボーンが見つかった数 */
    void GetMotionLayerStats(int& outTracks, int& outApplied) const {
        outTracks = motionLayer_.GetLastTrackCount();
        outApplied = motionLayer_.GetLastAppliedCount();
    }
    /** @brief 今表示しているリグのスケルトン（ボーン一覧の表示用） */
    const engine::graphics::Skeleton& GetRigSkeleton() const {
        return rig_->object->GetSkeleton();
    }

    /// @brief 進行中の射撃コンボ段の定義を返す（撃っていなければ nullptr）
    /// @note JustFired() のフレームに弾数・拡散・射程倍率・ノックバック・技IDの参照に使う
    const GunShotDef* GetActiveGunShot() const {
        return gunCombo_.GetActive();
    }
    /** @brief 現在の射撃コンボの段数を返す（1始まり） */
    int GetGunComboStep() const {
        return gunCombo_.GetStep();
    }

    // スペースキー スピン連射
    /** @brief このフレームにスピン連射の弾を出したかを返す */
    bool JustSpinShot() const {
        return HasFrameEvent(FrameEvent::kSpinShot);
    }
    /** @brief 逆さまになっているかを返す */
    bool IsUpsideDown() const {
        return isUpsideDown_;
    }
    /** @brief 空中での回転角を返す（度。0=正立、180=逆さ） */
    float GetSpinAngle() const {
        return spinAngle_;
    }

    // 固有技（スペースキー、武器種別ごと。Dagger/Hammer/Ball は上の Just～ を使う）
    /** @brief このフレームに大剣・ハンマーの大地砕きが出たかを返す */
    bool JustGreatswordSlam() const {
        return HasFrameEvent(FrameEvent::kGreatswordSlam);
    }
    /** @brief グレートソード: 投げ回転斬りで大剣が静止し渦を巻いている最中か（飛行中・帰還中はfalse） */
    bool IsGreatswordSpinning() const;
    /** @brief このフレームに投げた大剣の渦の多段ヒットが出たかを返す */
    bool JustGreatswordSpinHit() const {
        return HasFrameEvent(FrameEvent::kGreatswordSpinHit);
    }
    /** @brief このフレームに大剣を投げたかを返す（発射の演出用） */
    bool JustGreatswordThrown() const {
        return HasFrameEvent(FrameEvent::kGreatswordThrown);
    }
    Vector3 GetGreatswordThrowPos() const; ///< 投げた大剣が静止している位置（渦の中心）
    /** @brief このフレームに斧の突進が出たかを返す */
    bool JustAxeCharge() const {
        return HasFrameEvent(FrameEvent::kAxeCharge);
    }
    bool IsAxeEnraged() const; ///< アックス: 突進後の怒り強化中か
    /** @brief アックスの怒り強化中に攻撃力へ掛けるべき倍率を返す（強化中でなければ1.0） */
    float GetAxeRageMult() const;
    /** @brief このフレームに鎌の回転を始めたかを返す */
    bool JustScytheSpin() const {
        return HasFrameEvent(FrameEvent::kScytheSpin);
    }
    /** @brief このフレームに固有技の攻撃判定が出たか（叩きつけ・スピン連射・鎌の回転・斧の突進のどれか） */
    bool JustWeaponSkillHit() const;

    // 覚醒乱舞（Sword + 覚醒 + L）
    /** @brief 乱舞の最中かを返す */
    bool IsRampaging() const {
        return rampage_->IsActive();
    }
    /** @brief このフレームに敵を打ち上げたかを返す */
    bool JustLaunched() const {
        return HasFrameEvent(FrameEvent::kLaunched);
    }
    /** @brief このフレームに乱舞の一撃を出したかを返す */
    bool JustRampageHit() const {
        return HasFrameEvent(FrameEvent::kRampageHit);
    }
    /** @brief このフレームに乱舞の最後の一撃を出したかを返す */
    bool JustRampageFinish() const {
        return HasFrameEvent(FrameEvent::kRampageFinish);
    }
    /** @brief 乱舞の空中連撃の回数を返す */
    int GetJuggleCount() const {
        return juggleSlashCount_;
    }

    // フィニッシャースラッシュ（覚醒ゲージ満タン + F）
    /** @brief このフレームに締めの斬撃（フィニッシャー）を出したかを返す */
    bool JustFinisherSlash() const {
        return HasFrameEvent(FrameEvent::kFinisherSlash);
    }

private:
    /** @brief 装備を現在のボーンへ付け直し、武器と銃の行列を更新する */
    void UpdateAttachedWeapons();

    // ══════════════════════════════════════════════════════
    // 物理設定と状態データ
    // ══════════════════════════════════════════════════════
    // ゲームプレイの調整値は Resources/Config/player_params.json（PlayerTuning）が持つ。
    // ここに残す定数は判定の許容誤差と見た目だけ

    static constexpr Vector3 kDefaultSpawnPosition = { 8.0f, 0.4f, 0.0f };
    static constexpr float kBodyHalfExtent = 0.5f; ///< 当たり判定は pos_ を中心とした1x1x1
    // 通常ステージの左右端はStageEditorのsolid配置物で制御する
    float minX_ = -1000.0f;
    float maxX_ = 1000.0f;

    // 水中判定（水なしステージでは無効化された状態のままにする）
    static constexpr float kWaterLevelDisabled = -1.0f;

    Vector3 pos_ = kDefaultSpawnPosition;
    float waterLevel_ = kWaterLevelDisabled; // SetWaterLevel() で上書きされるまで水中判定は無効
    float velocityY_ = 0.0f;
    float groundVisualCorrection_ = 0.0f; // 実ブロック床と通常地面の高さ差を見た目だけへ反映
    bool useStageFloor_ = false; // 配置床がある場合は固定高さの地面で着地させない
    bool onGround_ = true;
    int visualPreset_ = -1; // -1=自動、0=Alien、1=Mech
    bool prevOnGround_ = true;
    float prevFrameX_ = 0.0f; ///< このフレームの移動前のX（壁にめり込んだ時に、入ってきた側へ押し戻すのに使う）

    bool inWater_ = false;
    bool prevInWater_ = false;

    // 1フレームだけ立つ出来事（毎フレーム冒頭でまとめて消す。シーン側は Just～() で読む）
    enum class FrameEvent {
        kJumped,
        kLanded,
        kEnteredWater,
        kExitedWater,
        kDodged,
        kComboHit,
        kComboExtraHit,
        kWeaponSwitchHit,
        kFired,
        kAwakened,
        kSpinShot,
        kGreatswordSlam,
        kGreatswordSpinHit,
        kGreatswordThrown,
        kAxeCharge,
        kScytheSpin,
        kLaunched,
        kRampageHit,
        kRampageFinish,
        kFinisherSlash,
        kSwingStarted,
        kCount
    };
    std::bitset<static_cast<size_t>(FrameEvent::kCount)> frameEvents_;
    /** @brief このフレームの出来事の印を立てる（on が false なら消す） */
    void SetFrameEvent(FrameEvent e, bool on = true) {
        frameEvents_.set(static_cast<size_t>(e), on);
    }
    /** @brief このフレームにその出来事が起きたかを返す */
    bool HasFrameEvent(FrameEvent e) const {
        return frameEvents_.test(static_cast<size_t>(e));
    }

    // 被弾後の無敵時間
    float invincibleTimer_ = 0.0f;

    // 回避（レーン制なので前後のみ。移動入力があればその方向、無ければ向きの逆へバックステップ）
    bool dodgeActive_ = false;
    bool dodgeRewardClaimed_ = false; // 1回の回避でジャスト回避報酬を二重に受け取らないためのフラグ
    float dodgeTimer_ = 0.0f;
    float dodgeStartX_ = 0.0f;
    float dodgeTargetX_ = 0.0f;
    float dodgeCooldown_ = 0.0f;
    float dodgeSpamTimer_ = 0.0f; // 前回の回避からの経過秒数（連打判定用）
    int dodgeSpamCount_ = 0;
    int dodgeBufferFrames_ = 0; // 先行入力した回避ボタンの残り有効フレーム数（0なら未入力）
    bool dodgeBackward_ = false; // 向きを変えずに後ろへ下がる回避か（前転モーションだと滑って見えるので専用の見せ方にする）
    float justDodgeWindowTimer_ = 0.0f; // ジャスト回避直後の強化窓の残り秒数（長さと倍率はCombatTuning側）

    // テレポート斬り（向いている方向へ短距離ワープする機動アクション。全区間無敵、コンボからキャンセル可）
    bool warpActive_ = false;
    float warpTimer_ = 0.0f;
    float warpStartX_ = 0.0f;
    float warpTargetX_ = 0.0f;
    float warpCooldown_ = 0.0f;

    // 向き（最後に入力した横方向+1=右 -1=左）
    float lastDirX_ = 1.0f;

    // 覚醒ゲージ
    float awakenGauge_ = 0.0f;
    bool isAwakened_ = false;
    float awakenTimer_ = 0.0f;

    // スタイル技（L / K キー）
    bool weaponSwitchAttackPending_ = false;
    bool weaponSwitchAttackActive_ = false;
    float weaponSwitchWindow_ = 0.0f;
    int comboStep_ = 0;

    // 近接コンボ（武器タイプ別の段・タイミング・スイングは MeleeCombo.cpp のテーブルが持つ）
    MeleeComboController meleeCombo_;
    AutoComboAssist autoCombo_; ///< オプションのオートコンボ（GameSettings::autoComboを毎フレーム反映）
    /// 段ごとの加算モーション（両リグの SkinnedObject3d が参照するので、リグより先に宣言して長く生存させる）
    ComboMotionLayer motionLayer_;
    /** @brief 今の段と経過割合を加算モーションのレイヤーへ渡す（リグを評価する直前に呼ぶ） */
    void SyncMotionLayer();
    const Animation* previewAnim_ = nullptr; ///< プレビュー中に設定済みのベースアニメ（毎フレームのクリップ複製を避ける）
    // 射撃コンボ（銃種別の段・弾数・リコイルは GunCombo.cpp のテーブルが持つ）
    GunComboController gunCombo_;
    float launchFollowTimer_ = 0.0f; ///< 打ち上げ直後の追撃ジャンプ強化の残り秒数

    // 体の空中回転（ボールの固有技で使う。度、0=正立 180=逆さ）
    bool isUpsideDown_ = false;
    float spinAngle_ = 0.0f;

    // 固有技で共通の水平ダッシュ（瞬間移動に見えないよう、開始位置→目標位置へ短時間でイージング移動する）
    // ヒット判定は移動完了フレームで発生させること
    struct DashMotion {
        bool active = false;
        float timer = 0.0f;
        float startX = 0.0f;
        float targetX = 0.0f;
    };
    /** @brief 現在位置からworldDeltaXぶん先（minX_/maxX_でクランプ）へダッシュを開始する */
    void BeginDash(DashMotion& dash, float worldDeltaX);
    /** @brief ダッシュを1フレーム進める（pos_.xを更新）。ちょうど完了したフレームだけtrueを返す */
    bool AdvanceDash(DashMotion& dash);

    // 覚醒乱舞（Sword + 覚醒 + L）
    int juggleSlashCount_ = 0; // 今回の乱舞で何回切ったか
    int juggleAngleIdx_ = 0; // 次のスラッシュ角度インデックス

    // リムライト（本体・手持ち武器で共通）
    static constexpr Vector3 kRimColor = { 0.4f, 0.9f, 1.0f };
    static constexpr float kRimPower = 2.5f;
    static constexpr float kRimIntensity = 1.2f;
    static constexpr float kFinisherRimBoost = 3.0f; // フィニッシャーの溜めが深まるほど足すリムの強さ

    // ブロックとの当たり判定の許容値
    static constexpr float kTopCrossTolerance = 0.01f; // 上面を跨いだとみなす高さの誤差
    static constexpr float kSideOverlapMargin = 0.05f; // 側面判定で上下の縁を無視する幅
    static constexpr float kLandingSnapTolerance = 0.05f; // 上面へ吸着させる高さの許容

    // 体の旋回（度）
    static constexpr float kFullTurnDegrees = 360.0f;
    static constexpr float kUpsideDownMinDegrees = 90.0f; // この範囲の角度では逆さとみなす
    static constexpr float kUpsideDownMaxDegrees = 270.0f;

    // スキル補正（ローグライト）
    SkillMods skillMods_;

    // Physics State パターン
    // 水中/水上で横移動・重力・ジャンプの処理を切り替える
    /** @brief 環境ごとの移動と重力処理を抽象化する物理状態 */
    class IPhysicsState {
    public:
        /** @brief 派生クラスを基底クラスから正しく破棄できるようにする仮想デストラクタ */
        virtual ~IPhysicsState() = default;
        /** @brief 水中・地上それぞれの移動と重力を1フレーム進める */
        virtual void Update(Player& player, Input* input) const = 0;
    };
    /** @brief 地上環境の移動と重力処理を適用する物理状態 */
    class GroundedPhysicsState;
    /** @brief 水中環境の浮力と移動処理を適用する物理状態 */
    class UnderwaterPhysicsState;
    static const IPhysicsState& GetPhysicsState(bool inWater);

    // Rampage State パターン
    // 覚醒乱舞の進行フェーズ（未発動/打ち上げ/空中追撃）ごとに攻撃入力の意味と
    // 毎フレームの物理更新内容を切り替える
    /** @brief 覚醒乱舞の段階固有処理を抽象化する状態 */
    class IRampageState {
    public:
        /** @brief 派生クラスを基底クラスから正しく破棄できるようにする仮想デストラクタ */
        virtual ~IRampageState() = default;
        /** @brief 乱舞の段階ごとに攻撃入力を扱う */
        virtual void HandleAttackInput(Player& player, Input* input, const Vector3& enemyPos) const = 0;
        /** @brief 乱舞の段階ごとにプレイヤーの動きを進める */
        virtual void UpdatePhysics(Player& player, const Vector3& enemyPos) const = 0;
        /** @brief 乱舞中か（未発動状態だけfalse） */
        virtual bool IsActive() const {
            return true;
        }
        /** @brief 空中追撃の段階か */
        virtual bool IsJuggling() const {
            return false;
        }
    };
    /** @brief 覚醒乱舞を開始していない通常状態 */
    class InactiveRampageState;
    /** @brief 覚醒乱舞の打ち上げ段階を処理する状態 */
    class LaunchRampageState;
    /** @brief 覚醒乱舞の空中追撃段階を処理する状態 */
    class JuggleRampageState;
    // 各段階の共有インスタンス（段階はステートレスで、状態クラスの定義はPlayerRampageStates.cppに閉じる）
    static const IRampageState& InactiveRampage();
    static const IRampageState& LaunchRampage();
    static const IRampageState& JuggleRampage();
    const IRampageState* rampage_ = &InactiveRampage(); ///< 覚醒乱舞の進行フェーズ

    // 覚醒残像
    AfterImageRenderer afterImageRenderer_;

    // アウトラインパス後に通常描画 PSO へ戻すために保持（Player::Draw() で使用）
    ModelCommon* modelCommon_ = nullptr;
    Model* staticOverrideModel_ = nullptr; // 実体はModelManagerが所有・共有する
    std::unique_ptr<Object3d> staticOverrideObject_;
    float staticOverrideFootOffset_ = 0.0f; // モデル原点から最下点までの距離（スケール後）中心原点のモデルでも足元を合わせる
    std::string staticOverrideModelPath_;
    std::string staticOverrideTexturePath_;

    // スキンメッシュ描画の共通設定（両フォームのリグで共有）
    std::unique_ptr<SkinCommon> skinCommon_;

    // 見た目1体ぶんのリグ。通常時と覚醒中でモデルごと差し替えるため、
    // アニメーションや武器アタッチ先ボーン名などモデル依存の情報をセットで持つ
    // （アセットパス・アニメ名の定義は CharacterVisuals.h の kNormalRigVisual / kAwakenedRigVisual）
    /** @brief 通常/覚醒フォーム1体ぶんのモデル・アニメーション・武器アタッチ用ボーン名一式 */
    struct CharacterRig {
        Model* staticModel = nullptr; ///< 残像・分身演出用（ボーンなし、本体と同じ見た目）。実体はModelManagerが所有・共有する
        SkinnedModel* skinnedModel = nullptr; ///< 本体描画（ボーンアニメーション付き）。実体はModelManagerが所有・共有する
        std::unique_ptr<SkinnedObject3d> object;
        float modelScale = 1.0f;
        float modelOffsetY = 0.0f; ///< モデル原点（足元）を AABB 中心の pos_ に合わせる下げ幅
        const char* meleeBoneName = ""; ///< 近接武器のアタッチ先ボーン
        const char* gunBoneName = ""; ///< 銃のアタッチ先ボーン
        const char* motionRigId = ""; ///< 加算モーションの保存キー（リグのモデルファイル名）
        Animation idleAnim;
        Animation runAnim;
        Animation jumpAnim;
        Animation runningJumpAnim; ///< 移動しながらのジャンプ
        Animation swimAnim; ///< 水中
        Animation idleHoldAnim; ///< 武器持ち待機（構え系スタイル）
        Animation runHoldAnim; ///< 武器持ち走り（構え系スタイル）
        Animation slashAnim; ///< 斬撃（ソード攻撃・乱舞・フィニッシャー）
        Animation punchAnim; ///< パンチ（ソード以外のスタイルのコンボ）
    };
    CharacterRig normalRig_; ///< 通常フォーム
    CharacterRig awakenedRig_; ///< 覚醒フォーム（メカ）
    CharacterRig* rig_ = &normalRig_; ///< 現在表示中のリグ（覚醒の開始/終了で切り替え）

    // Weapon Behavior Strategy パターン
    // 武器種別ごとのスペースキー挙動（固有技の連撃・投げ回転斬り・滞空ホバーなど）を切り替える。
    // 技ごとのクールタイムや進行中の状態は各Behaviorが持ち、Playerには下の固有技から使う操作だけで触る
    /** @brief 装備武器ごとの固有技を抽象化するStrategy（プレイヤー1人につき武器種ぶんのインスタンスを持つ） */
    class IWeaponBehavior {
    public:
        /** @brief 派生クラスを基底クラスから正しく破棄できるようにする仮想デストラクタ */
        virtual ~IWeaponBehavior() = default;
        /** @brief 装備している間だけ毎フレーム呼ぶ（固有技の入力と発生） */
        virtual void Update(Player& player, Input* input) = 0;
        /**
         * @brief 装備に関係なく毎フレーム呼ぶ（クールタイムの消化、手元を離れた技の進行）
         * @param cooldownStep このフレームに減らすクールタイム（覚醒中の短縮込み）
         */
        virtual void Tick(Player&, Input*, float cooldownStep) {
            (void)cooldownStep;
        }
        /** @brief 回避・ワープで踏み込み系の動きを打ち切る */
        virtual void Interrupt() { }
        /** @brief 武器を持ち替えた時に進行中の技をすべて仕切り直す */
        virtual void OnUnequipped() {
            Interrupt();
        }
        /** @brief 着地した（空中で使い切る技の回数を戻す） */
        virtual void OnLanded() { }
        /** @brief 武器が手元を離れていて近接コンボを出せない状態か */
        virtual bool IsDisarmed() const {
            return false;
        }
        /**
         * @brief 手元を離れて飛んでいる武器の見た目の位置と回転
         * @return 飛んでいる間だけtrue（持ち替え後も描き続ける）
         */
        virtual bool GetThrownPose(Vector3& outPosition, float& outSpinZ) const {
            (void)outPosition;
            (void)outSpinZ;
            return false;
        }
        /** @brief 攻撃ボタンで通常コンボの代わりに覚醒乱舞を始めるか */
        virtual bool StartsRampage(const Player&) const {
            return false;
        }
        /** @brief 固有技の連撃の締めで地面を叩き、衝撃波を出すか */
        virtual bool SlamsOnSkillFinish() const {
            return false;
        }
        /** @brief 体の空中回転を保ったままにするか。false ならこのフレームで正立に戻す */
        virtual bool KeepsSpin(const Player&) const {
            return false;
        }
    };
    /** @brief 固有技の連撃（剣舞・百裂突き・大車輪など）を出すStrategy */
    class SkillSequenceBehavior;
    /** @brief 短剣: 地上は固有技の連撃、空中は1回だけの空中ダッシュ */
    class DaggerBehavior;
    /** @brief ボール: スピン連射と空中回転 */
    class BallBehavior;
    /** @brief 大剣: 投げ回転斬り */
    class GreatswordBehavior;
    /** @brief 鎌: 滞空ホバー */
    class ScytheBehavior;
    /** @brief 斧: バーサーク突進 */
    class AxeBehavior;
    /** @brief 武器種ごとのBehaviorの実体一式（定義は PlayerWeaponBehaviors.cpp） */
    struct WeaponBehaviorSet;
    std::unique_ptr<WeaponBehaviorSet> weaponBehaviors_;
    IWeaponBehavior& BehaviorFor(WeaponType type);
    const IWeaponBehavior& BehaviorFor(WeaponType type) const;
    /** @brief 全武器のBehaviorの踏み込み系の動きを打ち切る（回避・ワープ用） */
    void InterruptWeaponSkills();
    /** @brief 全武器のBehaviorの進行中の技を仕切り直す（持ち替え用） */
    void ResetWeaponSkills();
    /** @brief 全武器のBehaviorのクールタイムと、手元を離れた技を進める */
    void TickWeaponSkills(Input* input, float cooldownStep);
    /** @brief 着地を全武器のBehaviorへ知らせる */
    void NotifyWeaponSkillsLanded();

    // 固有技から使う操作（Behaviorはこれらを通してPlayerに触る）
    /** @brief 空中で縦の速度を止める */
    void StopVerticalMotion() {
        velocityY_ = 0.0f;
    }
    /** @brief 落下速度を minVelocityY より速くならないよう抑える */
    void LimitFallSpeed(float minVelocityY) {
        velocityY_ = (std::max)(velocityY_, minVelocityY);
    }
    /** @brief 今のリグの指定クリップを攻撃モーションとして再生する */
    void PlaySkillAnim(Animation CharacterRig::* clip, float speed);
    /** @brief 体を空中で回す（1周したら0へ戻す） */
    void AddSpin(float degrees);
    /** @brief 投げた武器などを浮かせる高さの位置（足元の pos_ から胸の高さへ上げた位置） */
    Vector3 GetSkillHoldPosition() const;
    /** @brief ステージの左右端に収めたX座標 */
    float ClampToStageX(float x) const {
        return std::clamp(x, minX_, maxX_);
    }
    /** @brief 習得済みの技による強化の値を返す */
    const SkillMods& GetSkillMods() const {
        return skillMods_;
    }

    /**
     * @brief Spaceが押され、クールタイムが明けていれば、武器の固有技の連撃を始める
     * @param input           入力
     * @param type            武器タイプ（連撃表の選択に使う）
     * @param cooldown        その武器の固有技クールタイム（始めたらcooldownSecondsを入れる）
     * @param cooldownSeconds 次に出せるまでの秒数
     * @return 連撃を始めたらtrue
     */
    bool TryStartSkillSequence(Input* input, WeaponType type, float& cooldown, float cooldownSeconds);

    // 右手ボーンに持たせる近接武器（現在のスタイルに対応する1つだけ表示）
    // 種類が多いため個別メンバーではなくテーブルで持つ（追加は CharacterVisuals.h の kHeldWeaponVisuals）
    /** @brief 右手に持たせる近接武器1種ぶんのモデルと、握らせる際のローカルスケール/回転/位置 */
    struct HeldWeaponSlot {
        WeaponType type;
        Model* model = nullptr; // 実体はModelManagerが所有・共有する
        std::unique_ptr<Object3d> object;
        Vector3 gripScale;
        Vector3 gripRotate;
        Vector3 gripTranslate;
    };
    std::vector<HeldWeaponSlot> heldWeapons_;
    int activeHeldIndex_ = -1; ///< 現在表示中の heldWeapons_ インデックス（-1=非表示）
    int thrownWeaponIndex_ = -1; ///< 手元を離れて飛んでいる武器（武器切り替え後もactiveHeldIndex_と別に描画し続けるための heldWeapons_ インデックス、無ければ-1）

    /** @brief 手元を離れて飛んでいる武器を、手のボーンから外して位置・回転を直接動かす */
    void PlaceThrownWeapon(HeldWeaponSlot& slot, const Vector3& position, float spinZ);

    /** @brief 右手ボーンに武器オブジェクトを追従させる（握りローカル行列は呼び出し側で指定） */
    void AttachHeldWeapon(Object3d* obj, const char* boneName,
        const Vector3& gripScale, const Vector3& gripRotate, const Vector3& gripTranslate);

    // 銃装備（左手ボーンに毎フレーム追従、G キーで切り替えた1丁だけ表示）
    // 近接の heldWeapons_ と同じテーブル方式（追加は CharacterVisuals.h の kGunVisuals）
    struct GunSlot {
        GunType type;
        Model* model = nullptr; // 実体はModelManagerが所有・共有する
        std::unique_ptr<Object3d> object;
        Vector3 gripScale;
        Vector3 gripRotate;
        Vector3 gripTranslate;
    };
    std::vector<GunSlot> guns_;
    int activeGunIndex_ = -1; ///< 現在表示中の guns_ インデックス（-1=非表示）
    float portalAlpha_ = 1.0f; ///< 入口演出時の輪郭の透明度
    bool gunVisible_ = true; ///< 銃のアタッチ先ボーンが見つかったか
    bool weaponsDropped_ = false; ///< やられ演出で武器を手放したか（手に付けず、銃も隠す）
    Vector3 droppedWeaponPosition_ {}; ///< 手放した近接武器の位置（やられ演出が毎フレーム決める）
    float droppedWeaponSpin_ = 0.0f; ///< 手放した近接武器のZ軸回りの回転

    // Anim State パターン
    // 移動系のアニメーション状態ごとに、遷移した瞬間に再生するモーションを切り替える
    /** @brief アニメーション状態ごとのモーション選択を抽象化する状態 */
    class IAnimState {
    public:
        /** @brief 派生クラスを基底クラスから正しく破棄できるようにする仮想デストラクタ */
        virtual ~IAnimState() = default;
        /** @brief この状態へ遷移した瞬間に対応するモーションを再生する */
        virtual void Enter(Player& player, bool hold, bool isMoving) const = 0;
    };
    /** @brief 立ち姿勢のモーションを再生する状態 */
    class IdleAnimState;
    /** @brief 走りモーションを再生する状態 */
    class RunAnimState;
    /** @brief ジャンプモーションを再生する状態 */
    class JumpAnimState;
    /** @brief 泳ぎモーションを再生する状態 */
    class SwimAnimState;
    /** @brief 攻撃モーション再生中の状態（モーションはPlayAttackAnim側で設定済み） */
    class AttackAnimState;
    // 各状態の共有インスタンス（状態の定義はPlayerVisuals.cppに閉じる）
    static const IAnimState& IdleAnim();
    static const IAnimState& RunAnim();
    static const IAnimState& JumpAnim();
    static const IAnimState& SwimAnim();
    static const IAnimState& AttackAnim();
    const IAnimState* animState_ = &IdleAnim(); ///< 再生中のアニメーション状態
    bool animHold_ = false; ///< 武器持ちバリエーション（IdleHold/RunHold）を再生中か
    float attackAnimTimer_ = 0.0f; ///< 攻撃モーションの残り再生秒数（0以下で通常状態へ復帰）

    // フィニッシャー 静止集中 → 一閃
    bool finisherCharging_ = false; ///< 静止して溜めている最中か
    float finisherChargeTimer_ = 0.0f; ///< 残り溜め時間（0以下で解放＝一閃へ）

    /** @brief 左右の移動入力ぶん step だけ横へ動かし、向きを更新する */
    void ApplyHorizontalInput(Input* input, float step);
    /** @brief 押している左右の向き（-1/+1、押していなければ0。両方なら左を優先） */
    float GetHeldMoveDirection(Input* input) const;

    /**
     * @brief 移動・接地状況から再生すべきアニメーション状態を切り替える
     * @param isMoving 左右に移動しているか
     * @param airborne 宙にいるか（ふだんは接地状態の逆、演出中は演出側が決める）
     */
    void UpdateAnimationState(bool isMoving, bool airborne);

    /** @brief 攻撃モーションを頭から再生し、再生し切るまで状態遷移をロックする */
    void PlayAttackAnim(const Animation& anim, float speed);

    // Update() 分割ヘルパー（呼び出し順に定義、詳細は Player.cpp 参照）
    /** @brief 毎フレーム冒頭でJust～系の単発フラグ（ジャンプ・着地・被弾等）を全てリセットする */
    void ResetFrameFlags();
    /// @brief 回避入力の受付と回避移動の進行を処理する
    /// @param input 入力マネージャー
    /// @note 近接コンボ中でも受け付けてコンボを打ち切る（武器切替キャンセルと並ぶ逃げ道）。
    /// フィニッシャー溜め中・乱舞中・水中は受け付けない
    void HandleDodge(Input* input);
    /// @brief テレポート斬り入力の受付とワープ移動の進行を処理する
    /// @param input 入力マネージャー
    /// @note 覚醒ゲージがkWarpGaugeCost_未満の時は通常時は発動しない（覚醒中はゲージ消費なしで発動可）。
    /// 近接コンボ中でも受け付けてコンボを打ち切る回避と同じくフィニッシャー溜め中・乱舞中・水中は受け付けない
    void HandleWarp(Input* input);
    /** @brief 回避・ワープの水平移動を1フレーム進め、移動時間に達したかを返す */
    bool AdvanceHorizontalMove(float& timer, float duration, float startX, float targetX);
    /// @brief 数字キー/十字キーによる武器スロット切り替えを処理する
    /// @param input 入力マネージャー
    void HandleStyleSwitch(Input* input);
    /// @brief 銃切り替え（Gキー）と射撃コンボ（Kキー）の入力、および射撃反動の移動を処理する
    /// @param input 入力マネージャー
    void HandleRangedCombat(Input* input);
    /// @brief 近接攻撃コンボ・打ち上げ・乱舞（Lキー）の入力受付とコンボ進行を処理する
    /// @param input 入力マネージャー
    /// @param enemyPos 乱舞攻撃のターゲット座標
    void HandleMeleeCombat(Input* input, const Vector3& enemyPos);
    /// @brief フィニッシャースラッシュ（Fキー）の発動条件判定と、静止溜め→解放までの進行を処理する
    /// @param input 入力マネージャー
    void HandleFinisherSlash(Input* input);
    /// @brief スペースキーによる武器固有技（ダッシュ斬り・間合い外し・大地砕き等）の入力とクールダウンを処理する
    /// @param input 入力マネージャー
    void HandleWeaponSkill(Input* input);
    /// @brief 覚醒乱舞の現フェーズに応じた突進/追撃物理を進め、ヒットフレームで斬撃モーションを再生する
    /// @param enemyPos 乱舞スラッシュのターゲット座標
    void UpdateRampagePhysics(const Vector3& enemyPos);
    /// @brief 覚醒の発動判定（Rキー）とゲージ消費、時間切れ/ゲージ切れによる覚醒終了を処理する
    /// @param input 入力マネージャー
    void UpdateAwakenState(Input* input);
    /// @brief 乱舞中でない時、敵と一定距離以上重ならないようX座標を押し出す
    /// @param enemyPos 押し出し判定に使う敵の座標
    void ResolveEnemyOverlap(const Vector3& enemyPos);
    /** @brief 水面Yとの比較で入水/出水を判定し、入水時は落下速度を減衰させる */
    void UpdateWaterState();
    /// @brief 覚醒残像・アニメーション状態・プレイヤー色などUpdate()末尾の表示関連処理をまとめて更新する
    /// @param input 入力マネージャー（移動入力の参照に使用）
    void UpdateVisualState(Input* input);
    /** @brief 現在のスタイルに対応する近接武器と銃を、それぞれの手ボーンへ追従させる */
    void AttachActiveWeapons();
};

} // namespace engine::game
