/**
 * @file FinalBossTuning.h
 * @brief ラスボス戦（FinalBossBattle）の調整値と、Resources/Config/final_boss.json の読み込み
 */
#pragma once
#include "Vector3.h"
#include "Vector4.h"
#include <string>

namespace engine::game {

/**
 * @brief ラスボス戦の調整値（final_boss.json。初期値はJSONに項目が無い時の既定値）
 * @note 秒・距離・速さはすべてシーンの座標と実時間。距離の基準はプレイヤーの体の大きさ（約1）
 */
struct FinalBossTuning {
    // ── 舞台 ──
    std::string level = "Resources/Levels/final_arena.json"; ///< ラスボス戦で読み込むレベル
    std::string bossName = "黒き刃の影"; ///< 名乗りと体力バーに出す名前
    std::string introLine = "……その刀、返してもらう"; ///< 名乗りの後の台詞
    std::string rageLine = "まだだ……！"; ///< 後半戦に入る時の台詞
    // 戦闘の前に出す操作説明（{操作名} は今の機器の表記になる。guide は改行で行を分ける）
    std::string guideTitle = "闘技場の操作";
    std::string guide = "{ArenaMove}  移動（左右でボスの周りを回り込む）\n{ArenaJump}  ジャンプ\n{Attack}  攻撃（{ArenaBack}+{Attack} で打ち上げ）\n{Shoot}  射撃\n{Skill}  刀の固有技\n{Dodge}  回避（攻撃に合わせるとジャスト回避）\n{Awaken}  覚醒\n{Finisher}  フィニッシャー\n{Pause}  ポーズ・音量設定";
    std::string guideConfirm = "{Attack} / 決定キー で戦闘開始";
    float guideMinSeconds = 0.6f; ///< 操作説明を出してから決定を受け付けるまで（読まずに飛ばさないように）
    float guideFadeSeconds = 0.3f; ///< 操作説明が出入りする長さ
    std::string stealLine = "落とした刀に近づいて J で奪え"; ///< 撃破した後に出す案内
    int bossMaxHp = 90; ///< ボスの体力
    float rageHpRatio = 0.5f; ///< 体力がこの割合を切ったら後半戦
    float startDistance = 8.0f; ///< 戦闘開始時のプレイヤーとボスの距離
    float edgeMargin = 1.2f; ///< 闘技場の縁から内側へ空ける幅（ここより外へは出られない）
    float shadowBaseY = 0.4f; ///< 分身の体の中心の高さ（プレイヤーの地面の高さと同じ）
    Vector4 shadowColor = { 0.06f, 0.05f, 0.09f, 1.0f }; ///< 分身の体の色
    Vector3 shadowRimColor = { 0.55f, 0.3f, 1.0f }; ///< 分身の輪郭の光
    Vector3 shadowRimColorRage = { 1.0f, 0.2f, 0.15f }; ///< 後半戦の輪郭の光
    Vector4 shadowFlashColor = { 0.9f, 0.85f, 1.0f, 1.0f }; ///< 殴られた瞬間に寄せる色
    Vector3 shadowKatanaGripRotate = { 3.14159265f, 0.0f, 0.0f }; ///< 分身の刀の握りに足す回転（ラジアン。既定は X 軸で半回転して上下を返す）

    // ── 移動とカメラ ──
    float strafeSpeed = 5.5f; ///< 横へ回り込む速さ（毎秒）
    float minOrbitRadius = 1.5f; ///< 回り込みの角度を求める時の最小の半径（近すぎて一気に回らないように）
    float turnRate = 14.0f; ///< 体の向きを進む方へ寄せる速さ（毎秒の割合）
    float cameraBack = 7.0f; ///< プレイヤーの後ろへ下がる距離
    float cameraHeight = 3.0f; ///< プレイヤーより上の高さ
    float cameraSide = 1.6f; ///< 右へずらす量（プレイヤーとボスが重ならないように）
    float cameraLookAhead = 0.4f; ///< 注視点をボスへ寄せる割合
    float cameraLookHeight = 0.9f; ///< 注視点の高さ
    float cameraTurnRate = 5.0f; ///< カメラが向きを合わせる速さ（毎秒）
    float cameraFollowRate = 9.0f; ///< カメラの位置が追いつく速さ（毎秒）

    // ── 武器が集まって刀になる ──
    float gatherSeconds = 2.4f; ///< 武器がプレイヤーの周りを回って頭上へ集まる長さ
    float gatherRadius = 1.8f; ///< 回り始める半径
    float gatherHeight = 2.8f; ///< 集まる頭上の高さ（体の中心から）
    float gatherSpin = 9.0f; ///< 回る速さ（毎秒のラジアン）
    float gatherScale = 0.7f; ///< 浮かべる武器の大きさ
    float forgeSeconds = 0.7f; ///< 光って刀になるまでの長さ
    float descendSeconds = 0.7f; ///< 刀が手元へ降りる長さ
    float veilSeconds = 0.8f; ///< 白い幕が引くまでの長さ
    Vector4 forgeFlashColor = { 1.0f, 0.96f, 0.85f, 0.9f }; ///< 刀になる瞬間の閃光

    // ── 名乗り ──
    float introSeconds = 4.2f; ///< 名乗りの長さ
    float introDrawTime = 1.6f; ///< 居合の構えから抜き放つ時刻
    float introOrbitRadius = 4.0f; ///< ボスの周りを回るカメラの半径
    float introOrbitSpeed = 0.35f; ///< 回る速さ（毎秒のラジアン）
    float introCameraHeight = 1.4f; ///< 回るカメラの高さ（ボスの体の中心から）
    float letterboxSeconds = 0.4f; ///< 黒帯が出入りする長さ

