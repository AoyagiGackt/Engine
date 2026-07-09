/**
 * @file Quaternion.h
 * @brief クォータニオン（回転を表す四元数）とその補間関数を定義するファイル
 */
#pragma once
#include "Vector3.h"
#include <cmath>

/** @brief クォータニオン（四元数）回転をあらわす */
struct Quaternion
{
    float x;
    float y;
    float z;
    float w;
};

/**
 * @brief オイラー角（XYZ順、ラジアン）からクォータニオンを作る
 * @note X回転→Y回転→Z回転の順に適用される合成（q = qz * qy * qx）
 */
inline Quaternion MakeRotateXYZQuaternion(const Vector3& eulerRadians)
{
    float cx = std::cos(eulerRadians.x * 0.5f), sx = std::sin(eulerRadians.x * 0.5f);
    float cy = std::cos(eulerRadians.y * 0.5f), sy = std::sin(eulerRadians.y * 0.5f);
    float cz = std::cos(eulerRadians.z * 0.5f), sz = std::sin(eulerRadians.z * 0.5f);

    Quaternion qx{ sx, 0.0f, 0.0f, cx };
    Quaternion qy{ 0.0f, sy, 0.0f, cy };
    Quaternion qz{ 0.0f, 0.0f, sz, cz };

    auto mul = [](const Quaternion& a, const Quaternion& b) -> Quaternion {
        return {
            a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z
        };
    };
    return mul(mul(qz, qy), qx);
}

/**
 * @brief クォータニオンの球面線形補間 (Slerp)
 * @param q1 開始クォータニオン
 * @param q2 終了クォータニオン
 * @param t  補間係数 (0.0f ~ 1.0f)
 */
inline Quaternion Slerp(const Quaternion &q1, Quaternion q2, float t)
{
    float dot = q1.x * q2.x + q1.y * q2.y + q1.z * q2.z + q1.w * q2.w;

    // 内積が負なら片方を反転して最短経路を取る
    if (dot < 0.0f) {
        q2 = { -q2.x, -q2.y, -q2.z, -q2.w };
        dot = -dot;
    }

    // 角度がほぼゼロなら線形補間で代用
    if (dot > 0.9995f) {
        return {
            q1.x + t * (q2.x - q1.x),
            q1.y + t * (q2.y - q1.y),
            q1.z + t * (q2.z - q1.z),
            q1.w + t * (q2.w - q1.w)
        };
    }

    float theta0 = std::acos(dot);
    float theta = theta0 * t;
    float sinTheta = std::sin(theta);
    float sinTheta0 = std::sin(theta0);

    float s1 = std::cos(theta) - dot * sinTheta / sinTheta0;
    float s2 = sinTheta / sinTheta0;

    return {
        s1 * q1.x + s2 * q2.x,
        s1 * q1.y + s2 * q2.y,
        s1 * q1.z + s2 * q2.z,
        s1 * q1.w + s2 * q2.w
    };
}
