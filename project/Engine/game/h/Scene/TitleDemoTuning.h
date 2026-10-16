/**
 * @file TitleDemoTuning.h
 * @brief タイトル背景デモ（自動で戦うプレイヤーと的）の調整値を Resources/Config/title_demo.json から読む設定
 * @note 項目を足す時は構造体と kTitleDemoFields に1行ずつ足す。命中演出の見た目は hit_effects.json の title_ で始まるレシピ
 */
#pragma once
#include "JsonFieldTable.h"
#include "JsonHelper.h"
#include "Logger.h"
#include "Vector4.h"
#include <string>

namespace engine::game {

/** @brief タイトルデモの調整値（初期値はJSONに項目が無い時の既定値） */
struct TitleDemoTuning {
    // 舞台とカメラ
    float stageMinX = 0.0f; ///< 的が歩ける範囲の左端
    float stageMaxX = 44.0f; ///< 的が歩ける範囲の右端
    float arenaMinX = 12.0f; ///< プレイヤーと復帰した的が収まる範囲の左端
    float arenaMaxX = 32.0f; ///< プレイヤーと復帰した的が収まる範囲の右端
    float arenaEdgeMargin = 0.5f; ///< 範囲の端から内側へ収める余白
    float groundY = 0.4f; ///< プレイヤー・的の立ち位置Y
    float startX = 22.0f; ///< プレイヤーの開始位置X
    float cameraX = 22.0f;
    float cameraY = 4.8f;
    float effectZ = -1.0f; ///< 命中演出を出す奥行き（キャラより手前）

    // 的の見た目
    float dummyScale = 0.55f;
    float dummyFootOffsetY = -0.65f; ///< モデルの足元を地面に合わせるずらし量
    float dummyAirborneHeight = 2.4f; ///< 空中の的を浮かべる高さ
    float dummyBobAmplitude = 0.32f; ///< 空中の的の上下の揺れ幅
    float dummyHitFlashSeconds = 0.12f;
    float dummyFlashShrink = 0.9f; ///< 被弾で光っている間の縮み
    Vector4 dummyFlashColor = { 1.6f, 1.6f, 1.6f, 1.0f };
    float dummyDefeatSinkDepth = 0.8f; ///< 倒した的を地面へ沈める深さ

    // 初期配置の的ごとのばらつき（並び順に比例して変える）
    float dummyBobPhaseStep = 1.73f;
    float dummyBobSpeedBase = 1.15f;
    float dummyBobSpeedStep = 0.19f;
    float dummyMoveSpeedBase = 0.75f;
    float dummyMoveSpeedStep = 0.13f;
    float dummyAttackCooldownBase = 0.35f;
    float dummyAttackCooldownStep = 0.27f;

    // 倒された的の復帰
    float respawnDistanceMin = 7.0f; ///< プレイヤーからこの距離の範囲に出す
    float respawnDistanceMax = 12.0f;
    float respawnDelayMin = 0.9f;
    float respawnDelayMax = 2.2f;
    float respawnMoveSpeedMin = 0.65f;
    float respawnMoveSpeedMax = 1.35f;
    float respawnAttackDelayMin = 0.2f;
    float respawnAttackDelayMax = 1.1f;
    float respawnRightChance = 0.5f; ///< プレイヤーの右側に出る確率

    // 的の接近と攻撃
    float dummyPreferredDistanceBase = 1.05f; ///< プレイヤーの手前で止まる距離
    int dummyPreferredDistanceVariants = 3; ///< 止まる距離を何通りに散らすか
    float dummyPreferredDistanceStep = 0.38f;
    float dummyAttackAnimSeconds = 0.34f;
    float dummyNextAttackMin = 0.75f;
    float dummyNextAttackMax = 1.65f;
    float dummyLungeDistance = 0.7f; ///< 攻撃で踏み込む距離

