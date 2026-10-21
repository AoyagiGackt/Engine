/**
 * @file Collision.cpp
 * @brief 球・AABB・カプセルの各組み合わせの交差判定とレイキャスト（Collision）の実装
 */
#include "Collision.h"
#include <algorithm>
#include <iterator>
#include <cfloat> // FLT_MAX
#include <cmath>

namespace engine {
namespace {
/** @brief 長さの2乗・係数がこれ以下なら0（線分が点に縮んでいる等）とみなす */
constexpr float kDegenerateEpsilon = 1e-10f;
/** @brief レイの方向成分がこれより小さければ、その軸と平行とみなす */
constexpr float kParallelEpsilon = 1e-8f;

/** @brief レイと1軸の区間を交差させ、共通の入射・離脱距離と入射面を更新する */
bool IntersectRaySlab(float origin, float direction, float minimum, float maximum,
    const Vector3& minimumNormal, float& enter, float& exit, Vector3& hitNormal)
{
    // 平行なレイは区間の内側にある場合だけ他の軸の判定へ進む
    if (std::abs(direction) < kParallelEpsilon)
    {
        return origin >= minimum && origin <= maximum;
    }
    const float inverseDirection = 1.0f / direction;
    float nearDistance = (minimum - origin) * inverseDirection;
    float farDistance = (maximum - origin) * inverseDirection;
    Vector3 nearNormal = minimumNormal;
    if (nearDistance > farDistance)
    {
        std::swap(nearDistance, farDistance);
        nearNormal = { -nearNormal.x, -nearNormal.y, -nearNormal.z };
    }
    // 最も遅く入る面と最も早く出る面の間だけが全軸に共通する
    if (nearDistance > enter)
    {
        enter = nearDistance;
        hitNormal = nearNormal;
    }
    exit = std::min(exit, farDistance);
    return enter <= exit;
}
} // namespace

/** @brief 球どうしが重なっているかを返す */
bool Collision::CheckCollision(const Sphere& s1, const Sphere& s2)
{
    float dx = s2.center.x - s1.center.x;
    float dy = s2.center.y - s1.center.y;
    float dz = s2.center.z - s1.center.z;
    float distSq = dx * dx + dy * dy + dz * dz;
    float radiusSum = s1.radius + s2.radius;
    return distSq <= radiusSum * radiusSum;
}

// AABB × AABB
bool Collision::CheckCollision(const AABB& a, const AABB& b)
{
    return (a.min.x <= b.max.x && a.max.x >= b.min.x)
        && (a.min.y <= b.max.y && a.max.y >= b.min.y)
        && (a.min.z <= b.max.z && a.max.z >= b.min.z);
}

// 内部ユーティリティ

/** @brief AABB の内側に点を収め、AABB 上で点に最も近い位置を返す */
Vector3 Collision::ClosestPointOnAABB(const Vector3& p, const AABB& b)
{
    return {
        std::max(b.min.x, std::min(p.x, b.max.x)),
        std::max(b.min.y, std::min(p.y, b.max.y)),
        std::max(b.min.z, std::min(p.z, b.max.z))
    };
}

/** @brief 点を線分上へ投影し、線分の両端の間に収めた最も近い点を返す */
Vector3 Collision::ClosestPointOnSegment(const Vector3& p,
    const Vector3& a, const Vector3& b)
{
    Vector3 ab = { b.x - a.x, b.y - a.y, b.z - a.z };
    float len2 = ab.x * ab.x + ab.y * ab.y + ab.z * ab.z;
    if (len2 < kDegenerateEpsilon)
    {
        return a;
    }
    float t = ((p.x - a.x) * ab.x + (p.y - a.y) * ab.y + (p.z - a.z) * ab.z) / len2;
    t = std::max(0.0f, std::min(1.0f, t));
    return { a.x + ab.x * t, a.y + ab.y * t, a.z + ab.z * t };
}

// 線分 [p0,p1] と線分 [q0,q1] の最近傍距離²
float Collision::SegmentToSegmentDistSq(const Vector3& p0, const Vector3& p1,
    const Vector3& q0, const Vector3& q1,
    float& outS, float& outT)
{
    Vector3 d1 = { p1.x - p0.x, p1.y - p0.y, p1.z - p0.z };
    Vector3 d2 = { q1.x - q0.x, q1.y - q0.y, q1.z - q0.z };
    Vector3 r = { p0.x - q0.x, p0.y - q0.y, p0.z - q0.z };

    float a = d1.x * d1.x + d1.y * d1.y + d1.z * d1.z; // |d1|²
    float e = d2.x * d2.x + d2.y * d2.y + d2.z * d2.z; // |d2|²
    float f = d2.x * r.x + d2.y * r.y + d2.z * r.z;

    float s, t;
    if (a <= kDegenerateEpsilon && e <= kDegenerateEpsilon)
    {
        // 両線分とも点に縮退
        s = t = 0.0f;
    }
    else if (a <= kDegenerateEpsilon)
    {
        // p 側が点に縮退
        s = 0.0f;
        t = std::max(0.0f, std::min(1.0f, f / e));
    }
    else
    {
        float c = d1.x * r.x + d1.y * r.y + d1.z * r.z;
        if (e <= kDegenerateEpsilon)
        {
            // q 側が点に縮退
            t = 0.0f;
            s = std::max(0.0f, std::min(1.0f, -c / a));
        }
        else
        {
            // 一般ケース
            float b = d1.x * d2.x + d1.y * d2.y + d1.z * d2.z;
            float denom = a * e - b * b;
            if (std::abs(denom) > kDegenerateEpsilon)
            {
                s = std::max(0.0f, std::min(1.0f, (b * f - c * e) / denom));
            }
            else
            {
                s = 0.0f; // 平行 任意の点を選ぶ
            }
            t = (b * s + f) / e;
            // t がクランプされた場合は s を再計算
            if (t < 0.0f)
            {
                t = 0.0f;
                s = std::max(0.0f, std::min(1.0f, -c / a));
            }
            else if (t > 1.0f)
            {
                t = 1.0f;
                s = std::max(0.0f, std::min(1.0f, (b - c) / a));
            }
        }
    }

    outS = s;
    outT = t;

    Vector3 c1 = { p0.x + d1.x * s, p0.y + d1.y * s, p0.z + d1.z * s };
    Vector3 c2 = { q0.x + d2.x * t, q0.y + d2.y * t, q0.z + d2.z * t };
    float dx = c1.x - c2.x, dy = c1.y - c2.y, dz = c1.z - c2.z;
    return dx * dx + dy * dy + dz * dz;
}

// 球 × AABB
bool Collision::CheckCollision(const Sphere& s, const AABB& b)
{
    // AABB 上で球の中心に最も近い点を求め、距離と半径を比較
    Vector3 closest = ClosestPointOnAABB(s.center, b);
    float dx = s.center.x - closest.x;
    float dy = s.center.y - closest.y;
    float dz = s.center.z - closest.z;
    return dx * dx + dy * dy + dz * dz <= s.radius * s.radius;
}

// 球 × カプセル
bool Collision::CheckCollision(const Sphere& s, const Capsule& c)
{
    // カプセル軸上で球中心に最も近い点を求め、（球半径 + カプセル半径）と比較
    Vector3 closest = ClosestPointOnSegment(s.center, c.start, c.end);
    float dx = s.center.x - closest.x;
    float dy = s.center.y - closest.y;
    float dz = s.center.z - closest.z;
    float r = s.radius + c.radius;
    return dx * dx + dy * dy + dz * dz <= r * r;
}

// カプセル × カプセル
bool Collision::CheckCollision(const Capsule& c1, const Capsule& c2)
{
    // 線分同士の最近傍距離²と（半径の和）²を比較
    float s, t;
    float distSq = SegmentToSegmentDistSq(c1.start, c1.end,
        c2.start, c2.end, s, t);
    float r = c1.radius + c2.radius;
    return distSq <= r * r;
}

// カプセル × AABB（交互射影法による近似）
// アルゴリズム
//   AABB 中心 → カプセル軸上の最近傍点 P を求める
//   P → AABB 上の最近傍点 Q を求める
//   Q → カプセル軸上の最近傍点 R を求める（精度向上）
//  |R - Q| < radius なら衝突
bool Collision::CheckCollision(const Capsule& c, const AABB& b)
{
    // AABB 中心からカプセル軸への射影
    Vector3 center = { (b.min.x + b.max.x) * 0.5f,
        (b.min.y + b.max.y) * 0.5f,
        (b.min.z + b.max.z) * 0.5f };
    Vector3 segPt = ClosestPointOnSegment(center, c.start, c.end);

    // セグメント最近傍点から AABB への射影
    Vector3 aabbPt = ClosestPointOnAABB(segPt, b);

    // AABB 最近傍点からセグメントへの射影（精度向上）
    Vector3 finalSeg = ClosestPointOnSegment(aabbPt, c.start, c.end);

    float dx = finalSeg.x - aabbPt.x;
    float dy = finalSeg.y - aabbPt.y;
    float dz = finalSeg.z - aabbPt.z;
    return dx * dx + dy * dy + dz * dz <= c.radius * c.radius;
}

// レイキャスト  AABB
bool Collision::Raycast(const Ray& ray, const AABB& aabb, RaycastResult& result)
{
    result = { };
    float tmin = 0.0f;
    float tmax = FLT_MAX;
    Vector3 hitNormal = { };

    // 各軸の区間を同じ計算で狭め、入射面の法線を保持する
    if (!IntersectRaySlab(ray.origin.x, ray.direction.x, aabb.min.x, aabb.max.x,
            { -1.0f, 0.0f, 0.0f }, tmin, tmax, hitNormal)
        || !IntersectRaySlab(ray.origin.y, ray.direction.y, aabb.min.y, aabb.max.y,
            { 0.0f, -1.0f, 0.0f }, tmin, tmax, hitNormal)
        || !IntersectRaySlab(ray.origin.z, ray.direction.z, aabb.min.z, aabb.max.z,
            { 0.0f, 0.0f, -1.0f }, tmin, tmax, hitNormal))
    {
        return false;
    }

    // tmin < 0 はレイ原点が AABB 内部 → 衝突なし扱い（必要に応じてゲーム側で対応）
    if (tmin < 0.0f)
    {
        return false;
    }

    result.hit = true;
    result.distance = tmin;
    result.point = {
        ray.origin.x + ray.direction.x * tmin,
        ray.origin.y + ray.direction.y * tmin,
        ray.origin.z + ray.direction.z * tmin
    };
    result.normal = hitNormal;
    return true;
}

// レイキャスト  球体
bool Collision::Raycast(const Ray& ray, const Sphere& sphere, RaycastResult& result)
{
    result = { };
    Vector3 oc = {
        ray.origin.x - sphere.center.x,
        ray.origin.y - sphere.center.y,
        ray.origin.z - sphere.center.z
    };

    // ray.direction は正規化済みを仮定  dot(d,d) = 1
    float b = Dot(oc, ray.direction); // b/2 係数
    float c = Dot(oc, oc) - sphere.radius * sphere.radius;
    float disc = b * b - c; // 判別式 = (b/2)² - ac (a=1)

    if (disc < 0.0f)
    {
        return false;
    }

    float sqrtDisc = std::sqrt(disc);
    float t = -b - sqrtDisc; // 手前側の交点
    if (t < 0.0f)
    {
        t = -b + sqrtDisc; // レイ原点が球内部  奥側を使う
        if (t < 0.0f)
        {
            return false;
        }
    }

    result.hit = true;
    result.distance = t;
    result.point = {
        ray.origin.x + ray.direction.x * t,
        ray.origin.y + ray.direction.y * t,
        ray.origin.z + ray.direction.z * t
    };
    // 法線 = ヒット点から球心への方向を正規化
    float invR = 1.0f / sphere.radius;
    result.normal = {
        (result.point.x - sphere.center.x) * invR,
        (result.point.y - sphere.center.y) * invR,
        (result.point.z - sphere.center.z) * invR
    };
    return true;
}

// レイキャスト  Ray vs カプセル（無限円柱 + 端球）
// アルゴリズム概要
//   カプセル軸方向の垂直成分だけで二次方程式を立て円柱交点を求める
//   軸方向に [0, 1] にクリップして胴体判定
//   端球（start/end の Sphere）とも判定し最小 t を採用
bool Collision::Raycast(const Ray& ray, const Capsule& capsule, RaycastResult& result)
{
    result = { };

    // 端球判定
    RaycastResult capA, capB;
    bool hitA = Raycast(ray, Sphere { capsule.start, capsule.radius }, capA);
    bool hitB = Raycast(ray, Sphere { capsule.end, capsule.radius }, capB);

    // 円柱胴体判定
    RaycastResult cyl;
    bool hitCyl = false;

    Vector3 ab = {
        capsule.end.x - capsule.start.x,
        capsule.end.y - capsule.start.y,
        capsule.end.z - capsule.start.z
    };
    float abLen2 = ab.x * ab.x + ab.y * ab.y + ab.z * ab.z;

    if (abLen2 > kDegenerateEpsilon)
    {
        Vector3 ao = {
            ray.origin.x - capsule.start.x,
            ray.origin.y - capsule.start.y,
            ray.origin.z - capsule.start.z
        };

        // ray.direction は正規化済みなので dot(D,D) = 1
        float dAB = Dot(ray.direction, ab); // D·AB
        float aoAB = Dot(ao, ab); // AO·AB
        float dAo = Dot(ray.direction, ao); // D·AO

        // 垂直成分の二次方程式係数
        // 二次係数・一次係数の半分・定数項を使い、判別式と根を計算する
        float quadraticCoefficient = 1.0f - dAB * dAB / abLen2;
        float halfLinearCoefficient = dAo - aoAB * dAB / abLen2; // 係数 b/2
        float constantCoefficient = Dot(ao, ao) - aoAB * aoAB / abLen2 - capsule.radius * capsule.radius;

        if (std::abs(quadraticCoefficient) > kDegenerateEpsilon)
        {
            float disc = halfLinearCoefficient * halfLinearCoefficient - quadraticCoefficient * constantCoefficient;
            if (disc >= 0.0f)
            {
                float sqrtDisc = std::sqrt(disc);
                float t = (-halfLinearCoefficient - sqrtDisc) / quadraticCoefficient;
                if (t < 0.0f)
                {
                    t = (-halfLinearCoefficient + sqrtDisc) / quadraticCoefficient;
                }

                if (t >= 0.0f)
                {
                    // ヒット点がカプセル軸の区間 [0,1] 内か確認
                    Vector3 hitPt = {
                        ray.origin.x + ray.direction.x * t,
                        ray.origin.y + ray.direction.y * t,
                        ray.origin.z + ray.direction.z * t
                    };
                    Vector3 aHit = {
                        hitPt.x - capsule.start.x,
                        hitPt.y - capsule.start.y,
                        hitPt.z - capsule.start.z
                    };
                    float proj = Dot(aHit, ab) / abLen2; // [0,1] なら胴体上

                    if (proj >= 0.0f && proj <= 1.0f)
                    {
                        // 軸上の最近接点から外向き法線を計算
                        Vector3 axisPoint = {
                            capsule.start.x + ab.x * proj,
                            capsule.start.y + ab.y * proj,
                            capsule.start.z + ab.z * proj
                        };
                        float invR = 1.0f / capsule.radius;
                        hitCyl = true;
                        cyl.hit = true;
                        cyl.distance = t;
                        cyl.point = hitPt;
                        cyl.normal = {
                            (hitPt.x - axisPoint.x) * invR,
                            (hitPt.y - axisPoint.y) * invR,
                            (hitPt.z - axisPoint.z) * invR
                        };
                    }
                }
            }
        }
    }

    // 最小 t のヒットを採用
    const RaycastResult* best = nullptr;
    if (hitA)
    {
        best = &capA;
    }
    if (hitB && (!best || capB.distance < best->distance))
    {
        best = &capB;
    }
    if (hitCyl && (!best || cyl.distance < best->distance))
    {
        best = &cyl;
    }

    if (best)
    {
        result = *best;
        return true;
    }
    return false;
}

/** @brief 形状の順に対応する表でレイキャストを実行する */
bool Collision::Raycast(const Ray& ray, const Collider& collider, RaycastResult& result)
{
    using Handler = bool (*)(const Ray&, const Collider&, RaycastResult&);
    static constexpr Handler kHandlers[] = {
        [](const Ray& ray, const Collider& collider, RaycastResult& result) {
            return Raycast(ray, collider.aabb, result);
        },
        [](const Ray& ray, const Collider& collider, RaycastResult& result) {
            return Raycast(ray, collider.sphere, result);
        },
        [](const Ray& ray, const Collider& collider, RaycastResult& result) {
            return Raycast(ray, collider.capsule, result);
        },
    };
    static_assert(std::size(kHandlers) == kColliderShapeCount);
    const auto index = static_cast<size_t>(collider.shape);
    return index < std::size(kHandlers) && kHandlers[index](ray, collider, result);
}

/** @brief 形状の組み合わせに対応する表で交差を判定する */
bool Collision::CheckCollision(const Collider& a, const Collider& b)
{
    // 行が a の形状、列が b の形状（AABB・球・カプセルの順）。片側だけの判定関数しか無い組は引数を入れ替えて呼ぶ
    using Handler = bool (*)(const Collider&, const Collider&);
    static constexpr Handler kHandlers[][kColliderShapeCount] = {
        {
            [](const Collider& a, const Collider& b) {
                return CheckCollision(a.aabb, b.aabb);
            },
            [](const Collider& a, const Collider& b) {
                return CheckCollision(b.sphere, a.aabb);
            },
            [](const Collider& a, const Collider& b) {
                return CheckCollision(b.capsule, a.aabb);
            },
        },
        {
            [](const Collider& a, const Collider& b) {
                return CheckCollision(a.sphere, b.aabb);
            },
            [](const Collider& a, const Collider& b) {
                return CheckCollision(a.sphere, b.sphere);
            },
            [](const Collider& a, const Collider& b) {
                return CheckCollision(a.sphere, b.capsule);
            },
        },
        {
            [](const Collider& a, const Collider& b) {
                return CheckCollision(a.capsule, b.aabb);
            },
            [](const Collider& a, const Collider& b) {
                return CheckCollision(b.sphere, a.capsule);
            },
            [](const Collider& a, const Collider& b) {
                return CheckCollision(a.capsule, b.capsule);
            },
        },
    };
    static_assert(std::size(kHandlers) == kColliderShapeCount);
    // 表の外の形状が来たら当たっていない扱いにする
    const auto row = static_cast<size_t>(a.shape);
    const auto column = static_cast<size_t>(b.shape);
    return row < std::size(kHandlers) && column < std::size(kHandlers[0])
        && kHandlers[row][column](a, b);
}

} // namespace engine
