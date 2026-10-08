/**
 * @file CombatTuning.cpp
 * @brief CombatTuningの戦闘調整値の読み込みを実装するファイル
 */
#include "CombatTuning.h"
#include "JsonHelper.h"
#include "Logger.h"
using namespace engine::game;
using namespace engine;

namespace {
constexpr const char* kCombatTuningPath = "Resources/Config/combat_tuning.json";
}

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
    data_.meleeDamageDivisor = root.value("meleeDamageDivisor", defaults.meleeDamageDivisor);
    data_.styleDecayRate = root.value("styleDecayRate", defaults.styleDecayRate);
    data_.stylePersistDecayMult = root.value("stylePersistDecayMult", defaults.stylePersistDecayMult);
    data_.enemyHitBoxHalfExtent = root.value("enemyHitBoxHalfExtent", defaults.enemyHitBoxHalfExtent);
    data_.meleeRepeatPenaltyPerHit = root.value("meleeRepeatPenaltyPerHit", defaults.meleeRepeatPenaltyPerHit);
    data_.meleeRepeatPenaltyCap = root.value("meleeRepeatPenaltyCap", defaults.meleeRepeatPenaltyCap);
    data_.meleeWeaponSwitchBonus = root.value("meleeWeaponSwitchBonus", defaults.meleeWeaponSwitchBonus);
    data_.meleeBaseStyleGain = root.value("meleeBaseStyleGain", defaults.meleeBaseStyleGain);
    data_.meleeComboStepStyleGain = root.value("meleeComboStepStyleGain", defaults.meleeComboStepStyleGain);
    data_.meleeAwakenGaugeGain = root.value("meleeAwakenGaugeGain", defaults.meleeAwakenGaugeGain);
    data_.chainSkillRange = root.value("chainSkillRange", defaults.chainSkillRange);
    data_.skillSlamRadius = root.value("skillSlamRadius", defaults.skillSlamRadius);
    data_.skillDefaultRadius = root.value("skillDefaultRadius", defaults.skillDefaultRadius);
    data_.skillRangeHalfHeight = root.value("skillRangeHalfHeight", defaults.skillRangeHalfHeight);
    data_.weaponEnemySkillRadius = root.value("weaponEnemySkillRadius", defaults.weaponEnemySkillRadius);
    data_.skillVarietyBonusRepeat = root.value("skillVarietyBonusRepeat", defaults.skillVarietyBonusRepeat);
    data_.skillVarietyBonusFresh = root.value("skillVarietyBonusFresh", defaults.skillVarietyBonusFresh);
    data_.skillAwakenGaugeGain = root.value("skillAwakenGaugeGain", defaults.skillAwakenGaugeGain);
    data_.gunBackRange = root.value("gunBackRange", defaults.gunBackRange);
    data_.gunBaseStyleGain = root.value("gunBaseStyleGain", defaults.gunBaseStyleGain);
    data_.gunComboStepStyleGain = root.value("gunComboStepStyleGain", defaults.gunComboStepStyleGain);
    data_.gunAwakenGaugeGain = root.value("gunAwakenGaugeGain", defaults.gunAwakenGaugeGain);
    data_.stingerStyleGain = root.value("stingerStyleGain", defaults.stingerStyleGain);
    data_.rampageRushRadiusX = root.value("rampageRushRadiusX", defaults.rampageRushRadiusX);
    data_.rampageRushRadiusY = root.value("rampageRushRadiusY", defaults.rampageRushRadiusY);
    data_.rampageBaseStyleGain = root.value("rampageBaseStyleGain", defaults.rampageBaseStyleGain);
    data_.rampageJuggleStyleGain = root.value("rampageJuggleStyleGain", defaults.rampageJuggleStyleGain);
    data_.justDodgeStyleGain = root.value("justDodgeStyleGain", defaults.justDodgeStyleGain);
    data_.justDodgeGaugeGain = root.value("justDodgeGaugeGain", defaults.justDodgeGaugeGain);
    data_.justDodgeHitStopFrames = root.value("justDodgeHitStopFrames", defaults.justDodgeHitStopFrames);
    data_.dodgeSpamPenalty = root.value("dodgeSpamPenalty", defaults.dodgeSpamPenalty);
    data_.justDodgeBonusSeconds = root.value("justDodgeBonusSeconds", defaults.justDodgeBonusSeconds);
    data_.justDodgeDamageMult = root.value("justDodgeDamageMult", defaults.justDodgeDamageMult);
    data_.airborneHitStyleBonus = root.value("airborneHitStyleBonus", defaults.airborneHitStyleBonus);
    data_.weaponFatiguePerHit = root.value("weaponFatiguePerHit", defaults.weaponFatiguePerHit);
    data_.gunFatiguePerHit = root.value("gunFatiguePerHit", defaults.gunFatiguePerHit);
    data_.weaponFatigueRecoverPerSecond = root.value("weaponFatigueRecoverPerSecond", defaults.weaponFatigueRecoverPerSecond);
    data_.weaponFatigueFreeRatio = root.value("weaponFatigueFreeRatio", defaults.weaponFatigueFreeRatio);
    data_.weaponFatigueMinDamageMult = root.value("weaponFatigueMinDamageMult", defaults.weaponFatigueMinDamageMult);
    data_.duplicateWeaponAwakenBonus = root.value("duplicateWeaponAwakenBonus", defaults.duplicateWeaponAwakenBonus);
    data_.styleRankAwakenGaugeBonus = root.value("styleRankAwakenGaugeBonus", defaults.styleRankAwakenGaugeBonus);
}
