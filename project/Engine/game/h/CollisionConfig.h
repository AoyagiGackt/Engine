#pragma once
#include "MakeAffine.h"
#include <algorithm>
#include <cmath>

// =============================================
// 形状プリミティブ
// =============================================

/// レイ（半直線）
struct Ray {
    Vector3 origin;    ///< 発射点
    Vector3 direction; ///< 方向ベクトル（必ず正規化して渡すこと）
};

/**
 * @brief レイキャストの結果
 *
 * Collision::Raycast() の戻り値として使用します。
 *   RaycastResult res;
 *   if (Collision::Raycast(ray, aabb, res)) {
 *       // res.distance, res.point, res.normal が有効
 *   }
 */
struct RaycastResult {
    bool    hit      = false;  ///< ヒットしたか
    float   distance = 0.0f;  ///< origin からヒット点までの距離（t 値）
    Vector3 point    = {};     ///< ヒット点のワールド座標
    Vector3 normal   = {};     ///< ヒット面の外向き法線（正規化済み）
};

/// 球体
struct Sphere {
    Vector3 center; ///< 中心点
    float   radius; ///< 半径
};

/// AABB（回転しない軸並行境界箱）
struct AABB {
    Vector3 min; ///< 最小座標（左・下・奥）
    Vector3 max; ///< 最大座標（右・上・手前）
};

/// カプセル（線分を軸とする球の掃引体）
/// 使い方: キャラクターの上半身〜下半身、武器の刀身など縦長の当たり判定に最適
struct Capsule {
    Vector3 start;  ///< 始点（線分の根元）
    Vector3 end;    ///< 終点（線分の先端）
    float   radius; ///< 太さの半径
};

// =============================================
// コライダー
// =============================================

/// コライダーが持つ形状の種類
enum class ColliderShape {
    AABB,
    Sphere,
    Capsule,
};

/**
 * @brief GameObject が持つ当たり判定データ
 *
 * shape に応じた形状データを使って判定を行います。
 * SetAsAABB / SetAsSphere / SetAsCapsule でセットしてください。
 */
struct Collider {
    ColliderShape shape   = ColliderShape::AABB;
    AABB          aabb    = {};
    Sphere        sphere  = {};
    Capsule       capsule = {};
    bool          isHit   = false;

    // --- 形状セッター ---
    void SetAsAABB   (const AABB& a)    { shape = ColliderShape::AABB;    aabb    = a; }
    void SetAsSphere (const Sphere& s)  { shape = ColliderShape::Sphere;  sphere  = s; }
    void SetAsCapsule(const Capsule& c) { shape = ColliderShape::Capsule; capsule = c; }

    /**
     * @brief Octreeブロードフェーズ用の包含 AABB を返す
     * どの形状でもオブジェクトを完全に包む AABB を計算します
     */
    AABB GetBroadAABB() const {
        switch (shape) {
        case ColliderShape::Sphere:
            return {
                { sphere.center.x - sphere.radius,
                  sphere.center.y - sphere.radius,
                  sphere.center.z - sphere.radius },
                { sphere.center.x + sphere.radius,
                  sphere.center.y + sphere.radius,
                  sphere.center.z + sphere.radius }
            };
        case ColliderShape::Capsule: {
            float r = capsule.radius;
            return {
                { (std::min)(capsule.start.x, capsule.end.x) - r,
                  (std::min)(capsule.start.y, capsule.end.y) - r,
                  (std::min)(capsule.start.z, capsule.end.z) - r },
                { (std::max)(capsule.start.x, capsule.end.x) + r,
                  (std::max)(capsule.start.y, capsule.end.y) + r,
                  (std::max)(capsule.start.z, capsule.end.z) + r }
            };
        }
        default: // AABB
            return aabb;
        }
    }
};
