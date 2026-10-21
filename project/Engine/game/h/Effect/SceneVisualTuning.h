/** @file SceneVisualTuning.h
 * @brief シーンと配置物の描画設定を演出JSONから取得する */
#pragma once
#include "Vector3.h"
#include "Vector4.h"

namespace engine::game {

/** @brief 水場の色と環境粒子の調整値 */
struct WaterVisualSettings {
    Vector4 deepWaterColor = { 0.03f, 0.18f, 0.58f, 0.94f };
    Vector4 surfaceWaterColor = { 0.20f, 0.58f, 0.96f, 0.68f };
    float surfaceLayerDepth = 1.5f; ///< 水面から明るい層を重ねる深さ
    int rippleIntervalFrames = 25;
    float rippleEdgeMargin = 1.5f;
    float rippleSpeed = 0.9f;
    Vector4 rippleColor = { 0.55f, 0.82f, 1.0f, 0.55f };
    int rippleCount = 6;
    float rippleLifetime = 0.7f;
    float rippleSize = 0.08f;
    int glintIntervalFrames = 4;
    float glintEdgeMargin = 1.0f;
    Vector4 glintColor = { 0.88f, 0.98f, 1.0f, 1.0f };
    float glintLifetime = 0.2f;
    float glintSize = 0.1f;
    int causticIntervalFrames = 8;
    float causticEdgeMargin = 0.5f;
    float causticBottomMargin = 0.3f;
    float causticTopMargin = 0.7f;
    float causticDriftX = 0.2f;
    float causticRiseSpeed = 0.4f;
    Vector4 causticColor = { 0.8f, 1.0f, 0.88f, 0.4f };
    float causticLifetime = 2.0f;
    float causticSize = 0.28f;
    int bubbleIntervalFrames = 12;
    float bubbleEdgeMargin = 0.5f;
    float bubbleBottomMargin = 0.2f;
    float bubbleTopMargin = 1.0f;
    float bubbleRiseSpeed = 0.9f;
    Vector4 bubbleColor = { 0.7f, 0.9f, 1.0f, 0.45f };
    float bubbleSize = 0.07f;
    float splashRingSpeed = 2.2f;
    Vector4 splashRingColor = { 0.6f, 0.86f, 1.0f, 0.9f };
    int splashRingCount = 8;
    float splashRingLifetime = 0.4f;
    float splashRingSize = 0.12f;
    float splashDropSpreadX = 2.5f;
    float splashDropRiseMin = 2.0f;
    float splashDropRiseMax = 4.5f;
    int splashDropCount = 5;
    Vector4 splashDropColor = { 0.55f, 0.82f, 1.0f, 0.9f };
    float splashDropLifetime = 0.5f;
    float splashDropSize = 0.1f;
};

/** @brief プレイヤーの輪郭と明滅の調整値 */
struct PlayerVisualSettings {
    Vector4 outlineColor = { 0.0f, 0.0f, 0.0f, 0.55f };
    float outlineWidth = 0.006f;
    float fallbackOverrideScale = 0.5f;
    float rampageBlinkSpeed = 3.0f; ///< 乱舞の斬撃回数あたりの明滅の速さ
    float rampageGreenFade = 0.4f;
    float rampageBlueFade = 0.6f;
    float awakenPulseSpeed = 6.0f;
    float awakenPulseAmplitude = 0.3f;
    float awakenPulseBase = 0.7f;
    float awakenRed = 0.15f;
    float awakenGreen = 0.55f;
};

/** @brief 本編のヒットとフィニッシャー演出の調整値 */
struct GameplayVisualSettings {
    Vector4 hpBarBackgroundColor = { 0.2f, 0.2f, 0.2f, 0.8f };
    float finisherShatterSeconds = 0.9f;
    float launchRingSpeed = 5.5f;
    Vector4 launchRingColor = { 1.0f, 0.55f, 0.1f, 1.0f };
    int launchRingCount = 20;
    float launchRingLifetime = 0.45f;
    float launchRingSize = 0.28f;
    float launchSparkSpreadX = 4.0f;
    float launchSparkRiseMin = 3.0f;
    float launchSparkRiseMax = 7.0f;
    int launchSparkCount = 10;
    Vector4 launchSparkColor = { 1.0f, 0.65f, 0.15f, 1.0f };
    float launchSparkLifetime = 0.8f;
    float launchSparkSize = 0.18f;
    float juggleSlashRadiusBase = 1.2f;
    float juggleSlashRadiusStep = 0.12f;
    float juggleSlashGreenFade = 0.06f;
    float juggleSlashBlueFade = 0.09f;
    float juggleRingSpeedBase = 2.5f;
    float juggleRingSpeedStep = 0.2f;
    Vector4 juggleRingColor = { 1.0f, 0.9f, 0.5f, 0.8f };
    int juggleRingCount = 8;
    float juggleRingLifetime = 0.25f;
    float juggleRingSize = 0.15f;
    float juggleShakeBase = 0.12f;
    float juggleShakeStep = 0.01f;
    float juggleShakeSeconds = 0.10f;
    float finishOuterRingSpeed = 8.0f;
    Vector4 finishOuterRingColor = { 1.0f, 0.3f, 0.3f, 1.0f };
    int finishOuterRingCount = 24;
    float finishOuterRingLifetime = 0.5f;
    float finishOuterRingSize = 0.35f;
    float finishInnerRingSpeed = 5.0f;
    Vector4 finishInnerRingColor = { 1.0f, 1.0f, 0.5f, 1.0f };
    int finishInnerRingCount = 16;
    float finishInnerRingLifetime = 0.45f;
    float finishInnerRingSize = 0.30f;
    float finishSparkSpreadX = 6.0f;
    float finishSparkRiseMin = 4.0f;
    float finishSparkRiseMax = 10.0f;
    int finishSparkCount = 16;
    float finishSparkGreenBase = 0.4f;
    float finishSparkGreenStep = 0.04f;
    float finishSparkBlue = 0.1f;
    float finishSparkLifetime = 1.0f;
    float finishSparkSize = 0.20f;
    float finisherStartShakeAmount = 0.20f;
    float finisherStartShakeSeconds = 0.15f;
    float finisherStartWarpImpulse = 0.4f;
};

/** @brief 配置物の回復と取得演出の調整値 */
struct LevelVisualSettings {
    Vector3 healerEffectOffset = { 0.0f, 0.7f, 0.0f }; ///< 足元から演出を出す位置
    Vector3 healedAllyEffectOffset = { 0.0f, 0.65f, 0.0f };
    Vector4 healInterruptedColor = { 1.0f, 0.9f, 0.35f, 1.0f };
    float castRingRangeRatio = 0.5f; ///< 詠唱中のリングを回復範囲のどれだけまで広げるか
    Vector4 castRingColor = { 0.3f, 1.0f, 0.4f, 0.4f };
    int castRingCount = 16;
    float castRingSize = 0.16f;
    Vector4 pulseRingColor = { 0.2f, 1.0f, 0.35f, 0.75f };
    int pulseRingCount = 24;
    float pulseRingLifetime = 0.65f;
    float pulseRingSize = 0.22f;
    float allyRingSpeed = 1.4f;
    Vector4 allyRingColor = { 0.25f, 1.0f, 0.4f, 0.9f };
    int allyRingCount = 12;
    float allyRingLifetime = 0.55f;
    float allyRingSize = 0.16f;
    Vector4 allyBurstColor = { 0.55f, 1.0f, 0.65f, 0.85f };
    int allyBurstCount = 8;
    float allyBurstLifetime = 0.7f;
    float allyBurstSize = 0.12f;
    Vector4 healerBurstColor = { 0.4f, 1.0f, 0.55f, 1.0f };
    int healerBurstCount = 14;
    float healerBurstLifetime = 1.0f;
    float healerBurstSize = 0.18f;
    float pickupPulseSpeed = 5.0f; ///< 脈動の速さ
    float pickupPulseAmplitude = 0.15f; ///< 脈動によるスケールの揺れ幅
    float pickupSpinSpeed = 1.5f; ///< 回転の速さ（ラジアン毎秒）
    float pickupRingRadius = 3.0f; ///< 回収時に広がるリングの半径
    int pickupRingCount = 18;
    float pickupRingLifetime = 0.45f;
    float pickupRingSize = 0.3f;
    float pickupFlashAlpha = 0.2f;
    float pickupFlashSeconds = 0.08f;
};

/** @brief 訓練戦闘の命中と射撃演出の調整値 */
struct BattleCombatVisualSettings {
    Vector4 slamFlashColor = { 1.0f, 0.6f, 0.3f, 0.30f };
    float slamFlashDuration = 0.10f;
    float muzzleSpeedStep = 1.0f; ///< /< 1粒ごとの速度加算
    float muzzleLifeTime = 0.35f; ///< /< パーティクル寿命（秒）
    float muzzleScale = 0.14f; ///< /< パーティクルの大きさ
    int minMuzzleBullets = 2; ///< /< 単発の銃でも扇に見えるよう最低限出す粒数
    float muzzleHalfRange = 0.5f; ///< /< 粒の並びを -0.5〜+0.5 に割り振る
    float hitEffectHeight = 0.5f; ///< /< ダミー中心からヒット演出を出す高さ
    float knightHitEffectHeight = 0.7f;
    Vector4 launcherFlashColor = { 1.0f, 0.95f, 0.7f, 0.25f };
    float launcherFlashDuration = 0.08f;
    Vector4 dummyFlashColor = { 1.5f, 1.5f, 1.5f, 1.0f };
    Vector4 dummyNormalColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    int burstColumns = 5;
    int burstCenterColumn = 2;
    float burstSpreadZ = 0.5f;
    float burstSpeedBase = 2.0f;
    float burstSpeedStep = 0.25f;
    float burstRiseBase = 2.0f;
    int burstRiseSteps = 4;
    float burstRiseStep = 0.7f;
    float burstLifetime = 0.5f;
    float burstSize = 0.16f;
    int effectRingBaseCount = 12;
    float effectRingLifetime = 0.28f;
    float effectRingSize = 0.16f;
    float throwEffectHeight = 0.5f;
    float throwRingSpeed = 1.2f;
    int throwRingCount = 10;
    float throwRingLifetime = 0.22f;
    float throwRingSize = 0.16f;
    int throwSparkCount = 6;
    float throwSparkSpeedBase = 6.0f;
    float throwSparkSpeedStep = 1.2f;
    float throwSparkRiseBase = 1.0f;
    int throwSparkRiseSteps = 3;
    float throwSparkRiseStep = 0.6f;
    float throwSparkLifetime = 0.35f;
    float throwSparkSize = 0.14f;
    int spinHitRingCount = 14;
    float spinHitRingLifetime = 0.30f;
    float spinHitRingSize = 0.18f;
};

/** @brief 訓練フィニッシャーの開始と解放演出の調整値 */
struct BattleFinisherVisualSettings {
    float effectHeight = 0.5f; ///< 足元から演出を出す高さ
    Vector4 chargeFlashColor = { 0.75f, 0.95f, 1.0f, 0.35f };
    float chargeFlashSeconds = 0.10f;
    Vector4 releaseFlashColor = { 0.75f, 0.95f, 1.0f, 0.65f };
};

/** @brief 訓練場のポータルと画面演出の調整値 */
struct TrainingVisualSettings {
    int warpPortalBlockCount = 5;
    float warpPortalBlockSpacing = 1.0f;
    Vector4 warpPortalColor = { 0.1f, 0.9f, 1.0f, 0.9f };
    float warpPulseBase = 0.6f;
    float warpPulseAmplitude = 0.4f;
    float warpPulseSpeed = 4.0f;
    float warpPulseAlpha = 0.85f;
    float warpLabelWorldY = 5.0f;
    float warpLabelScale = 1.5f;
    Vector4 warpLabelColor = { 0.2f, 1.0f, 1.0f, 1.0f };
    Vector4 finisherFlashColor = { 0.75f, 0.95f, 1.0f, 0.65f };
    float slashLineLengthMin = 4.0f;
    float slashLineLengthMax = 9.0f;
    Vector4 slashLineColor = { 0.75f, 0.95f, 1.0f, 1.0f };
    float slashLineThickness = 5.0f;
    float slashLineSeconds = 0.22f;
    Vector4 pickupFlashColor = { 0.55f, 0.9f, 1.0f, 0.22f };
    float pickupFlashSeconds = 0.08f;
    float debugTextScale = 1.2f;
    Vector4 debugTextColor = { 0.6f, 1.0f, 0.6f, 0.85f };
};

/** @brief 訓練場の案内と命中演出の調整値 */
struct BattleVisualSettings {
    float barUp = 70.0f;
    float barH = 8.0f;
    float barW = 60.0f;
    int warpPortalBlockCount = 5;
    float warpPortalBlockSpacing = 1.0f;
    Vector4 warpPortalColor = { 1.0f, 0.5f, 0.1f, 0.9f };
    float warpPulseBase = 0.6f;
    float warpPulseAmplitude = 0.4f;
    float warpPulseSpeed = 4.0f;
    float warpLabelWorldY = 5.0f;
    Vector4 hpBarBackgroundColor = { 0.2f, 0.2f, 0.2f, 0.8f };
    Vector4 hpBarInitialColor = { 0.2f, 0.9f, 0.2f, 0.9f };
    float hpBarGreenScale = 0.85f;
    float hpBarGreenBase = 0.15f;
    float hpBarAlpha = 0.9f;
    float finisherShatterSeconds = 0.9f;
    float hitRingSpeed = 3.0f;
    Vector4 hitRingColor = { 1.0f, 0.85f, 0.2f, 1.0f };
    int hitRingCount = 12;
    float hitRingLifetime = 0.25f;
    float hitRingSize = 0.18f;
    float hitSparkSpreadX = 3.0f;
    float hitSparkRiseMin = 2.0f;
    float hitSparkRiseMax = 5.0f;
    int hitSparkCount = 6;
    Vector4 hitSparkColor = { 1.0f, 0.55f, 0.1f, 1.0f };
    float hitSparkLifetime = 0.6f;
    float hitSparkSize = 0.13f;
    float hintScale = 1.5f;
    Vector4 hintColor = { 0.80f, 0.76f, 0.65f, 1.0f };
    Vector4 hintShadowColor = { 0.05f, 0.04f, 0.02f, 0.9f };
    float hintShadowOffset = 1.6f;
    Vector4 returnLabelColor = { 1.0f, 0.6f, 0.1f, 1.0f };
    float lockMarkerHeight = 1.6f;
    float lockMarkerHalfWidth = 46.0f;
    float lockMarkerScale = 1.3f;
    Vector4 lockMarkerColor = { 1.0f, 0.35f, 0.2f, 1.0f };
    float hintLineHeight = 24.0f;
    Vector4 colliderLabelColor = { 1.0f, 0.5f, 0.1f, 1.0f };
};

/** @brief 敵の識別色とHPバーの調整値 */
struct InitialActorsVisualSettings {
    Vector4 spearEnemyColor = { 0.25f, 0.75f, 1.0f, 1.0f };
    Vector4 daggerEnemyColor = { 0.15f, 0.85f, 1.0f, 1.0f };
    Vector4 heavyEnemyColor = { 0.85f, 0.45f, 1.0f, 1.0f };
    Vector4 swordEnemyColor = { 1.0f, 0.3f, 0.15f, 1.0f };
    Vector4 hpBarBackgroundColor = { 0.2f, 0.2f, 0.2f, 0.8f };
};

/** @brief 演出JSONのシーン別設定を読み取り専用で共有する */
class SceneVisualTuning {
public:
    /** @brief 水場の色と環境粒子の設定を参照する */
    static const WaterVisualSettings& Water();
    /** @brief プレイヤーの輪郭と明滅の設定を参照する */
    static const PlayerVisualSettings& Player();
    /** @brief 本編のヒットとフィニッシャー演出の設定を参照する */
    static const GameplayVisualSettings& Gameplay();
    /** @brief 配置物の回復と取得演出の設定を参照する */
    static const LevelVisualSettings& Level();
    /** @brief 訓練戦闘の命中と射撃演出の設定を参照する */
    static const BattleCombatVisualSettings& BattleCombat();
    /** @brief 訓練フィニッシャーの開始と解放演出の設定を参照する */
    static const BattleFinisherVisualSettings& BattleFinisher();
    /** @brief 訓練場のポータルと画面演出の設定を参照する */
    static const TrainingVisualSettings& Training();
    /** @brief 訓練場の案内と命中演出の設定を参照する */
    static const BattleVisualSettings& Battle();
    /** @brief 敵の識別色とHPバーの設定を参照する */
    static const InitialActorsVisualSettings& InitialActors();
};

} // namespace engine::game