    // デモのプレイヤーの動き
    float approachStopDistance = 1.6f; ///< この距離まで近づいたら攻撃を始める
    float attackPulseInterval = 0.32f; ///< 攻撃ボタンを押す間隔
    float hitHeightTolerance = 0.7f; ///< 地上の的と高さがこの差以内なら攻撃する
    float backstepSeconds = 0.18f; ///< 連打の合間に一歩引く長さ
    float incomingStrikeWindowStart = 0.10f; ///< 的の攻撃アニメ残り時間がこの範囲なら回避する
    float incomingStrikeWindowEnd = 0.22f;
    float incomingStrikeRange = 2.4f;
    float enemyDodgeCooldown = 0.8f;
    float moveTrickDelay = 0.45f; ///< 移動を始めてからジャンプ/回避を混ぜるまでの時間
    float jumpToAirborneRange = 4.5f;
    float dodgeApproachMinDistance = 3.0f;

    // 命中演出と武器の軌跡
    float hitEffectAlpha = 0.9f;
    float idleTrailScaleRatio = 0.48f; ///< 攻撃していない時の軌跡の縮小率
    float idleTrailAlpha = 0.42f;
    int daggerCutCount = 3; ///< 短剣の命中で重ねる斬線の数
    float daggerCutSpacing = 0.28f;
};

namespace title_demo_tuning_detail {
    inline constexpr const char* kPath = "Resources/Config/title_demo.json";

