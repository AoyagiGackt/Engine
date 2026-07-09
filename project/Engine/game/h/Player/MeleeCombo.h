/**
 * @file MeleeCombo.h
 * @brief 武器タイプ別の近接コンボ定義と、段送り・先行入力・スイング進行を管理するコントローラ
 */
#pragma once
#include "Vector3.h"
namespace engine::game {

enum class WeaponType; // Weapon.h で定義

/**
 * @brief 近接攻撃1段ぶんの定義
 * @note damage は WeaponData.damage に damageMult を掛けて使う（武器の個性は基礎値、段の個性は倍率）
 */
struct MeleeAttackDef {
    const char* id;         ///< スタイル評価用の技ID（同じ技の連発を検出するキー）
    float damageMult;       ///< 基礎ダメージ倍率
    float duration;         ///< モーション全体の長さ（秒）
    float hitTime;          ///< 攻撃判定が発生するタイミング（秒）
    float cancelTime;       ///< 次段への先行入力を発動できる時刻（秒、hitTime 以上にする）
    float lungeDist;        ///< hitTime までに向いている方向へ前進する距離
    float rangeMult;        ///< WeaponData.range に掛けるリーチ倍率
    float knockX;           ///< 水平ノックバック初速
    float knockY;           ///< 垂直ノックバック初速（launcher 技はここが大きい）
    bool  launcher;         ///< 打ち上げ技か（ヒット時に敵を浮かせ、追撃ジャンプ猶予が付く）
    int   hitStop;          ///< ヒットストップのフレーム数
    float animSpeed;        ///< プレイヤー攻撃アニメの再生速度倍率
    bool  slashAnim;        ///< true=斬撃アニメ / false=パンチアニメ
    Vector3 swingFrom;      ///< 武器グリップ回転オフセットの振りかぶり側（ラジアン）
    Vector3 swingTo;        ///< 武器グリップ回転オフセットの振り抜き側（ラジアン）
    Vector3 bodyLeanFrom;   ///< 体（rig_->object）の傾きオフセットの振りかぶり側（ラジアン、X=前後/Z=左右）
    Vector3 bodyLeanTo;     ///< 体の傾きオフセットの振り抜き側（ラジアン）
};

/** @brief 1武器タイプぶんのコンボ一式（地上コンボ・空中コンボ・打ち上げ技） */
struct MeleeComboSet {
    const MeleeAttackDef* ground;      ///< 地上コンボ配列
    int                   groundCount;
    const MeleeAttackDef* air;         ///< 空中コンボ配列
    int                   airCount;
    const MeleeAttackDef* launcher;    ///< 打ち上げ技（S+攻撃、地上のみ）
};

/** @brief 武器タイプに対応するコンボ一式を返す（未定義タイプは Sword のセット） */
const MeleeComboSet& GetMeleeComboSet(WeaponType type);

/**
 * @brief 近接コンボの進行を管理するコントローラ
 * @note 攻撃ボタンの受理（段送り/先行入力バッファ）、ヒットタイミング発火、
 *       前進量、武器スイングの回転オフセット算出までを担当する。
 *       当たり判定そのものはシーン側が JustHit() と GetActive() を見て行う。
 */
class MeleeComboController {
public:
    /**
     * @brief 攻撃入力を受け付ける
     * @param type          現在の武器タイプ（途中で変わったら新しいコンボを最初から）
     * @param launcherInput 下入力（S/↓）を押しながらか（地上なら打ち上げ技になる）
     * @param airborne      空中か（空中コンボ表に切り替わる）
     * @return 攻撃開始・段送り・先行入力バッファのいずれかで受理されたら true
     */
    bool TryAttack(WeaponType type, bool launcherInput, bool airborne);

    /** @brief 1フレーム進める（ヒット発火・先行入力の消化・モーション終了処理） */
    void Update(float dt);

    /** @brief コンボを強制的に打ち切る（被弾・乱舞開始時など） */
    void Reset();

    bool IsAttacking()      const { return active_ != nullptr; }
    bool JustHit()          const { return justHit_; }     ///< このフレームに攻撃判定が発生したか
    bool JustStartedStep()  const { return justStarted_; } ///< このフレームに段が開始したか（アニメ再生用）
    const MeleeAttackDef* GetActive() const { return active_; } ///< 進行中の段（無ければ nullptr）
    int   GetStep()         const { return stepDisplay_; }  ///< 表示用の段数（1始まり、打ち上げ技は0）
    float GetLungeDelta()   const { return lungeDelta_; }   ///< このフレームぶんの前進量

    /** @brief 現在のスイング回転オフセットを返す（振りかぶり→振り抜き→構え直しの補間） */
    Vector3 GetSwingOffset() const;

    /** @brief 現在の体の傾きオフセットを返す（同じ振りかぶり→振り抜き→構え直しのタイミングで補間） */
    Vector3 GetBodyLeanOffset() const;

private:
    /** @brief GetSwingOffset/GetBodyLeanOffset共通: 振りかぶり→振り抜き→構え直しの3相でfrom/toを補間する */
    Vector3 BlendPhase(const Vector3& from, const Vector3& to) const;

    void StartStep(const MeleeAttackDef* def, int tableIdx, bool airMode, bool isLauncher);
    /** @brief 現在の状態から次に出すべき段を返す（テーブル末尾は先頭へループ） */
    const MeleeAttackDef* NextStep(bool launcherInput, bool airborne, int& outIdx, bool& outLauncher) const;

    const MeleeAttackDef* active_ = nullptr;
    WeaponType type_{};
    bool  airMode_          = false; ///< 空中コンボ表を使用中か
    bool  launcherMode_     = false; ///< 打ち上げ技を再生中か
    int   stepIdx_          = 0;     ///< 現在のテーブル内インデックス
    int   stepDisplay_      = 0;
    float timer_            = 0.0f;
    bool  hitDone_          = false;
    bool  justHit_          = false;
    bool  justStarted_      = false;
    bool  buffered_         = false; ///< cancelTime 前に押された次段入力を保持
    bool  bufferedLauncher_ = false;
    bool  bufferedAir_      = false;
    float chainGraceTimer_  = 0.0f;  ///< モーション終了後もコンボを継続できる猶予
    float lungeDelta_       = 0.0f;

    static constexpr float kChainGrace_ = 0.45f; ///< 段間の入力猶予（秒）
};

} // namespace engine::game
