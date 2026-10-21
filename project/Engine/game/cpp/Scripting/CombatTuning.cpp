/**
 * @file CombatTuning.cpp
 * @brief CombatTuningの戦闘調整値の読み込みを実装するファイル
 */
#include "CombatTuning.h"
#include "JsonFieldTable.h"
#include "JsonHelper.h"
#include "Logger.h"
#include <cmath>
#include <algorithm>

namespace engine::game {
namespace {
constexpr const char* kCombatTuningPath = "Resources/Config/combat_tuning.json";

constexpr JsonField<FallingFloorTiming> kFloorFields[] = {
    { "fallingFloor", "warningEnd", &FallingFloorTiming::warningEnd },
    { "fallingFloor", "respawnStart", &FallingFloorTiming::respawnStart },
    { "fallingFloor", "cycleEnd", &FallingFloorTiming::cycleEnd },
    { "fallingFloor", "crackLeadTime", &FallingFloorTiming::crackLeadTime },
    { "fallingFloor", "minimumSpeed", &FallingFloorTiming::minimumSpeed },
};

constexpr JsonField<CombatTuningData> kFields[] = {
    { nullptr, "waterMinX", &CombatTuningData::waterMinX },
    { nullptr, "waterMaxX", &CombatTuningData::waterMaxX },
    { nullptr, "waterSurfaceY", &CombatTuningData::waterSurfaceY },
    { nullptr, "waterBottomY", &CombatTuningData::waterBottomY },
    { nullptr, "trainingWarpX", &CombatTuningData::trainingWarpX },
    { nullptr, "trainingWarpProximity", &CombatTuningData::trainingWarpProximity },
    { nullptr, "trainingGroundY", &CombatTuningData::trainingGroundY },
    { nullptr, "trainingFacingTargetDistance", &CombatTuningData::trainingFacingTargetDistance },
    { nullptr, "finisherLaunchRatio", &CombatTuningData::finisherLaunchRatio },
    { nullptr, "waypointArrivalDistance", &CombatTuningData::waypointArrivalDistance },
    { nullptr, "awakenExtraHitKnockScale", &CombatTuningData::awakenExtraHitKnockScale },
    { nullptr, "launchSpeed", &CombatTuningData::launchSpeed },
    { nullptr, "skillRearReachMult", &CombatTuningData::skillRearReachMult },
    { nullptr, "finisherLineDamage", &CombatTuningData::finisherLineDamage },
    { nullptr, "finisherSlashDamage", &CombatTuningData::finisherSlashDamage },
    { nullptr, "finisherSlashLines", &CombatTuningData::finisherSlashLines },
    { nullptr, "finisherChargeDelay", &CombatTuningData::finisherChargeDelay },
    { nullptr, "finisherLineInterval", &CombatTuningData::finisherLineInterval },
    { nullptr, "finisherImpactDelay", &CombatTuningData::finisherImpactDelay },
    { nullptr, "weaponAbsorbRange", &CombatTuningData::weaponAbsorbRange },
    { nullptr, "weaponAbsorbDuration", &CombatTuningData::weaponAbsorbDuration },
    { nullptr, "finisherBeatLaunchSpeed", &CombatTuningData::finisherBeatLaunchSpeed },
    { nullptr, "finisherBeatStyleGain", &CombatTuningData::finisherBeatStyleGain },
    { nullptr, "finisherReleaseStyleGain", &CombatTuningData::finisherReleaseStyleGain },
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

/** @brief 唯一のCombatTuningインスタンスを取得する（初回はJSONを読み込む） */
CombatTuning* CombatTuning::GetInstance()
{
    static CombatTuning instance;
    return &instance;
}

/** @brief combat_tuning.json を読み込んで初期化する */
CombatTuning::CombatTuning()
{
    Reload();
}

/** @brief JSONを読み直す */
void CombatTuning::Reload()
{
    // まず既定値に戻し、読めない時はそのまま既定値で動かす
    CombatTuningData defaults;
    data_ = defaults;
    fallingFloor_ = FallingFloorTiming {};
    const nlohmann::json root = JsonHelper::Load(kCombatTuningPath);
    if (!root.is_object())
    {
        Logger::LogWarning(std::string("CombatTuning: ") + kCombatTuningPath + " が読めません。既定値で動きます");
        return;
    }
    // 項目の対応表どおりに読み、壊れた組み合わせだけ既定値へ戻す
    ReadJsonFields(root, kFields, data_);
    // 水場は幅と深さが正の範囲だけを採用する。
    if (data_.waterMaxX <= data_.waterMinX || data_.waterSurfaceY <= data_.waterBottomY)
    {
        data_.waterMinX = defaults.waterMinX;
        data_.waterMaxX = defaults.waterMaxX;
        data_.waterSurfaceY = defaults.waterSurfaceY;
        data_.waterBottomY = defaults.waterBottomY;
    }
    // フィニッシャーは正の総時間を保ち、段数と待ち時間を有効な範囲に収める。
    data_.finisherSlashLines = (std::max)(data_.finisherSlashLines, 1);
    data_.finisherChargeDelay = (std::max)(data_.finisherChargeDelay, 0.0f);
    data_.finisherLineInterval = (std::max)(data_.finisherLineInterval, 0.0f);
    data_.finisherImpactDelay = (std::max)(data_.finisherImpactDelay, 0.0f);
    if (data_.FinisherChargeDuration() <= 0.0f)
    {
        data_.finisherChargeDelay = defaults.finisherChargeDelay;
    }
    // 崩れる床の周期
    ReadJsonFields(root, kFloorFields, fallingFloor_);
    // 時刻の順序と正の周期を保ち、不正な設定では既定値へ戻す
    const auto& floor = fallingFloor_;
    if (!std::isfinite(floor.warningEnd) || !std::isfinite(floor.respawnStart) || !std::isfinite(floor.cycleEnd)
        || !std::isfinite(floor.crackLeadTime) || !std::isfinite(floor.minimumSpeed)
        || floor.warningEnd < 0.0f || floor.respawnStart < floor.warningEnd || floor.cycleEnd <= floor.respawnStart
        || floor.crackLeadTime < 0.0f || floor.minimumSpeed <= 0.0f)
    {
        fallingFloor_ = FallingFloorTiming {};
    }
}

} // namespace engine::game
