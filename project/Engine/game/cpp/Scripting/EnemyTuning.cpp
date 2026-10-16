/**
 * @file EnemyTuning.cpp
 * @brief EnemyTuningの敵調整値の読み込みを実装するファイル
 */
#include "EnemyTuning.h"
#include "JsonFieldTable.h"
#include "JsonHelper.h"
#include "Logger.h"
using namespace engine::game;
using namespace engine;

namespace {
constexpr const char* kEnemyParamsPath = "Resources/Config/enemy_params.json";

constexpr JsonField<BasicEnemyTuning> kBasicFields[] = {
    { "basic", "attackInterval", &BasicEnemyTuning::attackInterval },
    { "basic", "attackTelegraph", &BasicEnemyTuning::attackTelegraph },
    { "basic", "attackActive", &BasicEnemyTuning::attackActive },
    { "basic", "attackDamage", &BasicEnemyTuning::attackDamage },
    { "basic", "heavyAttackDamage", &BasicEnemyTuning::heavyAttackDamage },
    { "basic", "daggerTelegraph", &BasicEnemyTuning::daggerTelegraph },
    { "basic", "spearTelegraph", &BasicEnemyTuning::spearTelegraph },
    { "basic", "heavyTelegraph", &BasicEnemyTuning::heavyTelegraph },
    { "basic", "daggerRecovery", &BasicEnemyTuning::daggerRecovery },
    { "basic", "spearRecovery", &BasicEnemyTuning::spearRecovery },
    { "basic", "heavyRecovery", &BasicEnemyTuning::heavyRecovery },
    { "basic", "approachSpeed", &BasicEnemyTuning::approachSpeed },
    { "basic", "engageRange", &BasicEnemyTuning::engageRange },
    { "basic", "aggroRange", &BasicEnemyTuning::aggroRange },
    { "basic", "leashDistance", &BasicEnemyTuning::leashDistance },
    { "basic", "meleeAttackRange", &BasicEnemyTuning::meleeAttackRange },
    { "basic", "meleeReach", &BasicEnemyTuning::meleeReach },
    { "basic", "meleeHitHalfHeight", &BasicEnemyTuning::meleeHitHalfHeight },
    { "basic", "gravity", &BasicEnemyTuning::gravity },
    { "basic", "ceilingY", &BasicEnemyTuning::ceilingY },
    { "basic", "knockbackDecay", &BasicEnemyTuning::knockbackDecay },
    { "basic", "knockbackSlowMultiplier", &BasicEnemyTuning::knockbackSlowMultiplier },
    { "basic", "airComboGravityScale", &BasicEnemyTuning::airComboGravityScale },
    { "basic", "airComboHold", &BasicEnemyTuning::airComboHold },
    { "basic", "flyingBobAmplitude", &BasicEnemyTuning::flyingBobAmplitude },
    { "basic", "flyingBobSpeed", &BasicEnemyTuning::flyingBobSpeed },
    { "basic", "healerPulseSeconds", &BasicEnemyTuning::healerPulseSeconds },
    { "basic", "healerCastSeconds", &BasicEnemyTuning::healerCastSeconds },
    { "basic", "healerRange", &BasicEnemyTuning::healerRange },
    { "basic", "healerAmount", &BasicEnemyTuning::healerAmount },
    { "basic", "corpseFootOffset", &BasicEnemyTuning::corpseFootOffset },
};

constexpr JsonField<KnightEnemyTuning> kKnightFields[] = {
    { "knight", "maxHp", &KnightEnemyTuning::maxHp },
    { "knight", "idleDuration", &KnightEnemyTuning::idleDuration },
    { "knight", "telegraphDuration", &KnightEnemyTuning::telegraphDuration },
    { "knight", "dashDuration", &KnightEnemyTuning::dashDuration },
    { "knight", "recoverDuration", &KnightEnemyTuning::recoverDuration },
    { "knight", "maxDashDistance", &KnightEnemyTuning::maxDashDistance },
    { "knight", "knockbackSpeed", &KnightEnemyTuning::knockbackSpeed },
    { "knight", "knockbackDecay", &KnightEnemyTuning::knockbackDecay },
};

constexpr JsonField<EnemyBulletTuning> kBulletFields[] = {
    { "bullet", "speed", &EnemyBulletTuning::speed },
    { "bullet", "lifetime", &EnemyBulletTuning::lifetime },
    { "bullet", "fireRange", &EnemyBulletTuning::fireRange },
};

constexpr JsonField<BossSlamTuning> kBossSlamFields[] = {
    { "bossSlam", "engageRange", &BossSlamTuning::engageRange },
    { "bossSlam", "interval", &BossSlamTuning::interval },
    { "bossSlam", "warningDuration", &BossSlamTuning::warningDuration },
    { "bossSlam", "radius", &BossSlamTuning::radius },
    { "bossSlam", "damage", &BossSlamTuning::damage },
};

constexpr JsonField<EnemySeparationTuning> kSeparationFields[] = {
    { "separation", "minDistanceX", &EnemySeparationTuning::minDistanceX },
    { "separation", "minDistanceY", &EnemySeparationTuning::minDistanceY },
    { "separation", "pushRate", &EnemySeparationTuning::pushRate },
    { "separation", "bodyHalfWidth", &EnemySeparationTuning::bodyHalfWidth },
    { "separation", "wallBandBottom", &EnemySeparationTuning::wallBandBottom },
    { "separation", "wallBandTop", &EnemySeparationTuning::wallBandTop },
    { "separation", "wallContactMargin", &EnemySeparationTuning::wallContactMargin },
};
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
    separation_ = EnemySeparationTuning { };

    const nlohmann::json root = JsonHelper::Load(kEnemyParamsPath);
    if (!root.is_object()) {
        Logger::LogWarning(std::string("EnemyTuning: ") + kEnemyParamsPath + " が読めません。既定値で動きます");
        return;
    }

    ReadJsonFields(root, kBasicFields, basic_);
    ReadJsonFields(root, kKnightFields, knight_);
    ReadJsonFields(root, kBulletFields, bullet_);
    ReadJsonFields(root, kBossSlamFields, bossSlam_);
    ReadJsonFields(root, kSeparationFields, separation_);
}
