/**
 * @file AutoComboAssist.h
 * @brief 攻撃ボタンだけで技を出し分ける入力アシスト（オプションのオートコンボ）を定義するファイル
 */
#pragma once
#include <cstddef>
#include <random>
namespace engine::game {

enum class WeaponType; // Weapon.h で定義
class MeleeComboController;

/**
 * @brief 攻撃ボタンの連打/長押しだけでコンボを組み立てる入力アシスト
 * @note 有効な間は長押しでも攻撃を出し続ける。コンボの締め方はルート表から1周ごとにランダムに選び、
 *       地上で締める・打ち上げて空中へ・固有技の連撃で締める、を織り交ぜる（同じルートは2周続けない）。
 *       打ち上げ技が当たったら追撃ジャンプを、固有技のルートでは段が当たったら固有技を自動で入れる
 */
class AutoComboAssist {
public:
    AutoComboAssist();

    /** @brief アシストの有効/無効を切り替える（無効にしたら出しかけの追撃ジャンプ・固有技も取り消す） */
    void SetEnabled(bool enabled);
    bool IsEnabled() const { return enabled_; }

    /**
     * @brief このフレームに攻撃を出すか
     * @param triggered 攻撃ボタンを押した瞬間か
     * @param held      攻撃ボタンを押し続けているか（有効時のみ見る）
     */
    bool WantsAttack(bool triggered, bool held) const { return triggered || (enabled_ && held); }

    /**
     * @brief 次の段を打ち上げ技にするか（今のルートの打ち上げ段を振っている最中なら true）
     * @param type     現在の武器タイプ
     * @param combo    近接コンボの進行状況
     * @param airborne 空中か（空中では打ち上げ技を出さない）
     */
    bool WantsLauncher(WeaponType type, const MeleeComboController& combo, bool airborne) const;

    /**
     * @brief 段が始まったことを伝える（前の周を締め終えていれば、ここで次のルートを選ぶ）
     * @param type     現在の武器タイプ
     * @param combo    近接コンボの進行状況（始まった段が GetActive() に入っている状態で呼ぶ）
     * @param airborne 空中か
     */
    void OnStepStarted(WeaponType type, const MeleeComboController& combo, bool airborne);

    /**
     * @brief 段の本ヒットを伝える（固有技のルートで固有技の段に当たったら、固有技を予約する）
     * @param type     現在の武器タイプ
     * @param combo    近接コンボの進行状況
     * @param airborne 空中か
     */
    void OnStepHit(WeaponType type, const MeleeComboController& combo, bool airborne);

    /** @brief 打ち上げ技が当たったことを伝える（追撃ジャンプを予約する） */
    void OnLauncherHit();

    /** @brief 予約された追撃ジャンプを1回だけ取り出す（取り出したら予約は消える） */
    bool ConsumeJumpRequest();

    /** @brief 予約された固有技を1回だけ取り出す（取り出したら予約は消える） */
    bool ConsumeSkillRequest();

    /** @brief 1フレーム進める（取り出されなかった追撃ジャンプ・固有技の予約を期限切れにする） */
    void Update(float dt);

private:
    /** @brief 今のルートで打ち上げに移る地上コンボの段（1始まり）。打ち上げないルートなら0 */
    int LaunchStep(WeaponType type) const;
    /** @brief 今のルートで固有技に繋ぐ地上コンボの段（1始まり）。固有技を使わないルートなら0 */
    int SkillStep(WeaponType type) const;
    /** @brief その武器で使えるルートの中から、今のルート以外をランダムに選ぶ */
    void PickNextRoute(WeaponType type);

    bool enabled_ = false;
    float jumpRequestTimer_ = 0.0f; ///< 追撃ジャンプの予約の残り秒数（0なら予約なし）
    float skillRequestTimer_ = 0.0f; ///< 固有技の予約の残り秒数（0なら予約なし）
    std::size_t routeIndex_ = 0; ///< ルート表の今の位置
    bool routeFinished_ = false; ///< 今のルートを締め終えたか（次の地上の段が始まったら次のルートを選ぶ）
    std::mt19937 rng_;

    static constexpr float kRequestWindow_ = 0.3f; ///< 着地待ちなどですぐ出せなかった予約を保つ秒数
};

} // namespace engine::game
