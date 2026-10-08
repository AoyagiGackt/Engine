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
    float justDodgeStyleGain = 0.12f; ///< 敵の攻撃を回避の無敵で紙一重にかわした時のスタイル加点
    float justDodgeGaugeGain = 0.12f; ///< ジャスト回避で溜まる覚醒ゲージ量
    int justDodgeHitStopFrames = 5; ///< ジャスト回避の瞬間に世界を止めるフレーム数
    float dodgeSpamPenalty = 0.04f; ///< 連打回避1回ごとに減るスタイル量（2回目以降）
    float justDodgeBonusSeconds = 1.5f; ///< ジャスト回避直後に攻撃力が上がる強化窓の長さ
    float justDodgeDamageMult = 1.3f; ///< 強化窓中のダメージ倍率
    float airborneHitStyleBonus = 0.03f; ///< 空中で近接ヒットを当てた時の追加スタイル加点
    float duplicateWeaponAwakenBonus = 0.15f; ///< 所持済み武器タイプを再度奪った時に覚醒ゲージへ転用する量
    float styleRankAwakenGaugeBonus = 0.1f; ///< スタイルランク1段ごとに攻撃・ジャスト回避の覚醒ゲージ量へ足す倍率（D=1.0倍、SSS=1.6倍）
    // 武器の疲労（同じ武器で当て続けるとダメージが落ち、他の武器や銃で当てている間に回復する）
    float weaponFatiguePerHit = 0.1f; ///< 近接で当てるたびにその武器へたまる疲労（0〜1）
    float gunFatiguePerHit = 0.04f; ///< 銃で当てるたびに銃へたまる疲労（連射が速いぶん近接より小さい）
    float weaponFatigueRecoverPerSecond = 0.35f; ///< 最後に当てた武器以外の疲労が1秒あたりに抜ける量
    float weaponFatigueFreeRatio = 0.3f; ///< この疲労まではダメージが落ちない（コンボ1周ぶんは気持ちよく殴れる）
    float weaponFatigueMinDamageMult = 0.3f; ///< 疲労が上限の時のダメージ倍率
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
