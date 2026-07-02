/**
 * @file WeaponManager.h
 * @brief スタイル別の武器データを管理するシングルトン
 */
#pragma once
#include "Weapon.h"
#include <vector>
namespace engine::game {

/// @brief スタイル別近接武器と射撃武器の定義を保持し、選択中スタイルを管理する
class WeaponManager {
public:
    /// @brief シングルトンインスタンスを取得する
    static WeaponManager* GetInstance();

    /// @brief 選択中の近接武器データを返す
    const WeaponData&       GetCurrent() const { return weapons_[index_]; }
    /// @brief 射撃武器データを返す
    const RangedWeaponData& GetRanged()  const { return ranged_; }
    /// @brief 選択中スタイルのインデックスを返す（0始まり）
    int  GetIndex() const { return index_; }
    /// @brief スタイル総数を返す
    int  GetCount() const { return static_cast<int>(weapons_.size()); }
    /// @brief 全スタイルのリストを返す
    const std::vector<WeaponData>& GetList() const { return weapons_; }

    /// @brief 指定インデックスのスタイルを選択する（範囲外はクランプ）
    void SelectIndex(int i);
    /// @brief 次のスタイルへ切り替える（循環）
    void SelectNext();
    /// @brief 前のスタイルへ切り替える（循環）
    void SelectPrev();

private:
    WeaponManager();

    std::vector<WeaponData> weapons_;
    RangedWeaponData        ranged_;
    int index_ = 0;
};

} // namespace engine::game
