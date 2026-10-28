/**
 * @file EnemyTypeLibrary.h
 * @brief エディタで作った「敵の種類」（動き方・見た目・武器・強さの倍率の組み合わせ）を Resources/Config/enemy_types.json から読み書きするファイル
 * @note 種類を配置物へ当てると、その時点の値を配置物へ写す（ステージのJSONだけで敵が再現でき、種類を後で消しても壊れない）
 */
#pragma once
#include <string>
#include <vector>

namespace engine::game {

struct ObjectDesc;

/** @brief 敵の種類1つぶんの定義 */
struct EnemyTypeDef {
    std::string id; ///< 保存用の名前（英数字）。配置物の enemyType に入る
    std::string label; ///< エディタに出す名前
    std::string behavior = "basic"; ///< 動き方（EnemyEntity の性格名: basic=歩く flying=飛ぶ healer=回復役）
    std::string look; ///< 見た目（enemy_params.json の visuals の名前。空なら騎士）
    std::string weapon; ///< 持たせて奪わせる武器（空なら持たない）
    float hpScale = 1.0f; ///< HPの倍率
    float damageScale = 1.0f; ///< 攻撃力の倍率
    float speedScale = 1.0f; ///< 近づく速さの倍率
};

/** @brief 敵の種類の一覧を読み書きする */
class EnemyTypeLibrary {
public:
    static EnemyTypeLibrary* GetInstance();

    /** @brief ファイルから読み直す */
    void Reload();
    /** @brief 今の一覧をファイルへ書き出す */
    bool Save() const;

    /** @brief 一覧を返す */
    const std::vector<EnemyTypeDef>& Types() const {
        return types_;
    }
    /** @brief 一覧を書き換える（エディタから。保存は Save() で行う） */
    std::vector<EnemyTypeDef>& MutableTypes() {
        return types_;
    }
    /** @brief 名前から種類を探す（無ければ nullptr） */
    const EnemyTypeDef* Find(const std::string& id) const;

    /** @brief 種類の値を配置物へ写す（動き方・見た目・武器・強さの倍率と、種類の名前） */
    static void ApplyTo(const EnemyTypeDef& type, ObjectDesc& desc);

private:
    EnemyTypeLibrary();
    EnemyTypeLibrary(const EnemyTypeLibrary&) = delete;
    EnemyTypeLibrary& operator=(const EnemyTypeLibrary&) = delete;

    std::vector<EnemyTypeDef> types_;
};

} // namespace engine::game
