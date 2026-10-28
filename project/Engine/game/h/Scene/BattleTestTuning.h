/**
 * @file BattleTestTuning.h
 * @brief バトルテストシーンのダミー・判定・加点の調整値を Resources/Config/battle_test.json から読む設定
 * @note 値を変えてもコードのリビルドが要らないようにする。項目を足す時は構造体と kBattleTestFields に1行ずつ足す
 */
#pragma once
#include "JsonFieldTable.h"
#include "JsonHelper.h"
#include "Logger.h"
#include <string>

namespace engine::game {

/** @brief バトルテストの調整値（初期値はJSONに項目が無い時の既定値） */
struct BattleTestTuning {
    float warpReturnX = 3.0f;
    float returnProximity = 3.0f;
    float dummyMaxHp = 100.0f;
    float dummySpawnX = 15.0f;
    float rampageDefaultTargetDistance = 6.0f;
    // ダミーの物理と帰還
    float dummyKnockDragX = 0.84f; ///< 水平ノックバック速度の毎フレーム減衰率
    float dummyKnockDragY = 0.88f; ///< 垂直ノックバック速度の毎フレーム減衰率
    float dummyHpRecoverTime = 0.8f; ///< 被弾後、HPバー表示が満タンに戻るまでの秒数
    float dummyReturnLerpRate = 0.05f; ///< 帰還タイマー経過後、定位置へ戻る補間率（毎フレーム）
    float dummyReturnDelay = 1.5f; ///< 被弾から定位置へ戻り始めるまでの秒数
    float dummyGravity = 0.012f;
    float dummyGroundY = 0.4f;
    float dummyMinX = 3.0f;
    float dummyMaxX = 35.0f;
    // 判定の広さ
    float lockAssistReachMult = 1.2f; ///< ロックオン中に前方リーチへ掛ける補正
    float lockAssistRearMult = 0.5f; ///< ロックオン中の背面リーチ（前方リーチ比）
    float slamRangeBelowY = 1.0f; ///< 設置型AoEの足元方向の厚み
    float slamRangeAboveY = 1.5f; ///< 設置型AoEの頭上方向の厚み
    float minMeleeReachRatio = 0.35f; ///< 踏み込みを差し引いた後も最低限残すリーチ（基準リーチ比）
    float gunShotRearReach = 0.8f; ///< 射撃判定の背面側の余裕（銃身ぶん）
    float bulletHalfExtent = 0.12f; ///< スピン連射弾の判定半径
    // 大剣の投げ回転斬りの渦
    float greatswordSpinSuctionRadius = 4.0f; ///< この半径内のダミーを渦へ引き寄せる
    float greatswordSpinHitRadius = 1.5f; ///< この半径内なら実際にヒットする
    float greatswordSpinSuctionSpeed = 0.1f; ///< 毎フレームの吸い込み速度
    // 覚醒乱舞
    float rampageRushKnockXFinisher = 0.45f; ///< 乱舞ラッシュ命中時の水平ノックバック（フィニッシュ段）
    float rampageRushKnockXNormal = 0.12f; ///< 乱舞ラッシュ命中時の水平ノックバック（通常段）
    float rampageRushKnockYFinisher = 0.18f; ///< 乱舞ラッシュ命中時の垂直ノックバック（フィニッシュ段）
    float rampageRushKnockYNormal = 0.03f; ///< 乱舞ラッシュ命中時の垂直ノックバック（通常段）
    int rampageRushHitStopFinisher = 6; ///< 乱舞ラッシュ命中時のヒットストップ（フィニッシュ段）
    int rampageRushHitStopNormal = 2; ///< 乱舞ラッシュ命中時のヒットストップ（通常段）
    float rampageRushStyleFinisher = 60.0f; ///< 乱舞ラッシュ命中時のスタイル加点（フィニッシュ段）
    float rampageRushStyleNormal = 20.0f; ///< 乱舞ラッシュ命中時のスタイル加点（通常段）
    // スピン連射弾
    float bulletKnockX = 0.09f;
    float bulletKnockY = 0.04f;
    int bulletHitStopFrames = 2;
    float bulletStyleScore = 3.0f;
    // 攻撃倍率・スタイル加点・覚醒ゲージ
    float awakenedAttackMult = 1.5f; ///< 覚醒中の攻撃倍率
    float meleeStyleScoreScale = 1.5f; ///< 近接ヒットのスタイル加点（与ダメージ比）
    float meleeAwakenGain = 0.08f;
    float gunAwakenGain = 0.04f;
    float bulletAwakenGain = 0.02f;
    // 被弾したダミーが光る秒数
    float gunHitFlash = 0.1f;
    float rampageFinisherHitFlash = 0.2f;
    float rampageHitFlash = 0.08f;
    float bulletHitFlash = 0.08f;
    float launcherHitFlash = 0.2f;
    float meleeHitFlash = 0.14f;
    // フィニッシャー
    float chargeWarpImpulse = 0.4f;
    float beatWarpImpulse = 0.12f;
    float beatHitFlash = 0.1f;
    float beatKnockX = 0.06f;
    float beatKnockY = 0.06f;
    float beatStyleScore = 6.0f;
    float releaseHitFlash = 0.22f;
    float releaseKnockX = 0.5f;
    float releaseKnockY = 0.2f;
    float releaseStyleScore = 120.0f;
    float rampageRushHalfWidth = 2.5f; ///< 乱舞ラッシュの判定半径（横）
    float rampageRushHalfHeight = 1.5f; ///< 乱舞ラッシュの判定半径（縦）
};

namespace battleTestTuningDetail {
    inline constexpr const char* kPath = "Resources/Config/battle_test.json";

