/**
 * @file CombatTuning.h
 * @brief 本編のスタイルゲージ・覚醒ゲージ・ヒット判定範囲の調整値をResources/Config/combat_tuning.jsonから読む設定
 */
#pragma once
namespace engine::game {

/** @brief combat_tuning.jsonの内容 */
struct CombatTuningData {
    float meleeDamageDivisor = 25.0f; ///< 武器ダメージ×倍率を敵HPスケールへ落とし込む除数
    float styleDecayRate = 0.12f; ///< スタイルゲージの毎秒減衰量
    float stylePersistDecayMult = 0.6f; ///< StylePersistスキル所持時の減衰倍率
    float enemyHitBoxHalfExtent = 0.5f; ///< 敵の当たり判定AABBの半径
    float meleeRepeatPenaltyPerHit = 0.035f;
    float meleeRepeatPenaltyCap = 0.10f;
    float meleeWeaponSwitchBonus = 0.18f;
    float meleeBaseStyleGain = 0.10f;
    float meleeComboStepStyleGain = 0.04f;
    float meleeAwakenGaugeGain = 0.08f;
    float chainSkillRange = 5.0f;
    float skillSlamRadius = 3.5f;
    float skillDefaultRadius = 2.8f;
    float skillRangeHalfHeight = 2.0f;
    float weaponEnemySkillRadius = 3.0f;
    float skillVarietyBonusRepeat = 0.06f;
    float skillVarietyBonusFresh = 0.18f;
    float skillAwakenGaugeGain = 0.10f;
    float gunBackRange = 0.8f;
    float gunBaseStyleGain = 0.04f;
    float gunComboStepStyleGain = 0.01f;
    float gunAwakenGaugeGain = 0.04f;
    float stingerStyleGain = 0.10f;
    float rampageRushRadiusX = 2.5f;
    float rampageRushRadiusY = 1.5f;
    float rampageBaseStyleGain = 0.10f;
    float rampageJuggleStyleGain = 0.02f;
};

/**
 * @brief combat_tuning.jsonを読み込んで保持するシングルトン
 */
class CombatTuning {
public:
    /**
     * @brief 唯一のCombatTuningインスタンスを取得する（初回はJSONを読み込む）
     * @return CombatTuningのインスタンス
     */
    static CombatTuning* GetInstance();

    /** @brief JSONを読み直す */
    void Reload();

    /** @brief 読み込んだ調整値を返す */
    const CombatTuningData& Get() const { return data_; }

private:
    CombatTuning();
    CombatTuning(const CombatTuning&) = delete;
    CombatTuning& operator=(const CombatTuning&) = delete;

    CombatTuningData data_;
};

} // namespace engine::game
