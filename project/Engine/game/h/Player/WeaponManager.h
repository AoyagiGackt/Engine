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
    const WeaponData& GetCurrent() const { return weapons_[index_]; }
    /// @brief 選択中の射撃武器データを返す
    const RangedWeaponData& GetRanged() const { return rangedWeapons_[rangedIndex_]; }
    /// @brief 選択中スタイルのインデックスを返す（0始まり）
    int GetIndex() const { return index_; }
    /// @brief スタイル総数を返す
    int GetCount() const { return static_cast<int>(weapons_.size()); }
    /// @brief 全スタイルのリストを返す
    const std::vector<WeaponData>& GetList() const { return weapons_; }

    /// @brief 選択中の銃のインデックスを返す（0始まり）
    int GetRangedIndex() const { return rangedIndex_; }
    /// @brief 銃の総数を返す
    int GetRangedCount() const { return static_cast<int>(rangedWeapons_.size()); }
    /// @brief 全銃のリストを返す
    const std::vector<RangedWeaponData>& GetRangedList() const { return rangedWeapons_; }
    /// @brief 次の銃へ切り替える（循環。銃は近接と違い最初から全部使える）
    void SelectNextRanged() { rangedIndex_ = (rangedIndex_ + 1) % static_cast<int>(rangedWeapons_.size()); }

    /// @brief 指定インデックスのスタイルを選択する（範囲外はクランプ、未解放スロットは無視）
    void SelectIndex(int i);
    /// @brief 次の解放済みスタイルへ切り替える（循環、未解放はスキップ）
    void SelectNext();
    /// @brief 前の解放済みスタイルへ切り替える（循環、未解放はスキップ）
    void SelectPrev();

    /// @brief 指定インデックスのスタイルが解放済みか
    bool IsUnlocked(int i) const { return i >= 0 && i < static_cast<int>(unlocked_.size()) && unlocked_[i]; }

    /**
     * @brief 指定タイプの武器を解放し、そのまま装備する（敵からの武器奪取用）
     * @return 新規解放なら true既に解放済み（重複入手）なら false を返す
     * @note 重複入手時の経験値/強化素材への転用は未実装（戻り値 false を呼び出し側で活用する想定）
     */
    bool Unlock(WeaponType type);

    /** @brief 全武器を解放する（BattleTestScene で全コンボを試すためのデバッグ用） */
    void UnlockAll() { unlocked_.assign(weapons_.size(), true); }

private:
    WeaponManager();

    std::vector<WeaponData> weapons_;
    std::vector<bool> unlocked_;
    std::vector<RangedWeaponData> rangedWeapons_;
    int index_ = 0;
    int rangedIndex_ = 0;
};

} // namespace engine::game
