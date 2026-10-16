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
    float weaponFatigueRestDelay = 1.5f; ///< 戦闘から離れてこの秒数たてば、最後に使った武器も休ませている扱いにする
    float fatigueDullStrengthMin = 0.45f; ///< 疲労が上限の時のヒット演出の大きさ倍率

    // ヒット判定
    float lockOnCameraShiftRatio = 0.15f; ///< ロック中にカメラを対象側へ寄せる割合
    float airMeleeRangeHalfHeight = 1.35f; ///< 空中の近接攻撃の縦判定（ボス）
    float airMeleeRangeWidthPadding = 0.4f; ///< 空中の近接攻撃の横の広げ幅（ボス）
    float weaponEnemyAirRangeHalfHeight = 1.25f; ///< 空中の近接攻撃の縦判定（道中の敵）
    float weaponEnemyAirRangeWidthPadding = 0.35f;
    float breakableHitPadding = 0.1f; ///< 壊せる物の見た目より広く取る当たり
    float enemyBulletHalfExtent = 0.15f; ///< 敵弾の当たり判定の半径

    // ダメージ
    float unarmedBaseDamage = 20.0f; ///< 素手の時の基礎ダメージ
    int bossSkillBaseDamage = 4; ///< ボスへの固有技の固定ダメージ
    int bossSlamBaseDamage = 5; ///< ボスへの叩きつけの固定ダメージ
    int weaponEnemySlamBaseDamage = 3; ///< 道中の敵への叩きつけの固定ダメージ
    float weaponEnemySkillDamageMult = 2.5f; ///< 道中の敵への固有技の倍率（通常段より大きく削る）
    int gunBaseDamage = 1;
    int rampageDamage = 1; ///< 乱舞スラッシュ1回のダメージ
    int contactDamage = 1; ///< 体当たりで敵に与えるダメージ
    float contactHitCooldown = 0.5f; ///< 体当たりの連続ヒットを防ぐ間隔

    // 武器ごとのヒット時追加効果
    int swordBonusStartStep = 3; ///< 剣: 追加ダメージが乗り始める段
    int swordBonusDamage = 1;
    float daggerSlowSeconds = 0.9f; ///< 短剣: 当てた敵の行動を遅くする秒数
    int spearChainDamage = 1; ///< 槍: 周囲の武器持ちの敵へ連鎖するダメージ

    // 拘束と吹き飛び
    float knockbackScale = 0.72f; ///< 武器のノックバック倍率に掛ける全体の倍率
    float defaultKnockY = 0.05f; ///< 段定義がない時の打ち上げ量
    float skillKnockY = 0.18f; ///< 固有技は浮かせて吹き飛ばし、通常段との格の違いを見せる
    float bossHitstunScale = 0.6f; ///< ボスは長く拘束しすぎると一方的になるため硬直を短くする
    float hitstunPadding = 0.2f; ///< 段の長さに足す硬直の余裕（次の段の発生までは確実に拘束する）
    float defaultHitstunSeconds = 0.4f;
    float skillHitstunSeconds = 0.5f;
    float gunHitstunSeconds = 0.12f; ///< 銃は怯ませるだけで攻撃は潰さない

    // ヒットストップとカメラ揺れ（近接の段）
    int defaultHitStopFrames = 3;
    int finisherMinHitStopFrames = 10;
    float midStepShakePerFrame = 0.014f; ///< 途中の段の揺れ（ヒットストップ1フレームあたり）
    float finisherShakePerFrame = 0.032f; ///< 締めの段の揺れ（ヒットストップ1フレームあたり）
    float hitShakeDurationBase = 0.06f;
    float hitShakeDurationPerFrame = 0.01f;

    // ヒット演出の大きさ
    float comboStepHitStrengthGain = 0.2f; ///< 段が1つ進むごとに弾けを大きくする量
    float weaponSwitchHitStrength = 1.8f;
    float skillHitStrength = 1.6f; ///< ボスへの固有技
    float slamHitStrength = 2.2f;
    float weaponEnemySkillHitStrength = 1.8f; ///< 道中の敵への固有技
    float gunHitStrength = 0.7f;
    float rampageHitStrength = 0.8f;
    float skillElementScale = 1.6f;
    float gunElementScale = 0.6f; ///< 連射で画面が埋まらないよう近接より小さく
    int comboMilestoneInterval = 5; ///< このヒット数ごとに締めと同じ山場の演出を出す

    // 回避
    int dodgeSpamPenaltyStartCount = 2; ///< この回数目の連打回避からスタイルを削る

    // 武器の奪取とモンスターの消滅
    float monsterVanishDelay = 0.6f; ///< 倒れた姿を見せてから弾けて消えるまで
    float monsterVanishGauge = 0.06f; ///< 消えたモンスターから溜まる覚醒ゲージ
    float absorbRange = 2.0f; ///< 武器を奪える距離
    float absorbDuration = 0.5f;
    float absorbPullRate = 0.16f; ///< 1フレームで武器をプレイヤーへ寄せる割合
    float absorbTargetHeight = 0.5f; ///< 武器を吸い寄せる先の高さ（足元からの高さ）

    // 当て続けるほど命中演出を盛る
    float hitEscalationKeepSeconds = 2.0f; ///< この秒数ヒットが無いと盛り度合いが初撃に戻る
    int hitEscalationMaxHits = 20; ///< この連続ヒット数で盛り度合いが上限になる
    float hitEscalationStrengthGain = 0.6f; ///< 上限時に弾けの大きさへ足す割合
    int hitEscalationExtraSparks = 18; ///< 上限時に足す火花の数
    float hitEscalationExtraBurstGain = 1.0f; ///< 上限時に武器固有の追加バーストへ足す割合
    float hitEscalationOuterRingThreshold = 0.4f; ///< この盛り度合い以上で外側のリングを重ねる
    float hitEscalationMaxThreshold = 1.0f; ///< この盛り度合いで星をもう1つ重ねる
    float elementEscalationScaleGain = 0.8f; ///< 連続ヒットが上限の時に属性演出へ足す拡大率
    float elementScalePerComboStep = 0.12f; ///< コンボ段が1上がるごとの属性演出の拡大率
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
