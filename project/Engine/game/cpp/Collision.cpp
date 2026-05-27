#include "Collision.h"
#include <algorithm>
#include <cmath>
#include <cfloat> // FLT_MAX

// ==============================================================
// 既存：球 × 球
// ==============================================================
bool Collision::CheckCollision(const Sphere& s1, const Sphere& s2)
{
    float dx = s2.center.x - s1.center.x;
    float dy = s2.center.y - s1.center.y;
    float dz = s2.center.z - s1.center.z;
    float distSq    = dx*dx + dy*dy + dz*dz;
    float radiusSum = s1.radius + s2.radius;
    return distSq <= radiusSum * radiusSum;
}

// ==============================================================
// 既存：AABB × AABB
// ==============================================================
bool Collision::CheckCollision(const AABB& a, const AABB& b)
{
    return (a.min.x <= b.max.x && a.max.x >= b.min.x)
        && (a.min.y <= b.max.y && a.max.y >= b.min.y)
        && (a.min.z <= b.max.z && a.max.z >= b.min.z);
}

// ==============================================================
// 内部ユーティリティ
// ==============================================================

Vector3 Collision::ClosestPointOnAABB(const Vector3& p, const AABB& b)
{
    return {
        std::max(b.min.x, std::min(p.x, b.max.x)),
        std::max(b.min.y, std::min(p.y, b.max.y)),
        std::max(b.min.z, std::min(p.z, b.max.z))
    };
}

Vector3 Collision::ClosestPointOnSegment(const Vector3& p,
                                          const Vector3& a, const Vector3& b)
{
    Vector3 ab   = { b.x - a.x, b.y - a.y, b.z - a.z };
    float   len2 = ab.x*ab.x + ab.y*ab.y + ab.z*ab.z;
    if (len2 < 1e-10f) { return a; }
    float t = ((p.x - a.x)*ab.x + (p.y - a.y)*ab.y + (p.z - a.z)*ab.z) / len2;
    t = std::max(0.0f, std::min(1.0f, t));
    return { a.x + ab.x * t, a.y + ab.y * t, a.z + ab.z * t };
}

// 線分 [p0,p1] と線分 [q0,q1] の最近傍距離²
// Christer Ericson "Real-Time Collision Detection" §5.1.9 に基づく
float Collision::SegmentToSegmentDistSq(const Vector3& p0, const Vector3& p1,
                                         const Vector3& q0, const Vector3& q1,
                                         float& outS, float& outT)
{
    constexpr float kEps = 1e-10f;

    Vector3 d1 = { p1.x-p0.x, p1.y-p0.y, p1.z-p0.z };
    Vector3 d2 = { q1.x-q0.x, q1.y-q0.y, q1.z-q0.z };
    Vector3 r  = { p0.x-q0.x, p0.y-q0.y, p0.z-q0.z };

    float a = d1.x*d1.x + d1.y*d1.y + d1.z*d1.z; // |d1|²
    float e = d2.x*d2.x + d2.y*d2.y + d2.z*d2.z; // |d2|²
    float f = d2.x*r.x  + d2.y*r.y  + d2.z*r.z;

    float s, t;
    if (a <= kEps && e <= kEps) {
        // 両線分とも点に縮退
        s = t = 0.0f;
    } else if (a <= kEps) {
        // p 側が点に縮退
        s = 0.0f;
        t = std::max(0.0f, std::min(1.0f, f / e));
    } else {
        float c = d1.x*r.x + d1.y*r.y + d1.z*r.z;
        if (e <= kEps) {
            // q 側が点に縮退
            t = 0.0f;
            s = std::max(0.0f, std::min(1.0f, -c / a));
        } else {
            // 一般ケース
            float b     = d1.x*d2.x + d1.y*d2.y + d1.z*d2.z;
            float denom = a*e - b*b;
            if (std::abs(denom) > kEps) {
                s = std::max(0.0f, std::min(1.0f, (b*f - c*e) / denom));
            } else {
                s = 0.0f; // 平行：任意の点を選ぶ
            }
            t = (b*s + f) / e;
            // t がクランプされた場合は s を再計算
            if (t < 0.0f) {
                t = 0.0f;
                s = std::max(0.0f, std::min(1.0f, -c / a));
            } else if (t > 1.0f) {
                t = 1.0f;
                s = std::max(0.0f, std::min(1.0f, (b - c) / a));
            }
        }
    }

    outS = s;
    outT = t;

    Vector3 c1 = { p0.x + d1.x*s, p0.y + d1.y*s, p0.z + d1.z*s };
    Vector3 c2 = { q0.x + d2.x*t, q0.y + d2.y*t, q0.z + d2.z*t };
    float dx = c1.x - c2.x, dy = c1.y - c2.y, dz = c1.z - c2.z;
    return dx*dx + dy*dy + dz*dz;
}

