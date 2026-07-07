/**
 * @file Weapon.h
 * @brief 武器・戦闘スタイルのデータ構造を定義するファイル（実データは Resources/weapons.json）
 */
#pragma once
#include <string>
#include <vector>
namespace engine::game {
/** @brief 近接武器の種類（WeaponManagerが読み込むJSONの"type"と対応） */
enum class WeaponType { Sword, Spear, Hammer, Dagger, Ball };

/** @brief 操作説明HUDに表示する1コマンドぶんのキー表示と説明 */
struct WeaponCommand {
    std::string  key;   ///< ASCII キー表示（例: "Space"）
    std::wstring desc;  ///< 日本語説明
};

/** @brief 1スタイルぶんの近接武器データ */
struct WeaponData {
    std::string  name;
    std::string  styleName;   ///< 表示名
    std::wstring styleNameJp; ///< 日本語表示名
    WeaponType   type;
    float        damage;
    float        range;
    float        attackInterval;
    std::string  description;
    float        styleColor[4];        ///< RGBA 0.0~1.0
    float        knockbackMult = 1.0f; ///< ノックバック倍率（武器ごとの差別化）
    std::vector<WeaponCommand> commands;
};

/** @brief 全スタイル共通の射撃武器データ */
struct RangedWeaponData {
    std::string name;
    float       damage;
    float       range;
    float       attackInterval;
    std::string description;
    std::vector<WeaponCommand> commands;
};

} // namespace engine::game