    // ── 後半戦の演出 ──
    float rageSeconds = 2.2f; ///< 後半戦に入る演出の長さ
    float rageSpeedMult = 1.35f; ///< 後半戦の動きの速さの倍率
    float rageThinkMult = 0.6f; ///< 後半戦の考える時間の倍率

    // ── 考える時間と怯み ──
    float thinkMinSeconds = 0.45f; ///< 技と技の間の最短
    float thinkMaxSeconds = 1.0f; ///< 技と技の間の最長
    float staggerWindow = 1.6f; ///< ダメージを数える長さ
    int staggerDamage = 9; ///< この時間内にこれだけ受けたら怯む
    float staggerSeconds = 1.1f; ///< 怯んでいる長さ
    float staggerLean = 0.45f; ///< 怯みでのけぞる角度
    float hitHalfHeight = 1.1f; ///< ボスの攻撃判定の上下の半分

    // ── 攻撃の予兆（床の印） ──
    Vector4 telegraphColor = { 1.0f, 0.15f, 0.2f, 0.55f }; ///< 当たる範囲の印の色
    float telegraphPulseSpeed = 14.0f; ///< 印の明滅の速さ
    float telegraphPulseDepth = 0.35f; ///< 明滅で薄くなる割合
    float telegraphLineWidth = 1.0f; ///< 突き抜け斬りの通り道の帯の太さ
    float telegraphAimRadius = 0.9f; ///< 撃ち出す剣の狙いの印の半径

    // ── 間合いを測る ──
    float measureSeconds = 1.3f;
    float measureOrbitSpeed = 0.7f; ///< プレイヤーの周りを回る速さ（毎秒のラジアン）
    float measureDistance = 4.5f; ///< 保とうとする距離
    float walkSpeed = 2.4f; ///< 歩く速さ

    // ── 走って詰めて連撃 ──
    float runSpeed = 10.0f;
    float runTimeout = 1.4f; ///< 走って届かなければ諦める長さ
    float rushStopDistance = 1.7f; ///< ここまで詰めたら斬り始める
    int rushSlashes = 3; ///< 連撃の数（後半戦は +1）
    float slashSeconds = 0.36f; ///< 1振りの長さ
    float slashHitRatio = 0.45f; ///< 1振りのうち当たる時刻の割合
    float slashReach = 2.3f; ///< 前への届く距離
    float slashLunge = 0.5f; ///< 1振りごとの踏み込み
    int slashDamage = 3;
    float leapSeconds = 0.75f; ///< 締めの跳び斬りの長さ
    float leapHeight = 3.0f;
    float leapReach = 2.8f; ///< 着地の斬りの届く範囲（前後）
    int leapDamage = 5;

    // ── 突き抜け斬り ──
    float iaiWindup = 0.75f; ///< 構えの長さ（光って知らせる）
    float iaiDashSeconds = 0.16f;
    float iaiDashPast = 3.5f; ///< プレイヤーを通り過ぎる距離
    float iaiCutDelay = 0.4f; ///< 通り抜けてから斬線が走るまで
    float iaiRecover = 0.6f;
    int iaiDamage = 6;

    // ── 撃ち出す剣 ──
    int phantomCount = 5; ///< 背後に並べる剣の数（後半戦は +3）
    float phantomSummonSeconds = 0.7f; ///< 並べ終えるまで
    float phantomInterval = 0.22f; ///< 1本ずつ撃つ間隔
    float phantomSpeed = 20.0f;
    float phantomSpread = 2.2f; ///< 背後に並べる横幅
    float phantomHeight = 2.4f; ///< 並べる高さ（体の中心から）
    float phantomHalfSize = 0.35f; ///< 剣1本の当たり判定の半分
    float phantomLifetime = 1.6f;
    float phantomScale = 0.55f; ///< 剣の見た目の大きさ
    int phantomDamage = 2;

    // ── 範囲斬り ──
    float spaceTelegraph = 0.95f; ///< 予兆の長さ
    float spaceRadius = 2.4f; ///< 斬る範囲の半径
    float spaceSpacing = 3.2f; ///< 後半戦で3つ出す時の間隔
    int spaceDamage = 6;
    float spaceRecover = 0.7f;

    // ── 瞬身 ──
    float blinkSeconds = 0.28f; ///< 消えてから出るまで
    float blinkBehindDistance = 2.2f; ///< 背後へ回る時の距離
    float blinkAwayDistance = 7.0f; ///< 離れる時の距離

    // ── 撃破 ──
    float defeatSlowSeconds = 1.6f;
    float defeatSlowScale = 0.25f;
    float defeatLieAngle = 1.35f; ///< 後ろへ倒れる角度
    float defeatLieLift = -0.35f; ///< 倒れた体を下げる量
    float defeatFallSeconds = 0.9f; ///< 倒れ切るまで
    float defeatCinematicSeconds = 2.4f; ///< 操作を返すまで
};

/** @brief ラスボス戦の調整値の置き場（初回に読み、Reload で読み直す） */
class FinalBossTuningStore {
public:
    /** @brief 読み込み済みの調整値を返す */
    static const FinalBossTuning& Get();
    /** @brief final_boss.json を読み直す（調整値エディタで保存した時） */
    static void Reload();

private:
    /** @brief 調整値の実体 */
    static FinalBossTuning& Mutable();
};

} // namespace engine::game