// ==============================================================
// 追加：球 × AABB
// ==============================================================
bool Collision::CheckCollision(const Sphere& s, const AABB& b)
{
    // AABB 上で球の中心に最も近い点を求め、距離と半径を比較
    Vector3 closest = ClosestPointOnAABB(s.center, b);
    float dx = s.center.x - closest.x;
    float dy = s.center.y - closest.y;
    float dz = s.center.z - closest.z;
    return dx*dx + dy*dy + dz*dz <= s.radius * s.radius;
}

// ==============================================================
// 追加：球 × カプセル
// ==============================================================
bool Collision::CheckCollision(const Sphere& s, const Capsule& c)
{
    // カプセル軸上で球中心に最も近い点を求め、（球半径 + カプセル半径）と比較
    Vector3 closest = ClosestPointOnSegment(s.center, c.start, c.end);
    float dx = s.center.x - closest.x;
    float dy = s.center.y - closest.y;
    float dz = s.center.z - closest.z;
    float r = s.radius + c.radius;
    return dx*dx + dy*dy + dz*dz <= r * r;
}

// ==============================================================
// 追加：カプセル × カプセル
// ==============================================================
bool Collision::CheckCollision(const Capsule& c1, const Capsule& c2)
{
    // 線分同士の最近傍距離²と（半径の和）²を比較
    float s, t;
    float distSq = SegmentToSegmentDistSq(c1.start, c1.end,
                                           c2.start, c2.end, s, t);
    float r = c1.radius + c2.radius;
    return distSq <= r * r;
}

// ==============================================================
// 追加：カプセル × AABB（交互射影法による近似）
//
// アルゴリズム:
//   1. AABB 中心 → カプセル軸上の最近傍点 P を求める
//   2. P → AABB 上の最近傍点 Q を求める
//   3. Q → カプセル軸上の最近傍点 R を求める（精度向上）
//   4. |R - Q| < radius なら衝突
// ==============================================================
bool Collision::CheckCollision(const Capsule& c, const AABB& b)
{
    // Step 1: AABB 中心からカプセル軸への射影
    Vector3 center = { (b.min.x + b.max.x) * 0.5f,
                       (b.min.y + b.max.y) * 0.5f,
                       (b.min.z + b.max.z) * 0.5f };
    Vector3 segPt  = ClosestPointOnSegment(center, c.start, c.end);

    // Step 2: セグメント最近傍点から AABB への射影
    Vector3 aabbPt = ClosestPointOnAABB(segPt, b);

    // Step 3: AABB 最近傍点からセグメントへの射影（精度向上）
    Vector3 finalSeg = ClosestPointOnSegment(aabbPt, c.start, c.end);

    float dx = finalSeg.x - aabbPt.x;
    float dy = finalSeg.y - aabbPt.y;
    float dz = finalSeg.z - aabbPt.z;
    return dx*dx + dy*dy + dz*dz <= c.radius * c.radius;
}

