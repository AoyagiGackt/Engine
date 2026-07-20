/**
 * @file WeaponManager.h
 * @brief スタイル別の武器データを管理するシングルトン
 */
#pragma once
#include "Weapon.h"
#include <array>
#include <vector>
namespace engine::game {

/// @brief スタイル別近接武器と射撃武器の定義を保持し、選択中スタイルを管理する
class WeaponManager {
public:
    /** @brief 武器入手を4スロットへ反映した結果 */
    enum class AcquireResult {
        Added,
        Duplicate,
        NeedsReplacement,
    };

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

    /** @brief 全解放済み武器を対象に次を選び、現在スロットへ割り当てる */
    void SelectNextUnlockedInCurrentSlot();

    /** @brief 全解放済み武器を対象に前を選び、現在スロットへ割り当てる */
    void SelectPrevUnlockedInCurrentSlot();

    /// @brief 指定インデックスのスタイルが解放済みか
    bool IsUnlocked(int i) const { return i >= 0 && i < static_cast<int>(unlocked_.size()) && unlocked_[i]; }

    /** @brief 指定した4スロットを選択する */
    void SelectSlot(int slot);
    /** @brief スロットに登録された武器インデックスを返す */
    int GetSlotWeaponIndex(int slot) const;
    /** @brief 現在選択しているスロット番号を返す */
    int GetSelectedSlot() const { return selectedSlot_; }
    bool HasEquippedWeapon() const
    {
        return selectedSlot_ >= 0 && selectedSlot_ < static_cast<int>(slots_.size())
            && slots_[selectedSlot_] >= 0;
    }
    /** @brief 武器を空きスロットへ追加し、満杯なら交換待ちにする */
    AcquireResult Acquire(WeaponType type);
    /** @brief 交換待ちの武器で指定スロットを置き換える */
    void ReplacePendingWeapon(int slot);
    /** @brief 交換待ちの武器を破棄する */
    void DiscardPendingWeapon();
    /** @brief 武器交換の選択待ちか返す */
    bool HasPendingWeapon() const { return pendingWeaponIndex_ >= 0; }
    /** @brief 交換待ちの武器データを返す */
    const WeaponData& GetPendingWeapon() const { return weapons_[pendingWeaponIndex_]; }

    /**
     * @brief 指定タイプの武器を解放し、そのまま装備する（敵からの武器奪取用）
     * @return 新規解放なら true既に解放済み（重複入手）なら false を返す
     * @note 重複入手時の経験値/強化素材への転用は未実装（戻り値 false を呼び出し側で活用する想定）
     */
    bool Unlock(WeaponType type);

    /** @brief 全武器を解放し、先頭4種をスロットへ登録する */
    void UnlockAll();

private:
    WeaponManager();

    std::vector<WeaponData> weapons_;
    std::vector<bool> unlocked_;
    std::array<int, 4> slots_ { -1, -1, -1, -1 };
    std::vector<RangedWeaponData> rangedWeapons_;
    int index_ = 0;
    int selectedSlot_ = -1;
    int pendingWeaponIndex_ = -1;
    int rangedIndex_ = 0;
};

} // namespace engine::game
