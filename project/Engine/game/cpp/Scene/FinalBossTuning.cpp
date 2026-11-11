/**
 * @file FinalBossTuning.cpp
 * @brief ラスボス戦の調整値（FinalBossTuning）を final_boss.json から読み込む
 */
#include "FinalBossTuning.h"
#include "JsonFieldTable.h"
#include "JsonHelper.h"
#include "Logger.h"
#include <string>

namespace engine::game {
namespace {
constexpr const char* kConfigPath = "Resources/Config/final_boss.json";

/** @brief final_boss.json の項目と調整値のメンバの対応 */
constexpr JsonField<FinalBossTuning> kFinalBossFields[] = {
    // 舞台
    { "stage", "level", &FinalBossTuning::level },
    { "stage", "bossName", &FinalBossTuning::bossName },
    { "stage", "introLine", &FinalBossTuning::introLine },
    { "stage", "rageLine", &FinalBossTuning::rageLine },
    { "stage", "stealLine", &FinalBossTuning::stealLine },
    { "guide", "title", &FinalBossTuning::guideTitle },
    { "guide", "lines", &FinalBossTuning::guide },
    { "guide", "confirm", &FinalBossTuning::guideConfirm },
    { "guide", "minSeconds", &FinalBossTuning::guideMinSeconds },
    { "guide", "fadeSeconds", &FinalBossTuning::guideFadeSeconds },
    { "stage", "bossMaxHp", &FinalBossTuning::bossMaxHp },
    { "stage", "rageHpRatio", &FinalBossTuning::rageHpRatio },
    { "stage", "startDistance", &FinalBossTuning::startDistance },
    { "stage", "edgeMargin", &FinalBossTuning::edgeMargin },
    { "stage", "shadowBaseY", &FinalBossTuning::shadowBaseY },
    { "stage", "shadowColor", &FinalBossTuning::shadowColor },
    { "stage", "shadowRimColor", &FinalBossTuning::shadowRimColor },
    { "stage", "shadowRimColorRage", &FinalBossTuning::shadowRimColorRage },
    { "stage", "shadowFlashColor", &FinalBossTuning::shadowFlashColor },
    { "stage", "shadowKatanaGripRotate", &FinalBossTuning::shadowKatanaGripRotate },
    // 移動とカメラ
    { "move", "strafeSpeed", &FinalBossTuning::strafeSpeed },
    { "move", "minOrbitRadius", &FinalBossTuning::minOrbitRadius },
    { "move", "turnRate", &FinalBossTuning::turnRate },
    { "camera", "back", &FinalBossTuning::cameraBack },
    { "camera", "height", &FinalBossTuning::cameraHeight },
    { "camera", "side", &FinalBossTuning::cameraSide },
    { "camera", "lookAhead", &FinalBossTuning::cameraLookAhead },
    { "camera", "lookHeight", &FinalBossTuning::cameraLookHeight },
    { "camera", "turnRate", &FinalBossTuning::cameraTurnRate },
    { "camera", "followRate", &FinalBossTuning::cameraFollowRate },
    // 武器が集まって刀になる
    { "gather", "seconds", &FinalBossTuning::gatherSeconds },
    { "gather", "radius", &FinalBossTuning::gatherRadius },
    { "gather", "height", &FinalBossTuning::gatherHeight },
    { "gather", "spin", &FinalBossTuning::gatherSpin },
    { "gather", "scale", &FinalBossTuning::gatherScale },
    { "gather", "forgeSeconds", &FinalBossTuning::forgeSeconds },
    { "gather", "descendSeconds", &FinalBossTuning::descendSeconds },
    { "gather", "veilSeconds", &FinalBossTuning::veilSeconds },
    { "gather", "flashColor", &FinalBossTuning::forgeFlashColor },
    // 名乗りと後半戦
    { "intro", "seconds", &FinalBossTuning::introSeconds },
    { "intro", "drawTime", &FinalBossTuning::introDrawTime },
    { "intro", "orbitRadius", &FinalBossTuning::introOrbitRadius },
    { "intro", "orbitSpeed", &FinalBossTuning::introOrbitSpeed },
    { "intro", "cameraHeight", &FinalBossTuning::introCameraHeight },
    { "intro", "letterboxSeconds", &FinalBossTuning::letterboxSeconds },
    { "rage", "seconds", &FinalBossTuning::rageSeconds },
    { "rage", "speedMult", &FinalBossTuning::rageSpeedMult },
    { "rage", "thinkMult", &FinalBossTuning::rageThinkMult },
    // 考える時間と怯み
    { "fight", "thinkMin", &FinalBossTuning::thinkMinSeconds },
    { "fight", "thinkMax", &FinalBossTuning::thinkMaxSeconds },
    { "fight", "staggerWindow", &FinalBossTuning::staggerWindow },
    { "fight", "staggerDamage", &FinalBossTuning::staggerDamage },
    { "fight", "staggerSeconds", &FinalBossTuning::staggerSeconds },
    { "fight", "staggerLean", &FinalBossTuning::staggerLean },
    { "fight", "hitHalfHeight", &FinalBossTuning::hitHalfHeight },
    // 攻撃の予兆
    { "telegraph", "color", &FinalBossTuning::telegraphColor },
    { "telegraph", "pulseSpeed", &FinalBossTuning::telegraphPulseSpeed },
    { "telegraph", "pulseDepth", &FinalBossTuning::telegraphPulseDepth },
    { "telegraph", "lineWidth", &FinalBossTuning::telegraphLineWidth },
    { "telegraph", "aimRadius", &FinalBossTuning::telegraphAimRadius },
    // 間合いを測る
    { "measure", "seconds", &FinalBossTuning::measureSeconds },
    { "measure", "orbitSpeed", &FinalBossTuning::measureOrbitSpeed },
    { "measure", "distance", &FinalBossTuning::measureDistance },
    { "measure", "walkSpeed", &FinalBossTuning::walkSpeed },
    // 走って詰めて連撃
    { "rush", "runSpeed", &FinalBossTuning::runSpeed },
    { "rush", "runTimeout", &FinalBossTuning::runTimeout },
    { "rush", "stopDistance", &FinalBossTuning::rushStopDistance },
    { "rush", "slashes", &FinalBossTuning::rushSlashes },
    { "rush", "slashSeconds", &FinalBossTuning::slashSeconds },
    { "rush", "slashHitRatio", &FinalBossTuning::slashHitRatio },
    { "rush", "slashReach", &FinalBossTuning::slashReach },
    { "rush", "slashLunge", &FinalBossTuning::slashLunge },
    { "rush", "slashDamage", &FinalBossTuning::slashDamage },
    { "rush", "leapSeconds", &FinalBossTuning::leapSeconds },
    { "rush", "leapHeight", &FinalBossTuning::leapHeight },
    { "rush", "leapReach", &FinalBossTuning::leapReach },
    { "rush", "leapDamage", &FinalBossTuning::leapDamage },
    // 突き抜け斬り
    { "iai", "windup", &FinalBossTuning::iaiWindup },
    { "iai", "dashSeconds", &FinalBossTuning::iaiDashSeconds },
    { "iai", "dashPast", &FinalBossTuning::iaiDashPast },
    { "iai", "cutDelay", &FinalBossTuning::iaiCutDelay },
    { "iai", "recover", &FinalBossTuning::iaiRecover },
    { "iai", "damage", &FinalBossTuning::iaiDamage },
    // 撃ち出す剣
    { "phantom", "count", &FinalBossTuning::phantomCount },
    { "phantom", "summonSeconds", &FinalBossTuning::phantomSummonSeconds },
    { "phantom", "interval", &FinalBossTuning::phantomInterval },
    { "phantom", "speed", &FinalBossTuning::phantomSpeed },
    { "phantom", "spread", &FinalBossTuning::phantomSpread },
    { "phantom", "height", &FinalBossTuning::phantomHeight },
    { "phantom", "halfSize", &FinalBossTuning::phantomHalfSize },
    { "phantom", "lifetime", &FinalBossTuning::phantomLifetime },
    { "phantom", "scale", &FinalBossTuning::phantomScale },
    { "phantom", "damage", &FinalBossTuning::phantomDamage },
    // 範囲斬り
    { "space", "telegraph", &FinalBossTuning::spaceTelegraph },
    { "space", "radius", &FinalBossTuning::spaceRadius },
    { "space", "spacing", &FinalBossTuning::spaceSpacing },
    { "space", "damage", &FinalBossTuning::spaceDamage },
    { "space", "recover", &FinalBossTuning::spaceRecover },
    // 瞬身
    { "blink", "seconds", &FinalBossTuning::blinkSeconds },
    { "blink", "behindDistance", &FinalBossTuning::blinkBehindDistance },
    { "blink", "awayDistance", &FinalBossTuning::blinkAwayDistance },
    // 撃破
    { "defeat", "slowSeconds", &FinalBossTuning::defeatSlowSeconds },
    { "defeat", "slowScale", &FinalBossTuning::defeatSlowScale },
    { "defeat", "lieAngle", &FinalBossTuning::defeatLieAngle },
    { "defeat", "lieLift", &FinalBossTuning::defeatLieLift },
    { "defeat", "fallSeconds", &FinalBossTuning::defeatFallSeconds },
    { "defeat", "cinematicSeconds", &FinalBossTuning::defeatCinematicSeconds },
};

/** @brief final_boss.json を読んで調整値を作る（読めなければ既定値） */
FinalBossTuning Load() {
    FinalBossTuning tuning;
    const nlohmann::json root = JsonHelper::Load(kConfigPath);
    if (!root.is_object()) {
        Logger::LogWarning(std::string("FinalBossTuning: ") + kConfigPath + " が読めません。既定値で動きます");
        return tuning;
    }
    ReadJsonFields(root, kFinalBossFields, tuning);
    return tuning;
}
} // namespace

/** @brief 調整値の実体（初回に読み込む） */
FinalBossTuning& FinalBossTuningStore::Mutable() {
    static FinalBossTuning sTuning = Load();
    return sTuning;
}

/** @brief 読み込み済みの調整値を返す */
const FinalBossTuning& FinalBossTuningStore::Get() {
    return Mutable();
}

/** @brief final_boss.json を読み直す */
void FinalBossTuningStore::Reload() {
    Mutable() = Load();
}

} // namespace engine::game
