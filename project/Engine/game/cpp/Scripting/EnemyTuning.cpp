/**
 * @file EnemyTuning.cpp
 * @brief EnemyTuningの敵調整値の読み込みを実装するファイル
 */
#include "EnemyTuning.h"
#include "JsonHelper.h"
#include "Logger.h"
using namespace engine::game;
using namespace engine;

namespace {
constexpr const char* kEnemyParamsPath = "Resources/Config/enemy_params.json";

template <typename T>
T ReadValue(const nlohmann::json& section, const char* key, T fallback)
{
    return section.is_object() ? section.value(key, fallback) : fallback;
}
}

EnemyTuning* EnemyTuning::GetInstance()
{
    static EnemyTuning instance;
    return &instance;
}

EnemyTuning::EnemyTuning()
{
    Reload();
}

void EnemyTuning::Reload()
{
    basic_ = BasicEnemyTuning { };
    knight_ = KnightEnemyTuning { };
    bullet_ = EnemyBulletTuning { };
    bossSlam_ = BossSlamTuning { };

    const nlohmann::json root = JsonHelper::Load(kEnemyParamsPath);
    if (!root.is_object()) {
        Logger::LogWarning(std::string("EnemyTuning: ") + kEnemyParamsPath + " が読めません。既定値で動きます");
        return;
    }

    const nlohmann::json basic = root.value("basic", nlohmann::json::object());
    basic_.attackInterval = ReadValue(basic, "attackInterval", basic_.attackInterval);
    basic_.attackTelegraph = ReadValue(basic, "attackTelegraph", basic_.attackTelegraph);
    basic_.attackActive = ReadValue(basic, "attackActive", basic_.attackActive);
    basic_.attackDamage = ReadValue(basic, "attackDamage", basic_.attackDamage);
    basic_.heavyAttackDamage = ReadValue(basic, "heavyAttackDamage", basic_.heavyAttackDamage);
    basic_.daggerTelegraph = ReadValue(basic, "daggerTelegraph", basic_.daggerTelegraph);
    basic_.spearTelegraph = ReadValue(basic, "spearTelegraph", basic_.spearTelegraph);
    basic_.heavyTelegraph = ReadValue(basic, "heavyTelegraph", basic_.heavyTelegraph);
    basic_.daggerRecovery = ReadValue(basic, "daggerRecovery", basic_.daggerRecovery);
    basic_.spearRecovery = ReadValue(basic, "spearRecovery", basic_.spearRecovery);
    basic_.heavyRecovery = ReadValue(basic, "heavyRecovery", basic_.heavyRecovery);
    basic_.approachSpeed = ReadValue(basic, "approachSpeed", basic_.approachSpeed);
    basic_.engageRange = ReadValue(basic, "engageRange", basic_.engageRange);
    basic_.aggroRange = ReadValue(basic, "aggroRange", basic_.aggroRange);
    basic_.leashDistance = ReadValue(basic, "leashDistance", basic_.leashDistance);
    basic_.gravity = ReadValue(basic, "gravity", basic_.gravity);
    basic_.ceilingY = ReadValue(basic, "ceilingY", basic_.ceilingY);
    basic_.knockbackDecay = ReadValue(basic, "knockbackDecay", basic_.knockbackDecay);
    basic_.knockbackSlowMultiplier = ReadValue(basic, "knockbackSlowMultiplier", basic_.knockbackSlowMultiplier);
    basic_.airComboGravityScale = ReadValue(basic, "airComboGravityScale", basic_.airComboGravityScale);
    basic_.airComboHold = ReadValue(basic, "airComboHold", basic_.airComboHold);
    basic_.flyingBobAmplitude = ReadValue(basic, "flyingBobAmplitude", basic_.flyingBobAmplitude);
    basic_.flyingBobSpeed = ReadValue(basic, "flyingBobSpeed", basic_.flyingBobSpeed);
    basic_.healerPulseSeconds = ReadValue(basic, "healerPulseSeconds", basic_.healerPulseSeconds);
    basic_.healerCastSeconds = ReadValue(basic, "healerCastSeconds", basic_.healerCastSeconds);
    basic_.healerRange = ReadValue(basic, "healerRange", basic_.healerRange);
    basic_.healerAmount = ReadValue(basic, "healerAmount", basic_.healerAmount);

    const nlohmann::json knight = root.value("knight", nlohmann::json::object());
    knight_.maxHp = ReadValue(knight, "maxHp", knight_.maxHp);
    knight_.idleDuration = ReadValue(knight, "idleDuration", knight_.idleDuration);
    knight_.telegraphDuration = ReadValue(knight, "telegraphDuration", knight_.telegraphDuration);
    knight_.dashDuration = ReadValue(knight, "dashDuration", knight_.dashDuration);
    knight_.recoverDuration = ReadValue(knight, "recoverDuration", knight_.recoverDuration);
    knight_.maxDashDistance = ReadValue(knight, "maxDashDistance", knight_.maxDashDistance);
    knight_.knockbackSpeed = ReadValue(knight, "knockbackSpeed", knight_.knockbackSpeed);
    knight_.knockbackDecay = ReadValue(knight, "knockbackDecay", knight_.knockbackDecay);

    const nlohmann::json bullet = root.value("bullet", nlohmann::json::object());
    bullet_.speed = ReadValue(bullet, "speed", bullet_.speed);
    bullet_.lifetime = ReadValue(bullet, "lifetime", bullet_.lifetime);
    bullet_.fireRange = ReadValue(bullet, "fireRange", bullet_.fireRange);

    const nlohmann::json slam = root.value("bossSlam", nlohmann::json::object());
    bossSlam_.engageRange = ReadValue(slam, "engageRange", bossSlam_.engageRange);
    bossSlam_.interval = ReadValue(slam, "interval", bossSlam_.interval);
    bossSlam_.warningDuration = ReadValue(slam, "warningDuration", bossSlam_.warningDuration);
    bossSlam_.radius = ReadValue(slam, "radius", bossSlam_.radius);
    bossSlam_.damage = ReadValue(slam, "damage", bossSlam_.damage);
}