// ==============================================================
// レイキャスト: Ray vs AABB（スラブ法）
// ==============================================================
bool Collision::Raycast(const Ray& ray, const AABB& aabb, RaycastResult& result)
{
    result = {};
    float tmin = 0.0f;
    float tmax = FLT_MAX;
    Vector3 hitNormal = {};

    // ---- X 軸 ----
    {
        float d = ray.direction.x;
        if (std::abs(d) < 1e-8f) {
            // レイが X 方向に平行: X 範囲外なら当たらない
            if (ray.origin.x < aabb.min.x || ray.origin.x > aabb.max.x) { return false; }
        } else {
            float ood = 1.0f / d;
            float t1  = (aabb.min.x - ray.origin.x) * ood;
            float t2  = (aabb.max.x - ray.origin.x) * ood;
            Vector3 n1 = { -1.0f,  0.0f, 0.0f }; // min 面の外向き法線
            Vector3 n2 = {  1.0f,  0.0f, 0.0f }; // max 面の外向き法線
            if (t1 > t2) { std::swap(t1, t2); std::swap(n1, n2); }
            if (t1 > tmin) { tmin = t1; hitNormal = n1; }
            tmax = std::min(tmax, t2);
            if (tmin > tmax) { return false; }
        }
    }

    // ---- Y 軸 ----
    {
        float d = ray.direction.y;
        if (std::abs(d) < 1e-8f) {
            if (ray.origin.y < aabb.min.y || ray.origin.y > aabb.max.y) { return false; }
        } else {
            float ood = 1.0f / d;
            float t1  = (aabb.min.y - ray.origin.y) * ood;
            float t2  = (aabb.max.y - ray.origin.y) * ood;
            Vector3 n1 = { 0.0f, -1.0f,  0.0f };
            Vector3 n2 = { 0.0f,  1.0f,  0.0f };
            if (t1 > t2) { std::swap(t1, t2); std::swap(n1, n2); }
            if (t1 > tmin) { tmin = t1; hitNormal = n1; }
            tmax = std::min(tmax, t2);
            if (tmin > tmax) { return false; }
        }
    }

    // ---- Z 軸 ----
    {
        float d = ray.direction.z;
        if (std::abs(d) < 1e-8f) {
            if (ray.origin.z < aabb.min.z || ray.origin.z > aabb.max.z) { return false; }
        } else {
            float ood = 1.0f / d;
            float t1  = (aabb.min.z - ray.origin.z) * ood;
            float t2  = (aabb.max.z - ray.origin.z) * ood;
            Vector3 n1 = { 0.0f, 0.0f, -1.0f };
            Vector3 n2 = { 0.0f, 0.0f,  1.0f };
            if (t1 > t2) { std::swap(t1, t2); std::swap(n1, n2); }
            if (t1 > tmin) { tmin = t1; hitNormal = n1; }
            tmax = std::min(tmax, t2);
            if (tmin > tmax) { return false; }
        }
    }

    // tmin < 0 はレイ原点が AABB 内部 → 衝突なし扱い（必要に応じてゲーム側で対応）
    if (tmin < 0.0f) { return false; }

    result.hit      = true;
    result.distance = tmin;
    result.point    = {
        ray.origin.x + ray.direction.x * tmin,
        ray.origin.y + ray.direction.y * tmin,
        ray.origin.z + ray.direction.z * tmin
    };
    result.normal = hitNormal;
    return true;
}

