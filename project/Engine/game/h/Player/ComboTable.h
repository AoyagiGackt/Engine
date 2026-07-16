/**
 * @file ComboTable.h
 * @brief 近接コンボ(MeleeCombo)と射撃コンボ(GunCombo)が共通で使う、
 *        固定長constexpr配列の先頭ポインタ＋要素数を保持する軽量テンプレートと構築ヘルパー
 */
#pragma once
namespace engine::game {

/**
 * @brief コンボ1段ぶんの定義配列を指すビュー（先頭ポインタ＋要素数）
 * @tparam StepT コンボ1段ぶんの定義型（MeleeAttackDef / GunShotDef など）
 * @note 配列そのものの所有権は持たない。呼び出し側の static/constexpr 配列を指すだけ
 */
template <typename StepT>
struct ComboArray {
    const StepT* data = nullptr; ///< 配列の先頭ポインタ
    int count = 0; ///< 要素数

    const StepT& operator[](int index) const { return data[index]; }
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
