/**
 * @file GunCombo.h
 * @brief 銃種別の射撃コンボ定義と、段送り・先行入力・構え/リコイル進行を管理するコントローラ
 */
#pragma once
#include "ComboTable.h"
#include "Vector3.h"
namespace engine::game {

enum class GunType; // Weapon.h で定義

/**
 * @brief 射撃コンボ1段ぶんの定義
 * @note damage は RangedWeaponData.damage に damageMult を掛けて使う（銃の個性は基礎値、段の個性は倍率）
 */
struct GunShotDef {
    const char* id; ///< スタイル評価用の技ID（同じ技の連発を検出するキー）
    float damageMult; ///< 基礎ダメージ倍率
    float duration; ///< モーション全体の長さ（秒）
    float shotTime; ///< 発砲するタイミング（秒）
    float cancelTime; ///< 次段への先行入力を発動できる時刻（秒、shotTime 以上にする）
    int bullets; ///< 1トリガーで発射する弾数（視覚弾・エフェクトの数にも使う）
    float spreadDeg; ///< 弾の拡散角（度、bullets が複数のとき扇状にばらまく）
    float rangeMult; ///< RangedWeaponData.range に掛ける射程倍率
    float knockX; ///< 水平ノックバック初速
    float knockY; ///< 垂直ノックバック初速（launcher 段はここが大きい）
    bool launcher; ///< 打ち上げ段か（ヒット時に敵を浮かせる）
    int hitStop; ///< ヒットストップのフレーム数
    float moveDist; ///< shotTime までに向いている方向へ移動する距離（負なら後退＝バックステップ）
    Vector3 poseFrom; ///< 銃グリップ回転オフセットの構え側（ラジアン、発砲までに構える）
    Vector3 poseTo; ///< 発砲直後のリコイル側（ラジアン、duration までに構え直す）
};

/** @brief 1銃種ぶんのコンボ一式（地上/空中共通の単一テーブル） */
using GunComboSet = ComboArray<GunShotDef>;

/** @brief 銃種に対応するコンボ一式を返す（未定義タイプは Pistol のセット） */
const GunComboSet& GetGunComboSet(GunType type);

/**
 * @brief 射撃コンボの進行を管理するコントローラ
 * @note MeleeComboController の銃版。K ボタンの受理（段送り/先行入力バッファ）、
 *       発砲タイミング発火、前後移動量、銃の構え→リコイルの回転オフセット算出までを担当する。
 *       弾のヒット判定・視覚弾の発射はシーン側が JustShot() と GetActive() を見て行う。
 */
class GunComboController {
public:
    /**
     * @brief 射撃入力を受け付ける
     * @param type 現在の銃種（途中で変わったら新しいコンボを最初から）
     * @return 発砲開始・段送り・先行入力バッファのいずれかで受理されたら true
     */
    bool TryShoot(GunType type);

    /** @brief 1フレーム進める（発砲発火・先行入力の消化・モーション終了処理） */
    void Update(float dt);

    /** @brief コンボを強制的に打ち切る（被弾・銃切替・乱舞開始時など） */
    void Reset();

    bool IsShooting() const { return active_ != nullptr; }
    bool JustShot() const { return justShot_; } ///< このフレームに発砲したか
    bool JustStartedStep() const { return justStarted_; } ///< このフレームに段が開始したか
    const GunShotDef* GetActive() const { return active_; } ///< 進行中の段（無ければ nullptr）
    int GetStep() const { return stepDisplay_; } ///< 表示用の段数（1始まり）
    float GetMoveDelta() const { return moveDelta_; } ///< このフレームぶんの前後移動量（負なら後退）

    /** @brief 現在の銃回転オフセットを返す（構え→リコイル→構え直しの補間） */
    Vector3 GetPoseOffset() const;

private:
    void StartStep(const GunShotDef* def, int tableIdx);

    const GunShotDef* active_ = nullptr;
    GunType type_ { };
    int stepIdx_ = 0; ///< 現在のテーブル内インデックス
    int stepDisplay_ = 0;
    float timer_ = 0.0f;
    bool shotDone_ = false;
    bool justShot_ = false;
    bool justStarted_ = false;
    bool buffered_ = false; ///< cancelTime 前に押された次段入力を保持
    float chainGraceTimer_ = 0.0f; ///< モーション終了後もコンボを継続できる猶予
    float moveDelta_ = 0.0f;

    static constexpr float kChainGrace_ = 0.50f; ///< 段間の入力猶予（秒、銃は近接より少し緩め）
};

} // namespace engine::game
