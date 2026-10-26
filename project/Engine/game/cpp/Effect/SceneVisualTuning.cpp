/** @file SceneVisualTuning.cpp
 * @brief シーン別の描画調整値を項目表で読み込み、初回参照から保持する */
#include "SceneVisualTuning.h"
#include "HitEffectLibrary.h"
#include "JsonFieldTable.h"
#include "JsonHelper.h"
#include <algorithm>
#include <limits>

namespace engine::game {
namespace {
/** @brief 描画設定に使う演出JSONを初回だけ読み込む */
const nlohmann::json& VisualDocument() {
    static const auto kRoot = JsonHelper::Load(HitEffectLibrary::kConfigPath);
    return kRoot;
}

/** @brief 指定した節の値を既定設定へ適用する */
template<class Data, size_t N>
Data ReadSettings(const char* section, const JsonField<Data> (&fields)[N]) {
    const auto& root = VisualDocument();
    const auto found = root.find(section);
    return found != root.end() ? ReadJsonFields(*found, fields) : Data {};
}
} // namespace

/** @brief 水場の色と環境粒子の設定を初回読み込みから共有する */
const WaterVisualSettings& SceneVisualTuning::Water() {
    static const WaterVisualSettings sSettings = [] {
        // JSON の項目名と書き込み先メンバの対応表（初回呼び出しで一度だけ読む）
        constexpr JsonField<WaterVisualSettings> kFields[] = {
            { nullptr, "wavePointCount", &WaterVisualSettings::wavePointCount },
            { nullptr, "waveSpring", &WaterVisualSettings::waveSpring },
            { nullptr, "waveSpread", &WaterVisualSettings::waveSpread },
            { nullptr, "waveDamping", &WaterVisualSettings::waveDamping },
            { nullptr, "waveMaxHeight", &WaterVisualSettings::waveMaxHeight },
            { nullptr, "waveSplashImpulse", &WaterVisualSettings::waveSplashImpulse },
            { nullptr, "waveMoveImpulse", &WaterVisualSettings::waveMoveImpulse },
            { nullptr, "waveImpulseRadius", &WaterVisualSettings::waveImpulseRadius },
            { nullptr, "waveAmbientStrength", &WaterVisualSettings::waveAmbientStrength },
            { nullptr, "waveAmbientFrequency", &WaterVisualSettings::waveAmbientFrequency },
            { nullptr, "waveAmbientSpeed", &WaterVisualSettings::waveAmbientSpeed },
            { nullptr, "refractionStrength", &WaterVisualSettings::refractionStrength },
            { nullptr, "refractionSpeed", &WaterVisualSettings::refractionSpeed },
            { nullptr, "causticScale", &WaterVisualSettings::causticScale },
            { nullptr, "causticFlowSpeed", &WaterVisualSettings::causticFlowSpeed },
            { nullptr, "causticStrength", &WaterVisualSettings::causticStrength },
            { nullptr, "foamWidth", &WaterVisualSettings::foamWidth },
            { nullptr, "foamStrength", &WaterVisualSettings::foamStrength },

            { nullptr, "deepWaterColor", &WaterVisualSettings::deepWaterColor },
            { nullptr, "surfaceWaterColor", &WaterVisualSettings::surfaceWaterColor },
            { nullptr, "surfaceLayerDepth", &WaterVisualSettings::surfaceLayerDepth },
            { nullptr, "rippleIntervalFrames", &WaterVisualSettings::rippleIntervalFrames },
            { nullptr, "rippleEdgeMargin", &WaterVisualSettings::rippleEdgeMargin },
            { nullptr, "rippleSpeed", &WaterVisualSettings::rippleSpeed },
            { nullptr, "rippleColor", &WaterVisualSettings::rippleColor },
            { nullptr, "rippleCount", &WaterVisualSettings::rippleCount },
            { nullptr, "rippleLifetime", &WaterVisualSettings::rippleLifetime },
            { nullptr, "rippleSize", &WaterVisualSettings::rippleSize },
            { nullptr, "glintIntervalFrames", &WaterVisualSettings::glintIntervalFrames },
            { nullptr, "glintEdgeMargin", &WaterVisualSettings::glintEdgeMargin },
            { nullptr, "glintColor", &WaterVisualSettings::glintColor },
            { nullptr, "glintLifetime", &WaterVisualSettings::glintLifetime },
            { nullptr, "glintSize", &WaterVisualSettings::glintSize },
            { nullptr, "causticIntervalFrames", &WaterVisualSettings::causticIntervalFrames },
            { nullptr, "causticEdgeMargin", &WaterVisualSettings::causticEdgeMargin },
            { nullptr, "causticBottomMargin", &WaterVisualSettings::causticBottomMargin },
            { nullptr, "causticTopMargin", &WaterVisualSettings::causticTopMargin },
            { nullptr, "causticDriftX", &WaterVisualSettings::causticDriftX },
            { nullptr, "causticRiseSpeed", &WaterVisualSettings::causticRiseSpeed },
            { nullptr, "causticColor", &WaterVisualSettings::causticColor },
            { nullptr, "causticLifetime", &WaterVisualSettings::causticLifetime },
            { nullptr, "causticSize", &WaterVisualSettings::causticSize },
            { nullptr, "bubbleIntervalFrames", &WaterVisualSettings::bubbleIntervalFrames },
            { nullptr, "bubbleEdgeMargin", &WaterVisualSettings::bubbleEdgeMargin },
            { nullptr, "bubbleBottomMargin", &WaterVisualSettings::bubbleBottomMargin },
            { nullptr, "bubbleTopMargin", &WaterVisualSettings::bubbleTopMargin },
            { nullptr, "bubbleRiseSpeed", &WaterVisualSettings::bubbleRiseSpeed },
            { nullptr, "bubbleColor", &WaterVisualSettings::bubbleColor },
            { nullptr, "bubbleSize", &WaterVisualSettings::bubbleSize },
            { nullptr, "splashRingSpeed", &WaterVisualSettings::splashRingSpeed },
            { nullptr, "splashRingColor", &WaterVisualSettings::splashRingColor },
            { nullptr, "splashRingCount", &WaterVisualSettings::splashRingCount },
            { nullptr, "splashRingLifetime", &WaterVisualSettings::splashRingLifetime },
            { nullptr, "splashRingSize", &WaterVisualSettings::splashRingSize },
            { nullptr, "splashDropSpreadX", &WaterVisualSettings::splashDropSpreadX },
            { nullptr, "splashDropRiseMin", &WaterVisualSettings::splashDropRiseMin },
            { nullptr, "splashDropRiseMax", &WaterVisualSettings::splashDropRiseMax },
            { nullptr, "splashDropCount", &WaterVisualSettings::splashDropCount },
            { nullptr, "splashDropColor", &WaterVisualSettings::splashDropColor },
            { nullptr, "splashDropLifetime", &WaterVisualSettings::splashDropLifetime },
            { nullptr, "splashDropSize", &WaterVisualSettings::splashDropSize },
        };
        auto settings = ReadSettings("waterVisuals", kFields);
        // 編集された値でも固定配列の範囲と積分の安定性を保つ。
        constexpr int kMinPointCount = 4;
        constexpr int kMaxPointCount = 128;
        constexpr float kZero = 0.0f;
        constexpr float kOne = 1.0f;
        constexpr float kMinScale = 0.001f;
        constexpr float kMaxSpring = 120.0f;
        constexpr float kMaxSpread = 180.0f;
        constexpr float kMaxDamping = 20.0f;
        constexpr float kMaxHeight = 0.4f;
        constexpr float kMaxImpulse = 4.0f;
        constexpr float kMaxRadius = 5.0f;
        constexpr float kMaxFrequency = 20.0f;
        constexpr float kMaxRefraction = 0.02f;
        constexpr float kMaxLayerDepth = 10.0f;
        constexpr float kMaxFoamWidth = 0.2f;
        settings.wavePointCount = std::clamp(settings.wavePointCount, kMinPointCount, kMaxPointCount);
        settings.waveSpring = std::clamp(settings.waveSpring, kMinScale, kMaxSpring);
        settings.waveSpread = std::clamp(settings.waveSpread, kZero, kMaxSpread);
        settings.waveDamping = std::clamp(settings.waveDamping, kMinScale, kMaxDamping);
        settings.waveMaxHeight = std::clamp(settings.waveMaxHeight, kZero, kMaxHeight);
        settings.waveSplashImpulse = std::clamp(settings.waveSplashImpulse, kZero, kMaxImpulse);
        settings.waveMoveImpulse = std::clamp(settings.waveMoveImpulse, kZero, kOne);
        settings.waveImpulseRadius = std::clamp(settings.waveImpulseRadius, kMinScale, kMaxRadius);
        settings.waveAmbientStrength = std::clamp(settings.waveAmbientStrength, kZero, kOne);
        settings.waveAmbientFrequency = std::clamp(settings.waveAmbientFrequency, kZero, kMaxFrequency);
        settings.waveAmbientSpeed = std::clamp(settings.waveAmbientSpeed, kZero, kMaxFrequency);
        settings.refractionStrength = std::clamp(settings.refractionStrength, kZero, kMaxRefraction);
        settings.refractionSpeed = std::clamp(settings.refractionSpeed, kZero, kMaxFrequency);
        settings.causticScale = std::clamp(settings.causticScale, kMinScale, kMaxFrequency);
        settings.causticFlowSpeed = std::clamp(settings.causticFlowSpeed, kZero, kMaxFrequency);
        settings.causticStrength = std::clamp(settings.causticStrength, kZero, kOne);
        settings.foamWidth = std::clamp(settings.foamWidth, kMinScale, kMaxFoamWidth);
        settings.foamStrength = std::clamp(settings.foamStrength, kZero, kOne);
        settings.surfaceLayerDepth = std::clamp(settings.surfaceLayerDepth, kMinScale, kMaxLayerDepth);
        // 最小・最大が逆、または0除算になる値を有効な範囲に直す
        settings.splashDropRiseMax = (std::max)(settings.splashDropRiseMin, settings.splashDropRiseMax);
        settings.bubbleRiseSpeed = (std::max)(settings.bubbleRiseSpeed, std::numeric_limits<float>::epsilon());
        return settings;
    }();
    return sSettings;
}

/** @brief プレイヤーの輪郭と明滅の設定を初回読み込みから共有する */
const PlayerVisualSettings& SceneVisualTuning::Player() {
    static const PlayerVisualSettings kSettings = [] {
        // JSON の項目名と書き込み先メンバの対応表（初回呼び出しで一度だけ読む）
        constexpr JsonField<PlayerVisualSettings> kFields[] = {
            { nullptr, "outlineColor", &PlayerVisualSettings::outlineColor },
            { nullptr, "outlineWidth", &PlayerVisualSettings::outlineWidth },
            { nullptr, "fallbackOverrideScale", &PlayerVisualSettings::fallbackOverrideScale },
            { nullptr, "rampageBlinkSpeed", &PlayerVisualSettings::rampageBlinkSpeed },
            { nullptr, "rampageGreenFade", &PlayerVisualSettings::rampageGreenFade },
            { nullptr, "rampageBlueFade", &PlayerVisualSettings::rampageBlueFade },
            { nullptr, "awakenPulseSpeed", &PlayerVisualSettings::awakenPulseSpeed },
            { nullptr, "awakenPulseAmplitude", &PlayerVisualSettings::awakenPulseAmplitude },
            { nullptr, "awakenPulseBase", &PlayerVisualSettings::awakenPulseBase },
            { nullptr, "awakenRed", &PlayerVisualSettings::awakenRed },
            { nullptr, "awakenGreen", &PlayerVisualSettings::awakenGreen },
        };
        auto settings = ReadSettings("playerVisuals", kFields);

        return settings;
    }();
    return kSettings;
}

/** @brief 本編のヒットとフィニッシャー演出の設定を初回読み込みから共有する */
const GameplayVisualSettings& SceneVisualTuning::Gameplay() {
    static const GameplayVisualSettings kSettings = [] {
        // JSON の項目名と書き込み先メンバの対応表（初回呼び出しで一度だけ読む）
        constexpr JsonField<GameplayVisualSettings> kFields[] = {
            { nullptr, "hpBarBackgroundColor", &GameplayVisualSettings::hpBarBackgroundColor },
            { nullptr, "finisherShatterSeconds", &GameplayVisualSettings::finisherShatterSeconds },
            { nullptr, "launchRingSpeed", &GameplayVisualSettings::launchRingSpeed },
            { nullptr, "launchRingColor", &GameplayVisualSettings::launchRingColor },
            { nullptr, "launchRingCount", &GameplayVisualSettings::launchRingCount },
            { nullptr, "launchRingLifetime", &GameplayVisualSettings::launchRingLifetime },
            { nullptr, "launchRingSize", &GameplayVisualSettings::launchRingSize },
            { nullptr, "launchSparkSpreadX", &GameplayVisualSettings::launchSparkSpreadX },
            { nullptr, "launchSparkRiseMin", &GameplayVisualSettings::launchSparkRiseMin },
            { nullptr, "launchSparkRiseMax", &GameplayVisualSettings::launchSparkRiseMax },
            { nullptr, "launchSparkCount", &GameplayVisualSettings::launchSparkCount },
            { nullptr, "launchSparkColor", &GameplayVisualSettings::launchSparkColor },
            { nullptr, "launchSparkLifetime", &GameplayVisualSettings::launchSparkLifetime },
            { nullptr, "launchSparkSize", &GameplayVisualSettings::launchSparkSize },
            { nullptr, "juggleSlashRadiusBase", &GameplayVisualSettings::juggleSlashRadiusBase },
            { nullptr, "juggleSlashRadiusStep", &GameplayVisualSettings::juggleSlashRadiusStep },
            { nullptr, "juggleSlashGreenFade", &GameplayVisualSettings::juggleSlashGreenFade },
            { nullptr, "juggleSlashBlueFade", &GameplayVisualSettings::juggleSlashBlueFade },
            { nullptr, "juggleRingSpeedBase", &GameplayVisualSettings::juggleRingSpeedBase },
            { nullptr, "juggleRingSpeedStep", &GameplayVisualSettings::juggleRingSpeedStep },
            { nullptr, "juggleRingColor", &GameplayVisualSettings::juggleRingColor },
            { nullptr, "juggleRingCount", &GameplayVisualSettings::juggleRingCount },
            { nullptr, "juggleRingLifetime", &GameplayVisualSettings::juggleRingLifetime },
            { nullptr, "juggleRingSize", &GameplayVisualSettings::juggleRingSize },
            { nullptr, "juggleShakeBase", &GameplayVisualSettings::juggleShakeBase },
            { nullptr, "juggleShakeStep", &GameplayVisualSettings::juggleShakeStep },
            { nullptr, "juggleShakeSeconds", &GameplayVisualSettings::juggleShakeSeconds },
            { nullptr, "finishOuterRingSpeed", &GameplayVisualSettings::finishOuterRingSpeed },
            { nullptr, "finishOuterRingColor", &GameplayVisualSettings::finishOuterRingColor },
            { nullptr, "finishOuterRingCount", &GameplayVisualSettings::finishOuterRingCount },
            { nullptr, "finishOuterRingLifetime", &GameplayVisualSettings::finishOuterRingLifetime },
            { nullptr, "finishOuterRingSize", &GameplayVisualSettings::finishOuterRingSize },
            { nullptr, "finishInnerRingSpeed", &GameplayVisualSettings::finishInnerRingSpeed },
            { nullptr, "finishInnerRingColor", &GameplayVisualSettings::finishInnerRingColor },
            { nullptr, "finishInnerRingCount", &GameplayVisualSettings::finishInnerRingCount },
            { nullptr, "finishInnerRingLifetime", &GameplayVisualSettings::finishInnerRingLifetime },
            { nullptr, "finishInnerRingSize", &GameplayVisualSettings::finishInnerRingSize },
            { nullptr, "finishSparkSpreadX", &GameplayVisualSettings::finishSparkSpreadX },
            { nullptr, "finishSparkRiseMin", &GameplayVisualSettings::finishSparkRiseMin },
            { nullptr, "finishSparkRiseMax", &GameplayVisualSettings::finishSparkRiseMax },
            { nullptr, "finishSparkCount", &GameplayVisualSettings::finishSparkCount },
            { nullptr, "finishSparkGreenBase", &GameplayVisualSettings::finishSparkGreenBase },
            { nullptr, "finishSparkGreenStep", &GameplayVisualSettings::finishSparkGreenStep },
            { nullptr, "finishSparkBlue", &GameplayVisualSettings::finishSparkBlue },
            { nullptr, "finishSparkLifetime", &GameplayVisualSettings::finishSparkLifetime },
            { nullptr, "finishSparkSize", &GameplayVisualSettings::finishSparkSize },
            { nullptr, "finisherStartShakeAmount", &GameplayVisualSettings::finisherStartShakeAmount },
            { nullptr, "finisherStartShakeSeconds", &GameplayVisualSettings::finisherStartShakeSeconds },
            { nullptr, "finisherStartWarpImpulse", &GameplayVisualSettings::finisherStartWarpImpulse },
        };
        auto settings = ReadSettings("gameplayVisuals", kFields);
        // 最小・最大が逆、または0除算になる値を有効な範囲に直す
        settings.launchSparkRiseMax = (std::max)(settings.launchSparkRiseMin, settings.launchSparkRiseMax);
        settings.finishSparkRiseMax = (std::max)(settings.finishSparkRiseMin, settings.finishSparkRiseMax);
        return settings;
    }();
    return kSettings;
}

/** @brief 配置物の回復と取得演出の設定を初回読み込みから共有する */
const LevelVisualSettings& SceneVisualTuning::Level() {
    static const LevelVisualSettings kSettings = [] {
        // JSON の項目名と書き込み先メンバの対応表（初回呼び出しで一度だけ読む）
        constexpr JsonField<LevelVisualSettings> kFields[] = {
            { nullptr, "healerEffectOffset", &LevelVisualSettings::healerEffectOffset },
            { nullptr, "healedAllyEffectOffset", &LevelVisualSettings::healedAllyEffectOffset },
            { nullptr, "healInterruptedColor", &LevelVisualSettings::healInterruptedColor },
            { nullptr, "castRingRangeRatio", &LevelVisualSettings::castRingRangeRatio },
            { nullptr, "castRingColor", &LevelVisualSettings::castRingColor },
            { nullptr, "castRingCount", &LevelVisualSettings::castRingCount },
            { nullptr, "castRingSize", &LevelVisualSettings::castRingSize },
            { nullptr, "pulseRingColor", &LevelVisualSettings::pulseRingColor },
            { nullptr, "pulseRingCount", &LevelVisualSettings::pulseRingCount },
            { nullptr, "pulseRingLifetime", &LevelVisualSettings::pulseRingLifetime },
            { nullptr, "pulseRingSize", &LevelVisualSettings::pulseRingSize },
            { nullptr, "allyRingSpeed", &LevelVisualSettings::allyRingSpeed },
            { nullptr, "allyRingColor", &LevelVisualSettings::allyRingColor },
            { nullptr, "allyRingCount", &LevelVisualSettings::allyRingCount },
            { nullptr, "allyRingLifetime", &LevelVisualSettings::allyRingLifetime },
            { nullptr, "allyRingSize", &LevelVisualSettings::allyRingSize },
            { nullptr, "allyBurstColor", &LevelVisualSettings::allyBurstColor },
            { nullptr, "allyBurstCount", &LevelVisualSettings::allyBurstCount },
            { nullptr, "allyBurstLifetime", &LevelVisualSettings::allyBurstLifetime },
            { nullptr, "allyBurstSize", &LevelVisualSettings::allyBurstSize },
            { nullptr, "healerBurstColor", &LevelVisualSettings::healerBurstColor },
            { nullptr, "healerBurstCount", &LevelVisualSettings::healerBurstCount },
            { nullptr, "healerBurstLifetime", &LevelVisualSettings::healerBurstLifetime },
            { nullptr, "healerBurstSize", &LevelVisualSettings::healerBurstSize },
            { nullptr, "pickupPulseSpeed", &LevelVisualSettings::pickupPulseSpeed },
            { nullptr, "pickupPulseAmplitude", &LevelVisualSettings::pickupPulseAmplitude },
            { nullptr, "pickupSpinSpeed", &LevelVisualSettings::pickupSpinSpeed },
            { nullptr, "pickupRingRadius", &LevelVisualSettings::pickupRingRadius },
            { nullptr, "pickupRingCount", &LevelVisualSettings::pickupRingCount },
            { nullptr, "pickupRingLifetime", &LevelVisualSettings::pickupRingLifetime },
            { nullptr, "pickupRingSize", &LevelVisualSettings::pickupRingSize },
            { nullptr, "pickupFlashAlpha", &LevelVisualSettings::pickupFlashAlpha },
            { nullptr, "pickupFlashSeconds", &LevelVisualSettings::pickupFlashSeconds },
        };
        auto settings = ReadSettings("levelVisuals", kFields);

        return settings;
    }();
    return kSettings;
}

/** @brief 訓練戦闘の命中と射撃演出の設定を初回読み込みから共有する */
const BattleCombatVisualSettings& SceneVisualTuning::BattleCombat() {
    static const BattleCombatVisualSettings kSettings = [] {
        // JSON の項目名と書き込み先メンバの対応表（初回呼び出しで一度だけ読む）
        constexpr JsonField<BattleCombatVisualSettings> kFields[] = {
            { nullptr, "slamFlashColor", &BattleCombatVisualSettings::slamFlashColor },
            { nullptr, "slamFlashDuration", &BattleCombatVisualSettings::slamFlashDuration },
            { nullptr, "muzzleSpeedStep", &BattleCombatVisualSettings::muzzleSpeedStep },
            { nullptr, "muzzleLifeTime", &BattleCombatVisualSettings::muzzleLifeTime },
            { nullptr, "muzzleScale", &BattleCombatVisualSettings::muzzleScale },
            { nullptr, "minMuzzleBullets", &BattleCombatVisualSettings::minMuzzleBullets },
            { nullptr, "muzzleHalfRange", &BattleCombatVisualSettings::muzzleHalfRange },
            { nullptr, "hitEffectHeight", &BattleCombatVisualSettings::hitEffectHeight },
            { nullptr, "knightHitEffectHeight", &BattleCombatVisualSettings::knightHitEffectHeight },
            { nullptr, "launcherFlashColor", &BattleCombatVisualSettings::launcherFlashColor },
            { nullptr, "launcherFlashDuration", &BattleCombatVisualSettings::launcherFlashDuration },
            { nullptr, "dummyFlashColor", &BattleCombatVisualSettings::dummyFlashColor },
            { nullptr, "dummyNormalColor", &BattleCombatVisualSettings::dummyNormalColor },
            { nullptr, "burstColumns", &BattleCombatVisualSettings::burstColumns },
            { nullptr, "burstCenterColumn", &BattleCombatVisualSettings::burstCenterColumn },
            { nullptr, "burstSpreadZ", &BattleCombatVisualSettings::burstSpreadZ },
            { nullptr, "burstSpeedBase", &BattleCombatVisualSettings::burstSpeedBase },
            { nullptr, "burstSpeedStep", &BattleCombatVisualSettings::burstSpeedStep },
            { nullptr, "burstRiseBase", &BattleCombatVisualSettings::burstRiseBase },
            { nullptr, "burstRiseSteps", &BattleCombatVisualSettings::burstRiseSteps },
            { nullptr, "burstRiseStep", &BattleCombatVisualSettings::burstRiseStep },
            { nullptr, "burstLifetime", &BattleCombatVisualSettings::burstLifetime },
            { nullptr, "burstSize", &BattleCombatVisualSettings::burstSize },
            { nullptr, "effectRingBaseCount", &BattleCombatVisualSettings::effectRingBaseCount },
            { nullptr, "effectRingLifetime", &BattleCombatVisualSettings::effectRingLifetime },
            { nullptr, "effectRingSize", &BattleCombatVisualSettings::effectRingSize },
            { nullptr, "throwEffectHeight", &BattleCombatVisualSettings::throwEffectHeight },
            { nullptr, "throwRingSpeed", &BattleCombatVisualSettings::throwRingSpeed },
            { nullptr, "throwRingCount", &BattleCombatVisualSettings::throwRingCount },
            { nullptr, "throwRingLifetime", &BattleCombatVisualSettings::throwRingLifetime },
            { nullptr, "throwRingSize", &BattleCombatVisualSettings::throwRingSize },
            { nullptr, "throwSparkCount", &BattleCombatVisualSettings::throwSparkCount },
            { nullptr, "throwSparkSpeedBase", &BattleCombatVisualSettings::throwSparkSpeedBase },
            { nullptr, "throwSparkSpeedStep", &BattleCombatVisualSettings::throwSparkSpeedStep },
            { nullptr, "throwSparkRiseBase", &BattleCombatVisualSettings::throwSparkRiseBase },
            { nullptr, "throwSparkRiseSteps", &BattleCombatVisualSettings::throwSparkRiseSteps },
            { nullptr, "throwSparkRiseStep", &BattleCombatVisualSettings::throwSparkRiseStep },
            { nullptr, "throwSparkLifetime", &BattleCombatVisualSettings::throwSparkLifetime },
            { nullptr, "throwSparkSize", &BattleCombatVisualSettings::throwSparkSize },
            { nullptr, "spinHitRingCount", &BattleCombatVisualSettings::spinHitRingCount },
            { nullptr, "spinHitRingLifetime", &BattleCombatVisualSettings::spinHitRingLifetime },
            { nullptr, "spinHitRingSize", &BattleCombatVisualSettings::spinHitRingSize },
        };
        auto settings = ReadSettings("battleCombatVisuals", kFields);
        // 最小・最大が逆、または0除算になる値を有効な範囲に直す
        settings.burstColumns = (std::max)(settings.burstColumns, 1);
        settings.burstRiseSteps = (std::max)(settings.burstRiseSteps, 1);
        settings.throwSparkRiseSteps = (std::max)(settings.throwSparkRiseSteps, 1);
        return settings;
    }();
    return kSettings;
}

/** @brief 訓練フィニッシャーの開始と解放演出の設定を初回読み込みから共有する */
const BattleFinisherVisualSettings& SceneVisualTuning::BattleFinisher() {
    static const BattleFinisherVisualSettings kSettings = [] {
        // JSON の項目名と書き込み先メンバの対応表（初回呼び出しで一度だけ読む）
        constexpr JsonField<BattleFinisherVisualSettings> kFields[] = {
            { nullptr, "effectHeight", &BattleFinisherVisualSettings::effectHeight },
            { nullptr, "chargeFlashColor", &BattleFinisherVisualSettings::chargeFlashColor },
            { nullptr, "chargeFlashSeconds", &BattleFinisherVisualSettings::chargeFlashSeconds },
            { nullptr, "releaseFlashColor", &BattleFinisherVisualSettings::releaseFlashColor },
        };
        auto settings = ReadSettings("battleFinisherVisuals", kFields);

        return settings;
    }();
    return kSettings;
}

/** @brief 訓練場のポータルと画面演出の設定を初回読み込みから共有する */
const TrainingVisualSettings& SceneVisualTuning::Training() {
    static const TrainingVisualSettings kSettings = [] {
        // JSON の項目名と書き込み先メンバの対応表（初回呼び出しで一度だけ読む）
        constexpr JsonField<TrainingVisualSettings> kFields[] = {
            { nullptr, "warpPortalBlockCount", &TrainingVisualSettings::warpPortalBlockCount },
            { nullptr, "warpPortalBlockSpacing", &TrainingVisualSettings::warpPortalBlockSpacing },
            { nullptr, "warpPortalColor", &TrainingVisualSettings::warpPortalColor },
            { nullptr, "warpPulseBase", &TrainingVisualSettings::warpPulseBase },
            { nullptr, "warpPulseAmplitude", &TrainingVisualSettings::warpPulseAmplitude },
            { nullptr, "warpPulseSpeed", &TrainingVisualSettings::warpPulseSpeed },
            { nullptr, "warpPulseAlpha", &TrainingVisualSettings::warpPulseAlpha },
            { nullptr, "warpLabelWorldY", &TrainingVisualSettings::warpLabelWorldY },
            { nullptr, "warpLabelScale", &TrainingVisualSettings::warpLabelScale },
            { nullptr, "warpLabelColor", &TrainingVisualSettings::warpLabelColor },
            { nullptr, "finisherFlashColor", &TrainingVisualSettings::finisherFlashColor },
            { nullptr, "slashLineLengthMin", &TrainingVisualSettings::slashLineLengthMin },
            { nullptr, "slashLineLengthMax", &TrainingVisualSettings::slashLineLengthMax },
            { nullptr, "slashLineColor", &TrainingVisualSettings::slashLineColor },
            { nullptr, "slashLineThickness", &TrainingVisualSettings::slashLineThickness },
            { nullptr, "slashLineSeconds", &TrainingVisualSettings::slashLineSeconds },
            { nullptr, "pickupFlashColor", &TrainingVisualSettings::pickupFlashColor },
            { nullptr, "pickupFlashSeconds", &TrainingVisualSettings::pickupFlashSeconds },
            { nullptr, "debugTextScale", &TrainingVisualSettings::debugTextScale },
            { nullptr, "debugTextColor", &TrainingVisualSettings::debugTextColor },
        };
        auto settings = ReadSettings("trainingVisuals", kFields);
        // 最小・最大が逆、または0除算になる値を有効な範囲に直す
        settings.slashLineLengthMax = (std::max)(settings.slashLineLengthMin, settings.slashLineLengthMax);
        return settings;
    }();
    return kSettings;
}

/** @brief 訓練場の案内と命中演出の設定を初回読み込みから共有する */
const BattleVisualSettings& SceneVisualTuning::Battle() {
    static const BattleVisualSettings kSettings = [] {
        // JSON の項目名と書き込み先メンバの対応表（初回呼び出しで一度だけ読む）
        constexpr JsonField<BattleVisualSettings> kFields[] = {
            { nullptr, "barUp", &BattleVisualSettings::barUp },
            { nullptr, "barH", &BattleVisualSettings::barH },
            { nullptr, "barW", &BattleVisualSettings::barW },
            { nullptr, "warpPortalBlockCount", &BattleVisualSettings::warpPortalBlockCount },
            { nullptr, "warpPortalBlockSpacing", &BattleVisualSettings::warpPortalBlockSpacing },
            { nullptr, "warpPortalColor", &BattleVisualSettings::warpPortalColor },
            { nullptr, "warpPulseBase", &BattleVisualSettings::warpPulseBase },
            { nullptr, "warpPulseAmplitude", &BattleVisualSettings::warpPulseAmplitude },
            { nullptr, "warpPulseSpeed", &BattleVisualSettings::warpPulseSpeed },
            { nullptr, "warpLabelWorldY", &BattleVisualSettings::warpLabelWorldY },
            { nullptr, "hpBarBackgroundColor", &BattleVisualSettings::hpBarBackgroundColor },
            { nullptr, "hpBarInitialColor", &BattleVisualSettings::hpBarInitialColor },
            { nullptr, "hpBarGreenScale", &BattleVisualSettings::hpBarGreenScale },
            { nullptr, "hpBarGreenBase", &BattleVisualSettings::hpBarGreenBase },
            { nullptr, "hpBarAlpha", &BattleVisualSettings::hpBarAlpha },
            { nullptr, "finisherShatterSeconds", &BattleVisualSettings::finisherShatterSeconds },
            { nullptr, "hitRingSpeed", &BattleVisualSettings::hitRingSpeed },
            { nullptr, "hitRingColor", &BattleVisualSettings::hitRingColor },
            { nullptr, "hitRingCount", &BattleVisualSettings::hitRingCount },
            { nullptr, "hitRingLifetime", &BattleVisualSettings::hitRingLifetime },
            { nullptr, "hitRingSize", &BattleVisualSettings::hitRingSize },
            { nullptr, "hitSparkSpreadX", &BattleVisualSettings::hitSparkSpreadX },
            { nullptr, "hitSparkRiseMin", &BattleVisualSettings::hitSparkRiseMin },
            { nullptr, "hitSparkRiseMax", &BattleVisualSettings::hitSparkRiseMax },
            { nullptr, "hitSparkCount", &BattleVisualSettings::hitSparkCount },
            { nullptr, "hitSparkColor", &BattleVisualSettings::hitSparkColor },
            { nullptr, "hitSparkLifetime", &BattleVisualSettings::hitSparkLifetime },
            { nullptr, "hitSparkSize", &BattleVisualSettings::hitSparkSize },
            { nullptr, "hintScale", &BattleVisualSettings::hintScale },
            { nullptr, "hintColor", &BattleVisualSettings::hintColor },
            { nullptr, "hintShadowColor", &BattleVisualSettings::hintShadowColor },
            { nullptr, "hintShadowOffset", &BattleVisualSettings::hintShadowOffset },
            { nullptr, "returnLabelColor", &BattleVisualSettings::returnLabelColor },
            { nullptr, "lockMarkerHeight", &BattleVisualSettings::lockMarkerHeight },
            { nullptr, "lockMarkerHalfWidth", &BattleVisualSettings::lockMarkerHalfWidth },
            { nullptr, "lockMarkerScale", &BattleVisualSettings::lockMarkerScale },
            { nullptr, "lockMarkerColor", &BattleVisualSettings::lockMarkerColor },
            { nullptr, "hintLineHeight", &BattleVisualSettings::hintLineHeight },
            { nullptr, "colliderLabelColor", &BattleVisualSettings::colliderLabelColor },
        };
        auto settings = ReadSettings("battleVisuals", kFields);
        // 最小・最大が逆、または0除算になる値を有効な範囲に直す
        settings.hitSparkRiseMax = (std::max)(settings.hitSparkRiseMin, settings.hitSparkRiseMax);
        return settings;
    }();
    return kSettings;
}

/** @brief 敵の識別色とHPバーの設定を初回読み込みから共有する */
const InitialActorsVisualSettings& SceneVisualTuning::InitialActors() {
    static const InitialActorsVisualSettings kSettings = [] {
        // JSON の項目名と書き込み先メンバの対応表（初回呼び出しで一度だけ読む）
        constexpr JsonField<InitialActorsVisualSettings> kFields[] = {
            { nullptr, "spearEnemyColor", &InitialActorsVisualSettings::spearEnemyColor },
            { nullptr, "daggerEnemyColor", &InitialActorsVisualSettings::daggerEnemyColor },
            { nullptr, "heavyEnemyColor", &InitialActorsVisualSettings::heavyEnemyColor },
            { nullptr, "swordEnemyColor", &InitialActorsVisualSettings::swordEnemyColor },
            { nullptr, "hpBarBackgroundColor", &InitialActorsVisualSettings::hpBarBackgroundColor },
        };
        auto settings = ReadSettings("initialActorsVisuals", kFields);

        return settings;
    }();
    return kSettings;
}

} // namespace engine::game
