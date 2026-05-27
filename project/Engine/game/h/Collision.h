#pragma once
#include "CollisionConfig.h"

/**
 * @brief 各種形状の衝突判定を提供する静的クラス
 *
 * 対応形状の組み合わせ:
 *   Sphere  × Sphere
 *   AABB    × AABB
 *   Sphere  × AABB
 *   Sphere  × Capsule
 *   Capsule × Capsule
 *   Capsule × AABB
 *   Collider × Collider （形状を自動ディスパッチ）
 */
class Collision {
public:
    // ----- 既存 -----
    static bool CheckCollision(const Sphere& s1, const Sphere& s2);
    static bool CheckCollision(const AABB& a,    const AABB& b);

    // ----- 球 × AABB -----
    static bool CheckCollision(const Sphere& s, const AABB& b);
    static bool CheckCollision(const AABB& b,   const Sphere& s) { return CheckCollision(s, b); }

    // ----- 球 × カプセル -----
    static bool CheckCollision(const Sphere& s,  const Capsule& c);
    static bool CheckCollision(const Capsule& c, const Sphere& s) { return CheckCollision(s, c); }

    // ----- カプセル × カプセル -----
    static bool CheckCollision(const Capsule& c1, const Capsule& c2);

    // ----- カプセル × AABB -----
    static bool CheckCollision(const Capsule& c, const AABB& b);
    static bool CheckCollision(const AABB& b,    const Capsule& c) { return CheckCollision(c, b); }

    // ----- 形状ディスパッチ（CollisionManager から使用） -----
    static bool CheckCollision(const Collider& a, const Collider& b);

    // ===========================================================
    // レイキャスト
    // ===========================================================

    /**
     * @brief レイ vs AABB（スラブ法）
     * @param[out] result ヒット情報（法線は入射面の外向き軸法線）
     * @return ヒットした場合 true
     */
    static bool Raycast(const Ray& ray, const AABB& aabb, RaycastResult& result);

    /**
     * @brief レイ vs 球体（二次方程式）
     * @param[out] result ヒット情報（法線は球面の外向き法線）
     */
    static bool Raycast(const Ray& ray, const Sphere& sphere, RaycastResult& result);

    /**
     * @brief レイ vs カプセル（無限円柱 + 端球）
     * @param[out] result ヒット情報（最近傍のヒット）
     */
    static bool Raycast(const Ray& ray, const Capsule& capsule, RaycastResult& result);

    /**
     * @brief レイ vs Collider（形状を自動ディスパッチ）
     */
    static bool Raycast(const Ray& ray, const Collider& collider, RaycastResult& result);

private:
    // --- 内部ユーティリティ ---

    /// 点 p に最も近い線分上の点を返す
    static Vector3 ClosestPointOnSegment(const Vector3& p,
                                          const Vector3& segStart,
                                          const Vector3& segEnd);

    /**
     * @brief 線分 [p0,p1] と線分 [q0,q1] の最近傍点間の距離² を返す
     * @param[out] outS p 側のパラメータ（0〜1）
     * @param[out] outT q 側のパラメータ（0〜1）
     * Christer Ericson "Real-Time Collision Detection" に基づく実装
     */
    static float SegmentToSegmentDistSq(const Vector3& p0, const Vector3& p1,
                                         const Vector3& q0, const Vector3& q1,
                                         float& outS, float& outT);

    /// AABB 上で点 p に最も近い点を返す
    static Vector3 ClosestPointOnAABB(const Vector3& p, const AABB& b);
};
