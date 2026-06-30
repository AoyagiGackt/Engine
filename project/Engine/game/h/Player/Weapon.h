#pragma once
#include <string>
#include <vector>
namespace engine::game {
enum class WeaponType { Sword, Spear, Hammer, Dagger, Ball };

struct WeaponCommand {
    std::string  key;   // ASCII キー表示（例: "Space"）
    std::wstring desc;  // 日本語説明
};

struct WeaponData {
    std::string  name;
    std::string  styleName;   // "鬼神 (Swordmaster)"
    std::wstring styleNameJp; // L"鬼神"
    WeaponType   type;
    float        damage;
    float        range;
    float        attackInterval;
    std::string  description;
    float        styleColor[4];   // RGBA 0.0~1.0
    float        knockbackMult = 1.0f; // ノックバック倍率（武器ごとの差別化）
    std::vector<WeaponCommand> commands;
};

struct RangedWeaponData {
    std::string name;
    float       damage;
    float       range;
    float       attackInterval;
    std::string description;
    std::vector<WeaponCommand> commands;
};

} // namespace engine::game