// ==============================================================
// レイキャスト: Ray vs 球体（二次方程式）
// ==============================================================
bool Collision::Raycast(const Ray& ray, const Sphere& sphere, RaycastResult& result)
{
    result = {};
    // oc = ray.origin - sphere.center
    Vector3 oc = {
        ray.origin.x - sphere.center.x,
        ray.origin.y - sphere.center.y,
        ray.origin.z - sphere.center.z
    };

    // ray.direction は正規化済みを仮定: dot(d,d) = 1
    float b    = Dot(oc, ray.direction);          // b/2 係数
    float c    = Dot(oc, oc) - sphere.radius * sphere.radius;
    float disc = b * b - c;                       // 判別式 = (b/2)² - ac (a=1)

    if (disc < 0.0f) { return false; }

    float sqrtDisc = std::sqrt(disc);
    float t = -b - sqrtDisc; // 手前側の交点
    if (t < 0.0f) {
        t = -b + sqrtDisc;   // レイ原点が球内部: 奥側を使う
        if (t < 0.0f) { return false; }
    }

    result.hit      = true;
    result.distance = t;
    result.point    = {
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

// ==============================================================
// レイキャスト: Ray vs カプセル（無限円柱 + 端球）
//
// アルゴリズム概要:
//   1. カプセル軸方向の垂直成分だけで二次方程式を立て円柱交点を求める
//   2. 軸方向に [0, 1] にクリップして胴体判定
//   3. 端球（start/end の Sphere）とも判定し最小 t を採用
// ==============================================================
bool Collision::Raycast(const Ray& ray, const Capsule& capsule, RaycastResult& result)
{
    result = {};

    // ---- 端球判定 ----
    RaycastResult capA, capB;
    bool hitA = Raycast(ray, Sphere{ capsule.start, capsule.radius }, capA);
    bool hitB = Raycast(ray, Sphere{ capsule.end,   capsule.radius }, capB);

    // ---- 円柱胴体判定 ----
    RaycastResult cyl;
    bool hitCyl = false;

    Vector3 ab = {
        capsule.end.x - capsule.start.x,
        capsule.end.y - capsule.start.y,
        capsule.end.z - capsule.start.z
    };
    float abLen2 = ab.x*ab.x + ab.y*ab.y + ab.z*ab.z;

    if (abLen2 > 1e-10f) {
        Vector3 ao = {
            ray.origin.x - capsule.start.x,
            ray.origin.y - capsule.start.y,
            ray.origin.z - capsule.start.z
        };

        // ray.direction は正規化済みなので dot(D,D) = 1
        float dAB  = Dot(ray.direction, ab);  // D·AB
        float aoAB = Dot(ao, ab);             // AO·AB
        float dAo  = Dot(ray.direction, ao);  // D·AO

        // 垂直成分の二次方程式係数
        // A·t² + 2B·t + C = 0  (A = D_perp², B/2 = D_perp·AO_perp, C = |AO_perp|² - r²)
        float A = 1.0f   - dAB  * dAB  / abLen2;
        float B = dAo    - aoAB * dAB  / abLen2;    // 係数 b/2
        float C = Dot(ao, ao) - aoAB * aoAB / abLen2 - capsule.radius * capsule.radius;

        if (std::abs(A) > 1e-10f) {
            float disc = B * B - A * C;
            if (disc >= 0.0f) {
                float sqrtDisc = std::sqrt(disc);
                float t = (-B - sqrtDisc) / A;
                if (t < 0.0f) { t = (-B + sqrtDisc) / A; }

                if (t >= 0.0f) {
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

                    if (proj >= 0.0f && proj <= 1.0f) {
                        // 軸上の最近接点から外向き法線を計算
                        Vector3 axisPoint = {
                            capsule.start.x + ab.x * proj,
                            capsule.start.y + ab.y * proj,
                            capsule.start.z + ab.z * proj
                        };
                        float invR = 1.0f / capsule.radius;
                        hitCyl = true;
                        cyl.hit      = true;
                        cyl.distance = t;
                        cyl.point    = hitPt;
                        cyl.normal   = {
                            (hitPt.x - axisPoint.x) * invR,
                            (hitPt.y - axisPoint.y) * invR,
                            (hitPt.z - axisPoint.z) * invR
                        };
                    }
                }
            }
        }
    }

    // ---- 最小 t のヒットを採用 ----
    const RaycastResult* best = nullptr;
    if (hitA)   { best = &capA; }
    if (hitB   && (!best || capB.distance < best->distance)) { best = &capB; }
    if (hitCyl && (!best || cyl.distance  < best->distance)) { best = &cyl;  }

    if (best) {
        result = *best;
        return true;
    }
    return false;
}

// ==============================================================
// レイキャスト: 形状ディスパッチ
// ==============================================================
bool Collision::Raycast(const Ray& ray, const Collider& collider, RaycastResult& result)
{
    switch (collider.shape) {
    case ColliderShape::AABB:    return Raycast(ray, collider.aabb,    result);
    case ColliderShape::Sphere:  return Raycast(ray, collider.sphere,  result);
    case ColliderShape::Capsule: return Raycast(ray, collider.capsule, result);
    }
    return false;
}

// ==============================================================
// 形状ディスパッチ
// ==============================================================
bool Collision::CheckCollision(const Collider& a, const Collider& b)
{
    using S = ColliderShape;
    switch (a.shape) {
    case S::AABB:
        switch (b.shape) {
        case S::AABB:    return CheckCollision(a.aabb,    b.aabb);
        case S::Sphere:  return CheckCollision(b.sphere,  a.aabb);
        case S::Capsule: return CheckCollision(b.capsule, a.aabb);
        }
        break;
    case S::Sphere:
        switch (b.shape) {
        case S::AABB:    return CheckCollision(a.sphere, b.aabb);
        case S::Sphere:  return CheckCollision(a.sphere, b.sphere);
        case S::Capsule: return CheckCollision(a.sphere, b.capsule);
        }
        break;
    case S::Capsule:
        switch (b.shape) {
        case S::AABB:    return CheckCollision(a.capsule, b.aabb);
        case S::Sphere:  return CheckCollision(b.sphere,  a.capsule);
        case S::Capsule: return CheckCollision(a.capsule, b.capsule);
        }
        break;
    }
    return false;
}
