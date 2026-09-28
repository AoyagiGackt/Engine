/**
 * @file EnemyTuning.h
 * @brief 敵AI・敵弾・ボス範囲攻撃の調整値をResources/Config/enemy_params.jsonから読む設定
 * @note EnemyEntity/KnightEnemy/GamePlaySceneが参照する。数値の変更にコードのリビルドが要らないようにする
 */
#pragma once
namespace engine::game {

/** @brief 汎用敵(EnemyEntity)の調整値 */
struct BasicEnemyTuning {
    float attackInterval = 2.5f; ///< 攻撃と攻撃の間隔（秒）
    float attackTelegraph = 0.5f; ///< 予備動作の長さ（秒）
    float attackActive = 0.18f; ///< 発射直後の不応期（秒）
    int attackDamage = 2;
    int heavyAttackDamage = 3; ///< Hammer/Axe
    float daggerTelegraph = 0.20f;
    float spearTelegraph = 0.38f;
    float heavyTelegraph = 0.75f;
    float daggerRecovery = 1.25f;
    float spearRecovery = 2.0f;
    float heavyRecovery = 3.2f;
    float approachSpeed = 0.06f; ///< 1フレームあたりの歩行距離
    float engageRange = 2.0f; ///< これより近づいたら歩みを止める
    float aggroRange = 10.0f; ///< 持ち場からこの距離にプレイヤーが来たら接近・攻撃を始める
    float leashDistance = 8.0f; ///< 持ち場から離れられる上限
    float meleeAttackRange = 2.8f; ///< 近接敵が振りかぶりを始めるプレイヤーとの距離
    float meleeReach = 2.2f; ///< 近接攻撃が届く前方距離（発生の瞬間にこの範囲内なら命中）
    float meleeHitHalfHeight = 1.2f; ///< 近接攻撃判定の縦方向半径
    float gravity = 0.015f;
    float ceilingY = 12.5f;
    float knockbackDecay = 0.82f;
    float knockbackSlowMultiplier = 0.45f;
    float airComboGravityScale = 0.28f;
    float airComboHold = 0.32f;
    float flyingBobAmplitude = 0.65f; ///< flying種別の上下浮遊の振幅
    float flyingBobSpeed = 2.0f; ///< flying種別の上下浮遊の速さ
    float healerPulseSeconds = 1.5f;
    float healerCastSeconds = 0.5f;
    float healerRange = 7.0f;
    int healerAmount = 4;
};

/** @brief ナイト型敵(KnightEnemy)の調整値 */
struct KnightEnemyTuning {
    int maxHp = 3;
    float idleDuration = 1.2f;
    float telegraphDuration = 0.35f;
    float dashDuration = 0.22f;
    float recoverDuration = 0.6f;
    float maxDashDistance = 5.0f;
    float knockbackSpeed = 0.18f;
    float knockbackDecay = 0.85f;
};

/** @brief 敵の遠隔攻撃弾の調整値 */
struct EnemyBulletTuning {
    float speed = 9.0f;
    float lifetime = 1.2f;
    float fireRange = 7.0f; ///< 発射の瞬間にプレイヤーがこの距離より遠い敵は撃たない
};

/** @brief ボスの予告円→着弾の範囲攻撃の調整値 */
struct BossSlamTuning {
    float engageRange = 10.0f;
    float interval = 7.0f;
    float warningDuration = 1.1f;
    float radius = 2.6f;
    int damage = 3;
};

/**
 * @brief enemy_params.jsonを読み込んで保持するシングルトン
 */
class EnemyTuning {
public:
    /**
     * @brief 唯一のEnemyTuningインスタンスを取得する（初回はJSONを読み込む）
     * @return EnemyTuningのインスタンス
     */
    static EnemyTuning* GetInstance();

    /** @brief JSONを読み直す */
    void Reload();

    const BasicEnemyTuning& Basic() const { return basic_; }
    const KnightEnemyTuning& Knight() const { return knight_; }
    const EnemyBulletTuning& Bullet() const { return bullet_; }
    const BossSlamTuning& BossSlam() const { return bossSlam_; }

private:
    EnemyTuning();
    EnemyTuning(const EnemyTuning&) = delete;
    EnemyTuning& operator=(const EnemyTuning&) = delete;

    BasicEnemyTuning basic_;
    KnightEnemyTuning knight_;
    EnemyBulletTuning bullet_;
    BossSlamTuning bossSlam_;
};

} // namespace engine::game
