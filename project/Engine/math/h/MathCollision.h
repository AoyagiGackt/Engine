/**
 * @file MathCollision.h
 * @brief 球・AABB・レイなど、基本図形どうしの当たり判定関数を定義するファイル
 */
#pragma once
#include "Vector3.h"

/**
 * @brief 球体とAABB（箱）の当たり判定
 * @param spherePos 球体の中心座標
 * @param radius 球体の半径
 * @param aabbMin AABBの最小座標
 * @param aabbMax AABBの最大座標
 * @return bool 接触していれば true
 */
inline bool IsCollisionSphereAABB(const Vector3 &spherePos, float radius, const Vector3 &aabbMin, const Vector3 &aabbMax)
{
    // AABB上で、球の中心に最も近い点を探す
    Vector3 closestPoint = {
        Clamp(spherePos.x, aabbMin.x, aabbMax.x),
        Clamp(spherePos.y, aabbMin.y, aabbMax.y),
        Clamp(spherePos.z, aabbMin.z, aabbMax.z)
    };

    // その最も近い点と、球の中心との距離を測る
    float dist = Distance(spherePos, closestPoint);

    // 距離が半径以下なら当たっている
    return dist <= radius;
}

/**
 * @brief AABB（軸並行境界箱）同士の当たり判定
 * @param min1 物体1の最小座標（左下奥）
 * @param max1 物体1の最大座標（右上おもて）
 * @param min2 物体2の最小座標
 * @param max2 物体2の最大座標
 * @return bool 接触していれば true
 */
inline bool IsCollisionAABB(const Vector3 &min1, const Vector3 &max1, const Vector3 &min2, const Vector3 &max2)
{
    // 1つでも重なっていない軸があれば当たっていない
    if (max1.x < min2.x || min1.x > max2.x) {
        return false;
    }
    if (max1.y < min2.y || min1.y > max2.y) {
        return false;
    }
    if (max1.z < min2.z || min1.z > max2.z) {
        return false;
    }

    // 全ての軸で重なっていれば当たっている
    return true;
}

/**
 * @brief レイ（半直線）と球体の当たり判定
 * @param rayOrigin レイの発射地点
 * @param rayDirection レイの飛んでいく方向（必ずNormalizeしてないとバグ）
 * @param spherePos 球の中心座標
 * @param radius 球の半径
 * @return bool レイが球に当たっていれば true
 */
inline bool IsCollisionRaySphere(const Vector3 &rayOrigin, const Vector3 &rayDirection, const Vector3 &spherePos, float radius)
{
    // レイの始点から球の中心へのベクトル
    Vector3 m = { spherePos.x - rayOrigin.x, spherePos.y - rayOrigin.y, spherePos.z - rayOrigin.z };

    // レイの方向ベクトルへの射影（内積）
    float b = Dot(m, rayDirection);

    // 球の中心からレイの始点までの距離の2乗
    float c = Dot(m, m) - radius * radius;

    // 始点が球の外側にあり、かつレイが球と反対方向を向いている場合は当たらない
    if (c > 0.0f && b <= 0.0f) {
        return false;
    }

    // 判別式
    float discr = b * b - c;

    // 判別式が負ならレイは球をかすりもしない
    if (discr < 0.0f) {
        return false;
    }

    return true;
}
