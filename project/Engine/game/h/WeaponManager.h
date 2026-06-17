#pragma once
#include "Weapon.h"
#include <vector>

class WeaponManager {
public:
    static WeaponManager* GetInstance();

    const WeaponData&       GetCurrent() const { return weapons_[index_]; }
    const RangedWeaponData& GetRanged()  const { return ranged_; }
    int  GetIndex() const { return index_; }
    int  GetCount() const { return static_cast<int>(weapons_.size()); }
    const std::vector<WeaponData>& GetList() const { return weapons_; }

    void SelectIndex(int i);
    void SelectNext();
    void SelectPrev();

private:
    WeaponManager();
    static WeaponManager* instance_;

    std::vector<WeaponData> weapons_;
    RangedWeaponData        ranged_;
    int index_ = 0;
};
