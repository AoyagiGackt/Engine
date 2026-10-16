/**
 * @file CombatTuning.cpp
 * @brief CombatTuningの戦闘調整値の読み込みを実装するファイル
 */
#include "CombatTuning.h"
#include "JsonFieldTable.h"
#include "JsonHelper.h"
#include "Logger.h"
using namespace engine::game;
using namespace engine;

namespace {
constexpr const char* kCombatTuningPath = "Resources/Config/combat_tuning.json";

constexpr JsonField<CombatTuningData> kFields[] = {
    { nullptr, "meleeDamageDivisor", &CombatTuningData::meleeDamageDivisor },
    { nullptr, "styleDecayRate", &CombatTuningData::styleDecayRate },
    { nullptr, "stylePersistDecayMult", &CombatTuningData::stylePersistDecayMult },
    { nullptr, "enemyHitBoxHalfExtent", &CombatTuningData::enemyHitBoxHalfExtent },
    { nullptr, "meleeRepeatPenaltyPerHit", &CombatTuningData::meleeRepeatPenaltyPerHit },
    { nullptr, "meleeRepeatPenaltyCap", &CombatTuningData::meleeRepeatPenaltyCap },
    { nullptr, "meleeWeaponSwitchBonus", &CombatTuningData::meleeWeaponSwitchBonus },
    { nullptr, "meleeBaseStyleGain", &CombatTuningData::meleeBaseStyleGain },
    { nullptr, "meleeComboStepStyleGain", &CombatTuningData::meleeComboStepStyleGain },
    { nullptr, "meleeAwakenGaugeGain", &CombatTuningData::meleeAwakenGaugeGain },
    { nullptr, "chainSkillRange", &CombatTuningData::chainSkillRange },
    { nullptr, "skillSlamRadius", &CombatTuningData::skillSlamRadius },
    { nullptr, "skillDefaultRadius", &CombatTuningData::skillDefaultRadius },
    { nullptr, "skillRangeHalfHeight", &CombatTuningData::skillRangeHalfHeight },
    { nullptr, "weaponEnemySkillRadius", &CombatTuningData::weaponEnemySkillRadius },
    { nullptr, "skillVarietyBonusRepeat", &CombatTuningData::skillVarietyBonusRepeat },
    { nullptr, "skillVarietyBonusFresh", &CombatTuningData::skillVarietyBonusFresh },
    { nullptr, "skillAwakenGaugeGain", &CombatTuningData::skillAwakenGaugeGain },
    { nullptr, "gunBackRange", &CombatTuningData::gunBackRange },
    { nullptr, "gunBaseStyleGain", &CombatTuningData::gunBaseStyleGain },
    { nullptr, "gunComboStepStyleGain", &CombatTuningData::gunComboStepStyleGain },
    { nullptr, "gunAwakenGaugeGain", &CombatTuningData::gunAwakenGaugeGain },
    { nullptr, "rampageRushRadiusX", &CombatTuningData::rampageRushRadiusX },
    { nullptr, "rampageRushRadiusY", &CombatTuningData::rampageRushRadiusY },
    { nullptr, "rampageBaseStyleGain", &CombatTuningData::rampageBaseStyleGain },
    { nullptr, "rampageJuggleStyleGain", &CombatTuningData::rampageJuggleStyleGain },
    { nullptr, "justDodgeStyleGain", &CombatTuningData::justDodgeStyleGain },
    { nullptr, "justDodgeGaugeGain", &CombatTuningData::justDodgeGaugeGain },
    { nullptr, "dodgeSpamPenalty", &CombatTuningData::dodgeSpamPenalty },
    { nullptr, "justDodgeBonusSeconds", &CombatTuningData::justDodgeBonusSeconds },
    { nullptr, "justDodgeDamageMult", &CombatTuningData::justDodgeDamageMult },
    { nullptr, "airborneHitStyleBonus", &CombatTuningData::airborneHitStyleBonus },
    { nullptr, "duplicateWeaponAwakenBonus", &CombatTuningData::duplicateWeaponAwakenBonus },
    { nullptr, "styleRankAwakenGaugeBonus", &CombatTuningData::styleRankAwakenGaugeBonus },
    { nullptr, "weaponFatiguePerHit", &CombatTuningData::weaponFatiguePerHit },
    { nullptr, "gunFatiguePerHit", &CombatTuningData::gunFatiguePerHit },
    { nullptr, "weaponFatigueRecoverPerSecond", &CombatTuningData::weaponFatigueRecoverPerSecond },
    { nullptr, "weaponFatigueFreeRatio", &CombatTuningData::weaponFatigueFreeRatio },
    { nullptr, "weaponFatigueMinDamageMult", &CombatTuningData::weaponFatigueMinDamageMult },
    { nullptr, "weaponFatigueRestDelay", &CombatTuningData::weaponFatigueRestDelay },
    { nullptr, "fatigueDullStrengthMin", &CombatTuningData::fatigueDullStrengthMin },
    { nullptr, "lockOnCameraShiftRatio", &CombatTuningData::lockOnCameraShiftRatio },
    { nullptr, "airMeleeRangeHalfHeight", &CombatTuningData::airMeleeRangeHalfHeight },
    { nullptr, "airMeleeRangeWidthPadding", &CombatTuningData::airMeleeRangeWidthPadding },
    { nullptr, "weaponEnemyAirRangeHalfHeight", &CombatTuningData::weaponEnemyAirRangeHalfHeight },
    { nullptr, "weaponEnemyAirRangeWidthPadding", &CombatTuningData::weaponEnemyAirRangeWidthPadding },
    { nullptr, "breakableHitPadding", &CombatTuningData::breakableHitPadding },
    { nullptr, "enemyBulletHalfExtent", &CombatTuningData::enemyBulletHalfExtent },
    { nullptr, "unarmedBaseDamage", &CombatTuningData::unarmedBaseDamage },
    { nullptr, "weaponEnemySkillDamageMult", &CombatTuningData::weaponEnemySkillDamageMult },
    { nullptr, "contactHitCooldown", &CombatTuningData::contactHitCooldown },
    { nullptr, "daggerSlowSeconds", &CombatTuningData::daggerSlowSeconds },
    { nullptr, "knockbackScale", &CombatTuningData::knockbackScale },
    { nullptr, "defaultKnockY", &CombatTuningData::defaultKnockY },
    { nullptr, "skillKnockY", &CombatTuningData::skillKnockY },
    { nullptr, "bossHitstunScale", &CombatTuningData::bossHitstunScale },
    { nullptr, "hitstunPadding", &CombatTuningData::hitstunPadding },
    { nullptr, "defaultHitstunSeconds", &CombatTuningData::defaultHitstunSeconds },
    { nullptr, "skillHitstunSeconds", &CombatTuningData::skillHitstunSeconds },
    { nullptr, "gunHitstunSeconds", &CombatTuningData::gunHitstunSeconds },
    { nullptr, "midStepShakePerFrame", &CombatTuningData::midStepShakePerFrame },
    { nullptr, "finisherShakePerFrame", &CombatTuningData::finisherShakePerFrame },
    { nullptr, "hitShakeDurationBase", &CombatTuningData::hitShakeDurationBase },
    { nullptr, "hitShakeDurationPerFrame", &CombatTuningData::hitShakeDurationPerFrame },
    { nullptr, "comboStepHitStrengthGain", &CombatTuningData::comboStepHitStrengthGain },
    { nullptr, "weaponSwitchHitStrength", &CombatTuningData::weaponSwitchHitStrength },
    { nullptr, "skillHitStrength", &CombatTuningData::skillHitStrength },
    { nullptr, "slamHitStrength", &CombatTuningData::slamHitStrength },
    { nullptr, "weaponEnemySkillHitStrength", &CombatTuningData::weaponEnemySkillHitStrength },
    { nullptr, "gunHitStrength", &CombatTuningData::gunHitStrength },
    { nullptr, "rampageHitStrength", &CombatTuningData::rampageHitStrength },
    { nullptr, "skillElementScale", &CombatTuningData::skillElementScale },
    { nullptr, "gunElementScale", &CombatTuningData::gunElementScale },
    { nullptr, "monsterVanishDelay", &CombatTuningData::monsterVanishDelay },
    { nullptr, "monsterVanishGauge", &CombatTuningData::monsterVanishGauge },
    { nullptr, "absorbRange", &CombatTuningData::absorbRange },
    { nullptr, "absorbDuration", &CombatTuningData::absorbDuration },
    { nullptr, "absorbPullRate", &CombatTuningData::absorbPullRate },
    { nullptr, "absorbTargetHeight", &CombatTuningData::absorbTargetHeight },
    { nullptr, "hitEscalationKeepSeconds", &CombatTuningData::hitEscalationKeepSeconds },
    { nullptr, "hitEscalationStrengthGain", &CombatTuningData::hitEscalationStrengthGain },
    { nullptr, "hitEscalationExtraBurstGain", &CombatTuningData::hitEscalationExtraBurstGain },
    { nullptr, "hitEscalationOuterRingThreshold", &CombatTuningData::hitEscalationOuterRingThreshold },
    { nullptr, "hitEscalationMaxThreshold", &CombatTuningData::hitEscalationMaxThreshold },
    { nullptr, "elementEscalationScaleGain", &CombatTuningData::elementEscalationScaleGain },
    { nullptr, "elementScalePerComboStep", &CombatTuningData::elementScalePerComboStep },
    { nullptr, "justDodgeHitStopFrames", &CombatTuningData::justDodgeHitStopFrames },
    { nullptr, "bossSkillBaseDamage", &CombatTuningData::bossSkillBaseDamage },
    { nullptr, "bossSlamBaseDamage", &CombatTuningData::bossSlamBaseDamage },
    { nullptr, "weaponEnemySlamBaseDamage", &CombatTuningData::weaponEnemySlamBaseDamage },
    { nullptr, "gunBaseDamage", &CombatTuningData::gunBaseDamage },
    { nullptr, "rampageDamage", &CombatTuningData::rampageDamage },
    { nullptr, "contactDamage", &CombatTuningData::contactDamage },
    { nullptr, "swordBonusStartStep", &CombatTuningData::swordBonusStartStep },
    { nullptr, "swordBonusDamage", &CombatTuningData::swordBonusDamage },
    { nullptr, "spearChainDamage", &CombatTuningData::spearChainDamage },
    { nullptr, "defaultHitStopFrames", &CombatTuningData::defaultHitStopFrames },
    { nullptr, "finisherMinHitStopFrames", &CombatTuningData::finisherMinHitStopFrames },
    { nullptr, "comboMilestoneInterval", &CombatTuningData::comboMilestoneInterval },
    { nullptr, "dodgeSpamPenaltyStartCount", &CombatTuningData::dodgeSpamPenaltyStartCount },
    { nullptr, "hitEscalationMaxHits", &CombatTuningData::hitEscalationMaxHits },
    { nullptr, "hitEscalationExtraSparks", &CombatTuningData::hitEscalationExtraSparks },
};

} // namespace

CombatTuning* CombatTuning::GetInstance()
{
    static CombatTuning instance;
    return &instance;
}

CombatTuning::CombatTuning()
{
    Reload();
}

void CombatTuning::Reload()
{
    CombatTuningData defaults;
    data_ = defaults;
    const nlohmann::json root = JsonHelper::Load(kCombatTuningPath);
    if (!root.is_object()) {
        Logger::LogWarning(std::string("CombatTuning: ") + kCombatTuningPath + " が読めません。既定値で動きます");
        return;
    }
    ReadJsonFields(root, kFields, data_);
}
