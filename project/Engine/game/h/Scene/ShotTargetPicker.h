/**
 * @file ShotTargetPicker.h
 * @brief 射撃が当たる相手を、銃口に一番近い1体に絞り込む部品を定義するファイル
 */
#pragma once
#include "Collision.h"
#include "MakeAffine.h"
#include <algorithm>
#include <limits>

namespace engine::game {

/**
 * @brief 射撃の的選び
 * @note 撃った瞬間に射程の範囲を作り、候補（敵・壊せる物など）を順に Consider() へ渡すと、
 *       範囲に入っていて銃口に一番近い相手だけが的になる。弾は的で止まるので貫通しない。
 *       候補は型に関係なくポインタで見分ける（シーン側は IsTarget() に同じポインタを渡して確かめる）。
 */
class ShotTargetPicker {
public:
    /**
     * @brief 新しい1発の的選びを始める
     * @param muzzle    銃口の位置
     * @param dirX      撃つ向き（+1/-1）
     * @param shotRange 射程の範囲
     */
    void Begin(const Vector3& muzzle, float dirX, const engine::AABB& shotRange)
    {
        muzzle_ = muzzle;
        dirX_ = (dirX >= 0.0f) ? 1.0f : -1.0f;
        shotRange_ = shotRange;
        target_ = nullptr;
        hitDistance_ = (std::numeric_limits<float>::max)();
    }

    /**
     * @brief 候補を1つ調べ、射程に入っていて今までで一番近ければ的にする
     * @param key    候補を見分けるポインタ（敵や設置物の実体のアドレス）
     * @param bounds 候補の当たり判定
     */
    void Consider(const void* key, const engine::AABB& bounds)
    {
        if (key == nullptr || !engine::Collision::CheckCollision(shotRange_, bounds)) {
            return;
        }
        // 銃口から候補の手前の面までの距離（銃口が候補に食い込んでいれば0）
        const float distance = (dirX_ > 0.0f)
            ? (std::max)(bounds.min.x - muzzle_.x, 0.0f)
            : (std::max)(muzzle_.x - bounds.max.x, 0.0f);
        if (distance < hitDistance_) {
            hitDistance_ = distance;
            target_ = key;
        }
    }

    /** @brief 的選びを終えて何も選ばれていない状態に戻す（撃っていないフレーム用） */
    void Clear() { target_ = nullptr; }

    /** @brief この候補が今回の的か */
    bool IsTarget(const void* key) const { return target_ != nullptr && target_ == key; }

    /** @brief 的があるか */
    bool HasTarget() const { return target_ != nullptr; }

    /** @brief 銃口から的の手前の面までの距離（的が無ければ射程いっぱいの見た目に使えるよう呼び出し側で判断する） */
    float GetHitDistance() const { return hitDistance_; }

private:
    Vector3 muzzle_ = { };
    float dirX_ = 1.0f;
    engine::AABB shotRange_ = { };
    const void* target_ = nullptr;
    float hitDistance_ = 0.0f;
};

} // namespace engine::game
