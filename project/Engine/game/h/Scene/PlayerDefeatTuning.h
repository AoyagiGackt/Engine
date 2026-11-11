/**
 * @file PlayerDefeatTuning.h
 * @brief やられ演出（PlayerDefeat）の調整値と、Resources/Config/player_defeat.json の読み込み
 */
#pragma once
#include "Vector2.h"
#include "Vector3.h"
#include "Vector4.h"
#include <string>

namespace engine::game {

/** @brief やられ演出の調整値（player_defeat.json。初期値はJSONに項目が無い時の既定値） */
struct DefeatTuning {
    // カメラ
    float closeZoom = 0.55f; ///< 寄った時の引き（ふだんの距離に対する割合）
    float zoomSeconds = 0.45f; ///< やられてから寄り切るまでの長さ
    float closeHeight = 0.8f; ///< 寄り切った時に、カメラを追う位置よりどれだけ上に置くか（ふだんは gameConstants::kCameraFollowOffsetY）
    // 被弾で固まる
    float hitSeconds = 0.35f; ///< 被弾した姿勢のまま震えて止まる長さ
    float hitShakeWidth = 0.08f; ///< 震えの左右の幅
    float hitShakeSpeed = 90.0f; ///< 震えの速さ（ラジアン毎秒）
    std::string hitImpact = "player_defeat_hit"; ///< 被弾の瞬間のカメラ揺れ（hit_effects.json の impacts）
    std::string hitFlash = "player_defeat_hit"; ///< 被弾の瞬間の画面フラッシュ（flashes）
    std::string hitSound; ///< 被弾の瞬間に鳴らす声や効果音（空なら鳴らさない）
    float hitSoundVolume = 1.0f;
    // 吹き飛ぶ
    float launchSeconds = 0.75f; ///< 吹き飛んで床に落ちるまでの長さ
    float launchDistance = 3.2f; ///< 後ろへ吹き飛ぶ横の距離
    float launchHeight = 2.4f; ///< 吹き飛ぶ弧の高さ
    float stageEdgeMargin = 1.0f; ///< 吹き飛ぶ先をステージの端からこれだけ内側に収める
    float wallClearance = 0.9f; ///< 吹き飛ぶ先を壁の面からこれだけ離す（倒れた体の半分の長さ）
    float wallBandBottom = 0.1f; ///< やられた位置からこれより高い所に掛かるブロックを壁とみなす（低い段差は床として越える）
    float spinTurns = 1.0f; ///< 宙で後ろへ回る回数（最後に仰向けになる分は別に足す）
    // 手からすっぽ抜ける武器
    float weaponHandHeight = 0.3f; ///< 武器が手から離れる高さ（プレイヤーの中心から上へ）
    float weaponDistance = 1.8f; ///< 武器が飛ぶ横の距離（プレイヤーが吹き飛ぶのと逆、攻撃してきた側へ）
    float weaponHeight = 2.8f; ///< 武器が飛ぶ弧の高さ
    float weaponSeconds = 0.8f; ///< 武器が床に落ちるまでの長さ
    float weaponSpinTurns = 2.5f; ///< 武器が飛びながら回る回数
    float weaponGroundOffsetY = -0.35f; ///< 床に落ちた武器の高さ（プレイヤーが床に立った時の中心の高さから）
    float weaponLieAngleDegrees = 90.0f; ///< 床に落ちた武器の傾き（Z軸回り）
    std::string weaponLandSparks = "land_dust"; ///< 武器が床に落ちた時の砂ぼこり（sparks）
    // 敵の上で跳ねる
    float enemyClearance = 1.3f; ///< 倒れ込む位置と敵の中心の横の距離がこれより近いと、体が敵に埋まるとみなす
    float enemyTopOffset = 1.0f; ///< 敵の上で跳ねる時の高さ（敵の中心から上へ）。高さの差がこれ以上の敵は避けない
    float bounceExtraDistance = 0.6f; ///< 跳ねた先を、埋まらない距離からさらにこれだけ離す
    float enemyBounceSeconds = 0.45f; ///< 敵の上で跳ねてから床に落ちるまでの長さ
    float enemyBounceHeight = 1.4f; ///< 敵の上で跳ねる弧の高さ
    float enemyBounceSpinTurns = 1.0f; ///< 跳ねている間に後ろへ回る回数
    std::string enemyBounceImpact = "player_defeat_bounce"; ///< 敵の上で跳ねた時のカメラ揺れ（impacts）
    std::string enemyBounceSparks = "contact"; ///< 敵の上で跳ねた時の火花（sparks）
    // 倒れ込む
    float lieAngleDegrees = -90.0f; ///< 倒れた時ののけぞり（負で仰向け）
    float lieLift = 0.25f; ///< 倒れた体が床へめり込まないよう持ち上げる高さ
    float bounceHeight = 0.45f; ///< 床に当たって跳ねる高さ
    float bounceSeconds = 0.28f;
    float downSeconds = 0.7f; ///< 倒れたまま動かない長さ（跳ね返りも含む）
    std::string downImpact = "player_defeat_down"; ///< 床に叩きつけられた時のカメラ揺れ（impacts）
    std::string downRing = "intro_land"; ///< 床に叩きつけられた時の衝撃リング（rings）
    std::string downSparks = "land_dust"; ///< 床に叩きつけられた時の砂ぼこり（sparks）
    // あがいて力尽きる
    float struggleRiseSeconds = 0.5f; ///< 上半身を起こそうとするまでの長さ
    float struggleRiseDegrees = 35.0f; ///< 上半身を起こす角度（倒れた姿勢から）
    float struggleHoldSeconds = 0.6f; ///< 起こしかけたまま震えてあがく長さ
    float struggleShakeDegrees = 3.0f; ///< あがく間の震えの角度
    float struggleShakeSpeed = 45.0f; ///< 震えの速さ（ラジアン毎秒）
    std::string reachArm = "UpperArm.R"; ///< 空へ伸ばす腕の付け根のボーン
    std::string reachForearm = "LowerArm.R"; ///< 空へ伸ばす腕の肘から先のボーン（肘を伸ばす）
    Vector3 reachDirection = { 0.0f, 0.35f, 1.0f }; ///< 腕を伸ばす向き（モデルの座標。+Zが体の前で、仰向けでは空になる）
    float reachWeight = 0.85f; ///< 腕を目標の向きへ寄せる割合（1で真っすぐその向き）
    float reachForearmWeight = 0.9f; ///< 肘から先を目標の向きへ寄せる割合
    float reachShake = 0.15f; ///< 伸ばした腕の震えの大きさ（向きに足す横と縦のぶれ）
    float reachShakeSpeed = 23.0f; ///< 腕の震えの速さ（ラジアン毎秒）
    float collapseSeconds = 0.18f; ///< 力尽きて倒れ込むまでの長さ
    float afterCollapseSeconds = 0.5f; ///< 倒れてから GAME OVER を出すまでの長さ
    std::string collapseImpact = "player_defeat_collapse"; ///< 力尽きて倒れた時のカメラ揺れ（impacts）
    std::string collapseSparks = "land_dust"; ///< 力尽きて倒れた時の砂ぼこり（sparks）
    // GAME OVER
    float fadeSeconds = 0.6f; ///< 暗転が濃くなり切るまでの長さ
    float textDropSeconds = 0.7f; ///< 文字が上から落ちて弾み終えるまでの長さ
    float textDropHeight = 220.0f; ///< 文字が落ちてくる高さ（画面の座標）
    float holdSeconds = 1.4f; ///< 文字が落ち着いてからゲームオーバー画面へ移るまでの長さ
    Vector4 overlayColor = { 0.12f, 0.0f, 0.0f, 0.65f };
    std::string text = "GAME OVER";
    float skipLockSeconds = 0.4f; ///< GAME OVER が出始めてからこの間はボタンで飛ばせない（連打していた攻撃を拾わないように）
    // 選択肢
    std::string retryLabel = "もう一度挑戦"; ///< 同じステージをやり直す項目の文字
    std::string mapLabel = "マップに戻る"; ///< マップへ戻る項目の文字
    float choiceLockSeconds = 0.3f; ///< 選択肢が出てからこの間は決定を受け付けない（連打していたジャンプで決まらないように）
};

/** @brief やられ演出の調整値の置き場（初回に読み、Reload で読み直す） */
class DefeatTuningStore {
public:
    /** @brief 読み込み済みの調整値を返す */
    static const DefeatTuning& Get();
    /** @brief player_defeat.json を読み直す（調整値エディタで保存した時） */
    static void Reload();

private:
    /** @brief 調整値の実体 */
    static DefeatTuning& Mutable();
};

} // namespace engine::game