    inline constexpr JsonField<TitleDemoTuning> kTitleDemoFields[] = {
        { "stage", "stageMinX", &TitleDemoTuning::stageMinX },
        { "stage", "stageMaxX", &TitleDemoTuning::stageMaxX },
        { "stage", "arenaMinX", &TitleDemoTuning::arenaMinX },
        { "stage", "arenaMaxX", &TitleDemoTuning::arenaMaxX },
        { "stage", "arenaEdgeMargin", &TitleDemoTuning::arenaEdgeMargin },
        { "stage", "groundY", &TitleDemoTuning::groundY },
        { "stage", "startX", &TitleDemoTuning::startX },
        { "stage", "cameraX", &TitleDemoTuning::cameraX },
        { "stage", "cameraY", &TitleDemoTuning::cameraY },
        { "stage", "effectZ", &TitleDemoTuning::effectZ },
        { "dummyLook", "scale", &TitleDemoTuning::dummyScale },
        { "dummyLook", "footOffsetY", &TitleDemoTuning::dummyFootOffsetY },
        { "dummyLook", "airborneHeight", &TitleDemoTuning::dummyAirborneHeight },
        { "dummyLook", "bobAmplitude", &TitleDemoTuning::dummyBobAmplitude },
        { "dummyLook", "hitFlashSeconds", &TitleDemoTuning::dummyHitFlashSeconds },
        { "dummyLook", "flashShrink", &TitleDemoTuning::dummyFlashShrink },
        { "dummyLook", "flashColor", &TitleDemoTuning::dummyFlashColor },
        { "dummyLook", "defeatSinkDepth", &TitleDemoTuning::dummyDefeatSinkDepth },
        { "dummyVariation", "bobPhaseStep", &TitleDemoTuning::dummyBobPhaseStep },
        { "dummyVariation", "bobSpeedBase", &TitleDemoTuning::dummyBobSpeedBase },
        { "dummyVariation", "bobSpeedStep", &TitleDemoTuning::dummyBobSpeedStep },
        { "dummyVariation", "moveSpeedBase", &TitleDemoTuning::dummyMoveSpeedBase },
        { "dummyVariation", "moveSpeedStep", &TitleDemoTuning::dummyMoveSpeedStep },
        { "dummyVariation", "attackCooldownBase", &TitleDemoTuning::dummyAttackCooldownBase },
        { "dummyVariation", "attackCooldownStep", &TitleDemoTuning::dummyAttackCooldownStep },
        { "respawn", "distanceMin", &TitleDemoTuning::respawnDistanceMin },
        { "respawn", "distanceMax", &TitleDemoTuning::respawnDistanceMax },
        { "respawn", "delayMin", &TitleDemoTuning::respawnDelayMin },
        { "respawn", "delayMax", &TitleDemoTuning::respawnDelayMax },
        { "respawn", "moveSpeedMin", &TitleDemoTuning::respawnMoveSpeedMin },
        { "respawn", "moveSpeedMax", &TitleDemoTuning::respawnMoveSpeedMax },
        { "respawn", "attackDelayMin", &TitleDemoTuning::respawnAttackDelayMin },
        { "respawn", "attackDelayMax", &TitleDemoTuning::respawnAttackDelayMax },
        { "respawn", "rightChance", &TitleDemoTuning::respawnRightChance },
        { "dummyAttack", "preferredDistanceBase", &TitleDemoTuning::dummyPreferredDistanceBase },
        { "dummyAttack", "preferredDistanceVariants", &TitleDemoTuning::dummyPreferredDistanceVariants },
        { "dummyAttack", "preferredDistanceStep", &TitleDemoTuning::dummyPreferredDistanceStep },
        { "dummyAttack", "attackAnimSeconds", &TitleDemoTuning::dummyAttackAnimSeconds },
        { "dummyAttack", "nextAttackMin", &TitleDemoTuning::dummyNextAttackMin },
        { "dummyAttack", "nextAttackMax", &TitleDemoTuning::dummyNextAttackMax },
        { "dummyAttack", "lungeDistance", &TitleDemoTuning::dummyLungeDistance },
        { "player", "approachStopDistance", &TitleDemoTuning::approachStopDistance },
        { "player", "attackPulseInterval", &TitleDemoTuning::attackPulseInterval },
        { "player", "hitHeightTolerance", &TitleDemoTuning::hitHeightTolerance },
        { "player", "backstepSeconds", &TitleDemoTuning::backstepSeconds },
        { "player", "incomingStrikeWindowStart", &TitleDemoTuning::incomingStrikeWindowStart },
        { "player", "incomingStrikeWindowEnd", &TitleDemoTuning::incomingStrikeWindowEnd },
        { "player", "incomingStrikeRange", &TitleDemoTuning::incomingStrikeRange },
        { "player", "enemyDodgeCooldown", &TitleDemoTuning::enemyDodgeCooldown },
        { "player", "moveTrickDelay", &TitleDemoTuning::moveTrickDelay },
        { "player", "jumpToAirborneRange", &TitleDemoTuning::jumpToAirborneRange },
        { "player", "dodgeApproachMinDistance", &TitleDemoTuning::dodgeApproachMinDistance },
        { "effect", "hitEffectAlpha", &TitleDemoTuning::hitEffectAlpha },
        { "effect", "idleTrailScaleRatio", &TitleDemoTuning::idleTrailScaleRatio },
        { "effect", "idleTrailAlpha", &TitleDemoTuning::idleTrailAlpha },
        { "effect", "daggerCutCount", &TitleDemoTuning::daggerCutCount },
        { "effect", "daggerCutSpacing", &TitleDemoTuning::daggerCutSpacing },
    };

    inline TitleDemoTuning Load()
    {
        TitleDemoTuning tuning;
        const nlohmann::json root = engine::JsonHelper::Load(kPath);
        if (!root.is_object()) {
            engine::Logger::LogWarning(std::string("TitleDemoTuning: ") + kPath + " が読めません。既定値で動きます");
            return tuning;
        }
        ReadJsonFields(root, kTitleDemoFields, tuning);
        return tuning;
    }
} // namespace title_demo_tuning_detail

/** @brief 読み込み済みのタイトルデモ調整値（初回にJSONを読む） */
inline const TitleDemoTuning& TitleDemoTune()
{
    static const TitleDemoTuning tuning = title_demo_tuning_detail::Load();
    return tuning;
}

} // namespace engine::game
