/**
 * @file IEnemyEntity.h
 * @brief EnemyEntity/KnightEnemyが共通して持つ最小限の操作をまとめたインターフェース
 */
#pragma once
#include "IPositionTarget.h"

namespace engine::game {

/**
 * @brief 汎用敵(EnemyEntity)とナイト型敵(KnightEnemy)に共通する読み取り操作をまとめた基底クラス
 * @note ダメージ処理・撃破判定は両者で意味が異なる（ナイトはノックバックをTakeDamageの引数に含み、
 *       汎用敵はApplyComboReaction()へ分離しているなど）ため、あえてここには含めない。
 *       シーン側が種別を問わず扱いたい、位置・HP参照・見た目更新だけを共通化する
 */
class IEnemyEntity : public IPositionTarget {
public:
    /** @brief 派生クラスを基底クラスから正しく破棄できるようにする仮想デストラクタ */
    virtual ~IEnemyEntity() = default;

    /** @brief 現在のHPを返す */
    virtual int GetHp() const = 0;
    /** @brief 最大HPを返す */
    virtual int GetMaxHp() const = 0;
    /** @brief 見た目のトランスフォームだけを再計算する（AI/物理は進めない） */
    virtual void RefreshVisualTransforms() = 0;
    /** @brief ほかの敵を押し退ける体として扱うか（倒れた敵は重なってよい） */
    virtual bool BlocksOtherEnemies() const = 0;
};

} // namespace engine::game
