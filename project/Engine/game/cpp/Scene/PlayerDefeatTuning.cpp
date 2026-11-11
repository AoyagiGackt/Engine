/**
 * @file PlayerDefeatTuning.cpp
 * @brief やられ演出の調整値（DefeatTuning）を player_defeat.json から読み込む
 */
#include "PlayerDefeatTuning.h"
#include "JsonFieldTable.h"
#include "JsonHelper.h"
#include "Logger.h"
#include <string>

namespace engine::game {
namespace {
constexpr const char* kConfigPath = "Resources/Config/player_defeat.json";

constexpr JsonField<DefeatTuning> kDefeatFields[] = {
    { "camera", "closeZoom", &DefeatTuning::closeZoom },
    { "camera", "zoomSeconds", &DefeatTuning::zoomSeconds },
    { "camera", "closeHeight", &DefeatTuning::closeHeight },
    { "hit", "seconds", &DefeatTuning::hitSeconds },
    { "hit", "shakeWidth", &DefeatTuning::hitShakeWidth },
    { "hit", "shakeSpeed", &DefeatTuning::hitShakeSpeed },
    { "hit", "impact", &DefeatTuning::hitImpact },
    { "hit", "flash", &DefeatTuning::hitFlash },
    { "hit", "sound", &DefeatTuning::hitSound },
    { "hit", "soundVolume", &DefeatTuning::hitSoundVolume },
    { "launch", "seconds", &DefeatTuning::launchSeconds },
    { "launch", "distance", &DefeatTuning::launchDistance },
    { "launch", "height", &DefeatTuning::launchHeight },
    { "launch", "stageEdgeMargin", &DefeatTuning::stageEdgeMargin },
    { "launch", "wallClearance", &DefeatTuning::wallClearance },
    { "launch", "wallBandBottom", &DefeatTuning::wallBandBottom },
    { "launch", "spinTurns", &DefeatTuning::spinTurns },
    { "weapon", "handHeight", &DefeatTuning::weaponHandHeight },
    { "weapon", "distance", &DefeatTuning::weaponDistance },
    { "weapon", "height", &DefeatTuning::weaponHeight },
    { "weapon", "seconds", &DefeatTuning::weaponSeconds },
    { "weapon", "spinTurns", &DefeatTuning::weaponSpinTurns },
    { "weapon", "groundOffsetY", &DefeatTuning::weaponGroundOffsetY },
    { "weapon", "lieAngleDegrees", &DefeatTuning::weaponLieAngleDegrees },
    { "weapon", "landSparks", &DefeatTuning::weaponLandSparks },
    { "enemyBounce", "clearance", &DefeatTuning::enemyClearance },
    { "enemyBounce", "topOffset", &DefeatTuning::enemyTopOffset },
    { "enemyBounce", "extraDistance", &DefeatTuning::bounceExtraDistance },
    { "enemyBounce", "seconds", &DefeatTuning::enemyBounceSeconds },
    { "enemyBounce", "height", &DefeatTuning::enemyBounceHeight },
    { "enemyBounce", "spinTurns", &DefeatTuning::enemyBounceSpinTurns },
    { "enemyBounce", "impact", &DefeatTuning::enemyBounceImpact },
    { "enemyBounce", "sparks", &DefeatTuning::enemyBounceSparks },
    { "down", "lieAngleDegrees", &DefeatTuning::lieAngleDegrees },
    { "down", "lieLift", &DefeatTuning::lieLift },
    { "down", "bounceHeight", &DefeatTuning::bounceHeight },
    { "down", "bounceSeconds", &DefeatTuning::bounceSeconds },
    { "down", "seconds", &DefeatTuning::downSeconds },
    { "down", "impact", &DefeatTuning::downImpact },
    { "down", "ring", &DefeatTuning::downRing },
    { "down", "sparks", &DefeatTuning::downSparks },
    { "struggle", "riseSeconds", &DefeatTuning::struggleRiseSeconds },
    { "struggle", "riseDegrees", &DefeatTuning::struggleRiseDegrees },
    { "struggle", "holdSeconds", &DefeatTuning::struggleHoldSeconds },
    { "struggle", "shakeDegrees", &DefeatTuning::struggleShakeDegrees },
    { "struggle", "shakeSpeed", &DefeatTuning::struggleShakeSpeed },
    { "struggle", "reachArm", &DefeatTuning::reachArm },
    { "struggle", "reachForearm", &DefeatTuning::reachForearm },
    { "struggle", "reachDirection", &DefeatTuning::reachDirection },
    { "struggle", "reachWeight", &DefeatTuning::reachWeight },
    { "struggle", "reachForearmWeight", &DefeatTuning::reachForearmWeight },
    { "struggle", "reachShake", &DefeatTuning::reachShake },
    { "struggle", "reachShakeSpeed", &DefeatTuning::reachShakeSpeed },
    { "struggle", "collapseSeconds", &DefeatTuning::collapseSeconds },
    { "struggle", "afterSeconds", &DefeatTuning::afterCollapseSeconds },
    { "struggle", "impact", &DefeatTuning::collapseImpact },
    { "struggle", "sparks", &DefeatTuning::collapseSparks },
    { "gameover", "fadeSeconds", &DefeatTuning::fadeSeconds },
    { "gameover", "textDropSeconds", &DefeatTuning::textDropSeconds },
    { "gameover", "textDropHeight", &DefeatTuning::textDropHeight },
    { "gameover", "holdSeconds", &DefeatTuning::holdSeconds },
    { "gameover", "overlayColor", &DefeatTuning::overlayColor },
    { "gameover", "text", &DefeatTuning::text },
    { "gameover", "skipLockSeconds", &DefeatTuning::skipLockSeconds },
    { "choice", "retryLabel", &DefeatTuning::retryLabel },
    { "choice", "mapLabel", &DefeatTuning::mapLabel },
    { "choice", "lockSeconds", &DefeatTuning::choiceLockSeconds },
};

/** @brief player_defeat.json からやられ演出の調整値を読み込む */
DefeatTuning LoadTuning() {
    DefeatTuning tuning;
    const nlohmann::json root = JsonHelper::Load(kConfigPath);
    if (!root.is_object()) {
        Logger::LogWarning(std::string("PlayerDefeat: ") + kConfigPath + " が読めません。既定値で動きます");
        return tuning;
    }
    ReadJsonFields(root, kDefeatFields, tuning);
    return tuning;
}
} // namespace

/** @brief 調整値の実体（初回に読み込む） */
DefeatTuning& DefeatTuningStore::Mutable() {
    static DefeatTuning sTuning = LoadTuning();
    return sTuning;
}

/** @brief 読み込み済みの調整値を返す */
const DefeatTuning& DefeatTuningStore::Get() {
    return Mutable();
}

/** @brief player_defeat.json を読み直す */
void DefeatTuningStore::Reload() {
    Mutable() = LoadTuning();
}

} // namespace engine::game
