/**
 * @file EnemyEntity.h
 * @brief ローグライトの戦闘で使用する汎用敵エンティティを定義するファイル
 */
#pragma once
#include "Model.h"
#include "ModelCommon.h"
#include "Object3d.h"
#include "Weapon.h"
#include <algorithm>
#include <memory>
#include <string>
namespace engine::game {
using engine::graphics::Model;
using engine::graphics::ModelCommon;
using engine::graphics::Object3d;

/**
 * @brief ローグライト戦闘シーンで使用する汎用の敵エンティティクラス
 * @note HP 管理・打ち上げ物理・撃破判定を提供する
 * SetMaxHp() で種別（Normal/Elite/Boss）ごとの HP を設定してから使用すること
 */
class EnemyEntity {
public:
    /**
     * @brief 初期化モデル生成とワールド座標を設定する
     * @param modelCommon モデル共通設定
     * @param startPos    初期ワールド座標
     */
    void Initialize(ModelCommon* modelCommon, const Vector3& startPos,
        WeaponType weaponType = WeaponType::Sword);

    /** @brief 物理・アニメーションを毎フレーム更新する */
    void Update();

    /** @brief モデルを描画する */
    void Draw();

    /** @brief 所持武器を識別するための表示色を設定する */
    void SetColor(const Vector4& color)
    {
        object_->SetColor(color);
        if (weaponObject_) weaponObject_->SetColor(color);
    }

    /**
     * @brief 上方向の初速を与えて打ち上げる
     * @param velY 上方向速度（正の値）
     */
    void Launch(float velY);
    void ApplyComboReaction(float knockDirX, float knockY, bool switchPull,
        float playerX);
    void ApplySlow(float seconds) { slowTimer_ = (std::max)(slowTimer_, seconds); }

    /** @brief 攻撃の進行フェーズ */
    enum class AttackState {
        Idle, ///< 次の攻撃までのクールダウン中
        Telegraph, ///< 予備動作中（攻撃判定はまだ発生しない）
        Active, ///< 攻撃判定が発生している短い窓
    };

    /** @brief 予備動作が明けて弾を撃ち出す瞬間のフレームだけ true（弾の発射トリガー用） */
    bool JustFiredAttack() const { return justFiredAttack_; }

    /** @brief 攻撃がヒットした際に与えるダメージ量を返す */
    int GetAttackDamage() const
    {
        return (weaponType_ == WeaponType::Hammer || weaponType_ == WeaponType::Axe) ? 3 : 2;
    }

    /**
     * @brief ダメージを与えるHP が 0 以下になると撃破状態になる
     * @param dmg 与えるダメージ量（デフォルト 1）
     */
    void TakeDamage(int dmg = 1)
    {
        if (defeated_) {
            return;
        }
        hp_ -= dmg;
        if (hp_ <= 0) {
            hp_ = 0;
            defeated_ = true;
        }
    }

    /**
     * @brief 最大 HP を設定し、現在 HP をリセットする
     * @param v 設定する最大 HP 値
     */
    void SetMaxHp(int v)
    {
        maxHp_ = v;
        hp_ = v;
        defeated_ = false;
    }

    /**
     * @brief HP を回復する（maxHp を超えない、撃破済みは復活しない）
     * @param amount 回復量
     */
    void Heal(int amount)
    {
        if (defeated_) {
            return;
        }
        hp_ = (std::min)(hp_ + amount, maxHp_);
    }

    /**
     * @brief 位置だけ再計算する（AI/物理は一切進めない、StageEditor等が外部からpos_を書き換えた後の追従用）
     */
    void RefreshVisualTransforms()
    {
        object_->SetPosition(pos_);
        object_->Update();
    }

    /**
     * @brief EnemyRegistry へ登録する際のid。ノードグラフの対象敵指定に使う
     * @param id シーン内で一意な識別名（例: "enemy", "boss"）
     */
    void SetId(const std::string& id) { id_ = id; }
    /** @brief 登録id未設定なら空文字 */
    const std::string& GetId() const { return id_; }

    /** @brief 撃破済みかどうかを返す */
    bool IsDefeated() const { return defeated_; }
    /** @brief 現在の HP を返す */
    int GetHp() const { return hp_; }
    /** @brief 最大 HP を返す */
    int GetMaxHp() const { return maxHp_; }

    /**
     * @brief 本体モデルの表示/非表示を切り替える（切断演出中に非表示にする用）
     * @param visible 表示するなら true
     */
    void SetVisible(bool visible) { visible_ = visible; }
    /** @brief 本体モデルが表示中かどうか */
    bool IsVisible() const { return visible_; }

    /** @brief 切断演出などが参照するモデルを返す */
    Model* GetModel() const { return model_.get(); }

    /** @brief このフレームに着地したか */
    bool JustLanded() const { return justLanded_; }
    /** @brief 打ち上げ中かどうか */
    bool IsLaunched() const { return isLaunched_; }
    /** @brief 現在のワールド座標を返す */
    const Vector3& GetPosition() const { return pos_; }
    /** @brief StageEditorのギズモドラッグ等、外部から直接書き換えるための可変参照 */
    Vector3& GetPositionRef() { return pos_; }

private:
    static constexpr float kGroundY_ = 0.4f;
    static constexpr float kCeilingY_ = 12.5f;
    static constexpr float kGravity_ = 0.015f;

    // 攻撃ステートマシン（Idle→Telegraph→Active→Idle を固定時間で巡回する）
    // Telegraph→Active の切り替わり瞬間が弾の発射トリガー実際の弾はGamePlayScene側が撃ち出して追跡する
    static constexpr float kAttackInterval_ = 2.5f; // 攻撃と攻撃の間隔（秒）
    static constexpr float kAttackTelegraph_ = 0.5f; // 予備動作の長さ（秒）
    static constexpr float kAttackActive_ = 0.18f; // 発射直後の連射防止用の短い不応期（秒）
    static constexpr int kAttackDamage_ = 2;

    /** @brief 攻撃ステートマシンを毎フレーム進める（Update() から呼ぶ） */
    void UpdateAttack();

    std::unique_ptr<Model> model_;
    std::unique_ptr<Object3d> object_;
    std::unique_ptr<Model> weaponModel_;
    std::unique_ptr<Object3d> weaponObject_;
    Vector3 weaponScale_ { 0.14f, 0.14f, 0.14f };

    int maxHp_ = 20;
    int hp_ = 20;
    bool defeated_ = false;
    bool visible_ = true;
    std::string id_; // EnemyRegistry登録名（未登録なら空）

    Vector3 pos_ = { };
    float velY_ = 0.0f;
    bool isLaunched_ = false;
    bool justLanded_ = false;
    WeaponType weaponType_ = WeaponType::Sword;
    float knockVelX_ = 0.0f;
    float slowTimer_ = 0.0f;
    float airComboTimer_ = 0.0f;
    static constexpr float kAirComboHold_ = 0.32f;

    AttackState attackState_ = AttackState::Idle;
    float attackTimer_ = kAttackInterval_;
    bool justFiredAttack_ = false;
};

} // namespace engine::game
