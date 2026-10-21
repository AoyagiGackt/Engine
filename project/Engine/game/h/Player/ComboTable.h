/**
 * @file ComboTable.h
 * @brief 近接コンボ(MeleeCombo)と射撃コンボ(GunCombo)が共通で使う、
 *        固定長constexpr配列の先頭ポインタ＋要素数を保持する軽量テンプレートと構築ヘルパー
 */
#pragma once
#include <algorithm>
#include <optional>

namespace engine::game {

/**
 * @brief コンボの時間を1フレーム進める（段の再生中は経過時間を、段が無い時は次の段を受け付ける猶予を進める）
 * @param stepActive 段を再生中か
 * @param elapsed    段の経過時間（再生中だけ dt 進める）
 * @param grace      段が終わった後の受付猶予（段が無い時だけ dt 減らし、0 で止める）
 * @return 段を再生中なら、進める前の経過時間。段が無ければ値なし
 */
inline std::optional<float> AdvanceComboTime(bool stepActive, float dt, float& elapsed, float& grace)
{
    if (!stepActive)
    {
        grace = (std::max)(grace - dt, 0.0f);
        return std::nullopt;
    }
    const float previous = elapsed;
    elapsed += dt;
    return previous;
}

/**
 * @brief コンボ1段ぶんの定義配列を指すビュー（先頭ポインタ＋要素数）
 * @tparam StepT コンボ1段ぶんの定義型（MeleeAttackDef / GunShotDef など）
 * @note 配列そのものの所有権は持たない。呼び出し側の static/constexpr 配列を指すだけ
 */
template <typename StepT>
struct ComboArray {
    const StepT* data = nullptr; ///< 配列の先頭ポインタ
    int count = 0; ///< 要素数

    /** @brief 指定段の定義を返す。添字の範囲は呼び出し側で保証する */
    const StepT& operator[](int index) const {
        return data[index];
    }
};

/**
 * @brief constexpr配列から ComboArray を構築する（要素数はテンプレート引数推論に任せる）
 * @tparam StepT コンボ1段ぶんの定義型
 * @tparam N 配列の要素数（呼び出し側の配列サイズから自動推論）
 * @param arr 参照渡しされる constexpr 配列
 */
template <typename StepT, int N>
constexpr ComboArray<StepT> MakeComboArray(const StepT (&arr)[N])
{
    return { arr, N };
}

} // namespace engine::game
