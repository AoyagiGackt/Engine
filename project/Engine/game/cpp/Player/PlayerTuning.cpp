/**
 * @file PlayerTuning.cpp
 * @brief PlayerTuningの調整値の読み込みを実装するファイル
 */
#include "PlayerTuning.h"
#include "JsonFieldTable.h"
#include "JsonHelper.h"
#include "Logger.h"
#include <string>
using namespace engine;
using namespace engine::game;

namespace {
constexpr const char* kPlayerParamsPath = "Resources/Config/player_params.json";

constexpr JsonField<PlayerTuning> kFields[] = {
    { "movement", "groundY", &PlayerTuning::groundY },
    { "movement", "ceilingY", &PlayerTuning::ceilingY },
    { "movement", "gravity", &PlayerTuning::gravity },
    { "movement", "jumpPower", &PlayerTuning::jumpPower },
    { "movement", "speed", &PlayerTuning::speed },
    { "movement", "minEnemyDistanceX", &PlayerTuning::minEnemyDistanceX },
    { "movement", "minEnemyDistanceY", &PlayerTuning::minEnemyDistanceY },
    { "movement", "invincibleDuration", &PlayerTuning::invincibleDuration },
    { "water", "waterGravity", &PlayerTuning::waterGravity },
    { "water", "waterSpeed", &PlayerTuning::waterSpeed },
    { "water", "swimAccel", &PlayerTuning::swimAccel },
    { "water", "swimMaxVY", &PlayerTuning::swimMaxVY },
    { "water", "sinkMaxVY", &PlayerTuning::sinkMaxVY },
    { "water", "waterEntryImpactDamping", &PlayerTuning::waterEntryImpactDamping },
    { "dodge", "dodgeDistance", &PlayerTuning::dodgeDistance },
    { "dodge", "dodgeDuration", &PlayerTuning::dodgeDuration },
    { "dodge", "dodgeCooldown", &PlayerTuning::dodgeCooldown },
    { "dodge", "dodgeSpamWindow", &PlayerTuning::dodgeSpamWindow },
    { "dodge", "dodgeAnimSpeed", &PlayerTuning::dodgeAnimSpeed },
    { "dodge", "backDodgeAnimSpeed", &PlayerTuning::backDodgeAnimSpeed },
    { "dodge", "backDodgeLean", &PlayerTuning::backDodgeLean },
    { "dodge", "backDodgeHopHeight", &PlayerTuning::backDodgeHopHeight },
    { "warp", "warpDistance", &PlayerTuning::warpDistance },
    { "warp", "warpAwakenedDistance", &PlayerTuning::warpAwakenedDistance },
    { "warp", "warpDuration", &PlayerTuning::warpDuration },
    { "warp", "warpCooldown", &PlayerTuning::warpCooldown },
    { "warp", "warpAwakenedCooldown", &PlayerTuning::warpAwakenedCooldown },
    { "warp", "warpGaugeCost", &PlayerTuning::warpGaugeCost },
    { "warp", "warpAnimSpeed", &PlayerTuning::warpAnimSpeed },
    { "awaken", "awakenDuration", &PlayerTuning::awakenDuration },
    { "awaken", "awakenActivationThreshold", &PlayerTuning::awakenActivationThreshold },
    { "awaken", "awakenedSpeedMult", &PlayerTuning::awakenedSpeedMult },
    { "awaken", "awakenedJumpMult", &PlayerTuning::awakenedJumpMult },
    { "awaken", "awakenExtraHitInterval", &PlayerTuning::awakenExtraHitInterval },
    { "combo", "weaponSwitchWindow", &PlayerTuning::weaponSwitchWindow },
    { "combo", "weaponSwitchLunge", &PlayerTuning::weaponSwitchLunge },
    { "combo", "launchFollowWindow", &PlayerTuning::launchFollowWindow },
    { "combo", "launchFollowJumpMult", &PlayerTuning::launchFollowJumpMult },
    { "combo", "airAttackFallDamping", &PlayerTuning::airAttackFallDamping },
    { "combo", "attackAnimSpeed", &PlayerTuning::attackAnimSpeed },
    { "combo", "finisherReleaseAnimSpeed", &PlayerTuning::finisherReleaseAnimSpeed },
    { "combo", "stealStabAnimSpeed", &PlayerTuning::stealStabAnimSpeed },
    { "rampage", "rampageSpeed", &PlayerTuning::rampageSpeed },
    { "rampage", "rampageReachDistance", &PlayerTuning::rampageReachDistance },
    { "rampage", "rampageLaunchSpeed", &PlayerTuning::rampageLaunchSpeed },
    { "rampage", "juggleRadius", &PlayerTuning::juggleRadius },
    { "skills", "dashDuration", &PlayerTuning::dashDuration },
    { "skills", "skillHoldHeight", &PlayerTuning::skillHoldHeight },
    { "skills", "daggerAirDashDist", &PlayerTuning::daggerAirDashDist },
    { "skills", "daggerAirDashAnimSpeed", &PlayerTuning::daggerAirDashAnimSpeed },
    { "skills", "daggerSkillCooldown", &PlayerTuning::daggerSkillCooldown },
    { "skills", "swordSkillCooldown", &PlayerTuning::swordSkillCooldown },
    { "skills", "spearSkillCooldown", &PlayerTuning::spearSkillCooldown },
    { "skills", "hammerSkillCooldown", &PlayerTuning::hammerSkillCooldown },
    { "skills", "ballShootInterval", &PlayerTuning::ballShootInterval },
    { "skills", "ballSpinSpeed", &PlayerTuning::ballSpinSpeed },
    { "skills", "ballSpinShotAnimSpeed", &PlayerTuning::ballSpinShotAnimSpeed },
    { "skills", "greatswordThrowDist", &PlayerTuning::greatswordThrowDist },
    { "skills", "greatswordThrowTravelTime", &PlayerTuning::greatswordThrowTravelTime },
    { "skills", "greatswordVortexMaxDuration", &PlayerTuning::greatswordVortexMaxDuration },
    { "skills", "greatswordSpinHitInterval", &PlayerTuning::greatswordSpinHitInterval },
    { "skills", "greatswordThrowCooldown", &PlayerTuning::greatswordThrowCooldown },
    { "skills", "greatswordReturnTime", &PlayerTuning::greatswordReturnTime },
    { "skills", "greatswordThrowAnimSpeed", &PlayerTuning::greatswordThrowAnimSpeed },
    { "skills", "greatswordThrowSpinSpeed", &PlayerTuning::greatswordThrowSpinSpeed },
    { "skills", "greatswordVortexBobAmplitude", &PlayerTuning::greatswordVortexBobAmplitude },
    { "skills", "greatswordVortexBobSpeed", &PlayerTuning::greatswordVortexBobSpeed },
    { "skills", "scytheHoverMax", &PlayerTuning::scytheHoverMax },
    { "skills", "scytheHoverRecoverRate", &PlayerTuning::scytheHoverRecoverRate },
    { "skills", "scytheHoverVYCap", &PlayerTuning::scytheHoverVYCap },
    { "skills", "scytheHoverAnimSpeed", &PlayerTuning::scytheHoverAnimSpeed },
    { "skills", "axeChargeDist", &PlayerTuning::axeChargeDist },
    { "skills", "axeSkillCooldown", &PlayerTuning::axeSkillCooldown },
    { "skills", "axeRageDuration", &PlayerTuning::axeRageDuration },
    { "skills", "axeRageDamageMult", &PlayerTuning::axeRageDamageMult },
    { "skills", "axeChargeAnimSpeed", &PlayerTuning::axeChargeAnimSpeed },
    { "dodge", "dodgeBufferFrames", &PlayerTuning::dodgeBufferFrames },
    { "awaken", "awakenExtraHits", &PlayerTuning::awakenExtraHits },
    { "rampage", "juggleMaxSlashes", &PlayerTuning::juggleMaxSlashes },
};

PlayerTuning LoadFromFile()
{
    PlayerTuning loaded;
    const nlohmann::json root = JsonHelper::Load(kPlayerParamsPath);
    if (!root.is_object()) {
        Logger::LogWarning(std::string("PlayerTuning: ") + kPlayerParamsPath + " が読めません。既定値で動きます");
        return loaded;
    }
    ReadJsonFields(root, kFields, loaded);
    return loaded;
}

PlayerTuning& Instance()
{
    static PlayerTuning tuning = LoadFromFile();
    return tuning;
}
} // namespace

const PlayerTuning& PlayerTuning::Get()
{
    return Instance();
}

void PlayerTuning::Reload()
{
    Instance() = LoadFromFile();
}