    inline constexpr JsonField<BattleTestTuning> kBattleTestFields[] = {
        { "scene", "warpReturnX", &BattleTestTuning::warpReturnX },
        { "scene", "returnProximity", &BattleTestTuning::returnProximity },
        { "scene", "dummyMaxHp", &BattleTestTuning::dummyMaxHp },
        { "scene", "dummySpawnX", &BattleTestTuning::dummySpawnX },
        { "scene", "rampageDefaultTargetDistance", &BattleTestTuning::rampageDefaultTargetDistance },
        { "dummy", "dummyKnockDragX", &BattleTestTuning::dummyKnockDragX },
        { "dummy", "dummyKnockDragY", &BattleTestTuning::dummyKnockDragY },
        { "dummy", "dummyHpRecoverTime", &BattleTestTuning::dummyHpRecoverTime },
        { "dummy", "dummyReturnLerpRate", &BattleTestTuning::dummyReturnLerpRate },
        { "dummy", "dummyReturnDelay", &BattleTestTuning::dummyReturnDelay },
        { "dummy", "dummyGravity", &BattleTestTuning::dummyGravity },
        { "dummy", "dummyGroundY", &BattleTestTuning::dummyGroundY },
        { "dummy", "dummyMinX", &BattleTestTuning::dummyMinX },
        { "dummy", "dummyMaxX", &BattleTestTuning::dummyMaxX },
        { "reach", "lockAssistReachMult", &BattleTestTuning::lockAssistReachMult },
        { "reach", "lockAssistRearMult", &BattleTestTuning::lockAssistRearMult },
        { "reach", "slamRangeBelowY", &BattleTestTuning::slamRangeBelowY },
        { "reach", "slamRangeAboveY", &BattleTestTuning::slamRangeAboveY },
        { "reach", "minMeleeReachRatio", &BattleTestTuning::minMeleeReachRatio },
        { "reach", "gunShotRearReach", &BattleTestTuning::gunShotRearReach },
        { "reach", "bulletHalfExtent", &BattleTestTuning::bulletHalfExtent },
        { "greatswordSpin", "greatswordSpinSuctionRadius", &BattleTestTuning::greatswordSpinSuctionRadius },
        { "greatswordSpin", "greatswordSpinHitRadius", &BattleTestTuning::greatswordSpinHitRadius },
        { "greatswordSpin", "greatswordSpinSuctionSpeed", &BattleTestTuning::greatswordSpinSuctionSpeed },
        { "rampage", "rampageRushKnockXFinisher", &BattleTestTuning::rampageRushKnockXFinisher },
        { "rampage", "rampageRushKnockXNormal", &BattleTestTuning::rampageRushKnockXNormal },
        { "rampage", "rampageRushKnockYFinisher", &BattleTestTuning::rampageRushKnockYFinisher },
        { "rampage", "rampageRushKnockYNormal", &BattleTestTuning::rampageRushKnockYNormal },
        { "rampage", "rampageRushHitStopFinisher", &BattleTestTuning::rampageRushHitStopFinisher },
        { "rampage", "rampageRushHitStopNormal", &BattleTestTuning::rampageRushHitStopNormal },
        { "rampage", "rampageRushStyleFinisher", &BattleTestTuning::rampageRushStyleFinisher },
        { "rampage", "rampageRushStyleNormal", &BattleTestTuning::rampageRushStyleNormal },
        { "bullet", "bulletKnockX", &BattleTestTuning::bulletKnockX },
        { "bullet", "bulletKnockY", &BattleTestTuning::bulletKnockY },
        { "bullet", "bulletHitStopFrames", &BattleTestTuning::bulletHitStopFrames },
        { "bullet", "bulletStyleScore", &BattleTestTuning::bulletStyleScore },
        { "score", "awakenedAttackMult", &BattleTestTuning::awakenedAttackMult },
        { "score", "meleeStyleScoreScale", &BattleTestTuning::meleeStyleScoreScale },
        { "score", "meleeAwakenGain", &BattleTestTuning::meleeAwakenGain },
        { "score", "gunAwakenGain", &BattleTestTuning::gunAwakenGain },
        { "score", "bulletAwakenGain", &BattleTestTuning::bulletAwakenGain },
        { "hitFlash", "gunHitFlash", &BattleTestTuning::gunHitFlash },
        { "hitFlash", "rampageFinisherHitFlash", &BattleTestTuning::rampageFinisherHitFlash },
        { "hitFlash", "rampageHitFlash", &BattleTestTuning::rampageHitFlash },
        { "hitFlash", "bulletHitFlash", &BattleTestTuning::bulletHitFlash },
        { "hitFlash", "launcherHitFlash", &BattleTestTuning::launcherHitFlash },
        { "hitFlash", "meleeHitFlash", &BattleTestTuning::meleeHitFlash },
        { "finisher", "chargeWarpImpulse", &BattleTestTuning::chargeWarpImpulse },
        { "finisher", "beatWarpImpulse", &BattleTestTuning::beatWarpImpulse },
        { "finisher", "beatHitFlash", &BattleTestTuning::beatHitFlash },
        { "finisher", "beatKnockX", &BattleTestTuning::beatKnockX },
        { "finisher", "beatKnockY", &BattleTestTuning::beatKnockY },
        { "finisher", "beatStyleScore", &BattleTestTuning::beatStyleScore },
        { "finisher", "releaseHitFlash", &BattleTestTuning::releaseHitFlash },
        { "finisher", "releaseKnockX", &BattleTestTuning::releaseKnockX },
        { "finisher", "releaseKnockY", &BattleTestTuning::releaseKnockY },
        { "finisher", "releaseStyleScore", &BattleTestTuning::releaseStyleScore },
        { "reach", "rampageRushHalfWidth", &BattleTestTuning::rampageRushHalfWidth },
        { "reach", "rampageRushHalfHeight", &BattleTestTuning::rampageRushHalfHeight },
    };

    /** @brief JSONファイルからグラフを読み込む（読み込み失敗時は空のGraphDescを返す） */
    inline BattleTestTuning Load() {
        BattleTestTuning tuning;
        const nlohmann::json root = engine::JsonHelper::Load(kPath);
        if (!root.is_object()) {
            engine::Logger::LogWarning(std::string("BattleTestTuning: ") + kPath + " が読めません。既定値で動きます");
            return tuning;
        }
        ReadJsonFields(root, kBattleTestFields, tuning);
        return tuning;
    }
} // namespace battleTestTuningDetail

/** @brief 読み込み済みのバトルテスト調整値（初回にJSONを読む） */
inline const BattleTestTuning& BattleTestTune() {
    static const BattleTestTuning kTuning = battleTestTuningDetail::Load();
    return kTuning;
}

} // namespace engine::game
