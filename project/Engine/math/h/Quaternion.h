/**
 * @file Quaternion.h
 * @brief クォータニオン（回転を表す四元数）とその補間関数を定義するファイル
 */
#pragma once
#include "Vector3.h"
#include <cmath>

namespace engine {

/** @brief クォータニオン（四元数）回転をあらわす */
struct Quaternion {
    float x;
    float y;
    float z;
    float w;
};

/**
 * @brief オイラー角（XYZ順、ラジアン）からクォータニオンを作る
 * @note X回転→Y回転→Z回転の順に適用される合成（q = qz * qy * qx）
 */
inline Quaternion MakeRotateXYZQuaternion(const Vector3& eulerRadians) {
    float cx = std::cos(eulerRadians.x * 0.5f), sx = std::sin(eulerRadians.x * 0.5f);
    float cy = std::cos(eulerRadians.y * 0.5f), sy = std::sin(eulerRadians.y * 0.5f);
    float cz = std::cos(eulerRadians.z * 0.5f), sz = std::sin(eulerRadians.z * 0.5f);

    Quaternion qx { sx, 0.0f, 0.0f, cx };
    Quaternion qy { 0.0f, sy, 0.0f, cy };
    Quaternion qz { 0.0f, 0.0f, sz, cz };

    auto mul = [](const Quaternion& a, const Quaternion& b) -> Quaternion {
        return {
            a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z
        };
    };
    // 合成順序は @note の通り qz * qy * qx（qxを最初に、qzを最後に適用）
    return mul(mul(qz, qy), qx);
}

/**
 * @brief クォータニオンの球面線形補間 (Slerp)
 * @param q1 開始クォータニオン
 * @param target 終了クォータニオン
 * @param t  補間係数 (0.0f ~ 1.0f)
 */
inline Quaternion Slerp(const Quaternion& q1, const Quaternion& target, float t) {
    Quaternion q2 = target;
    float dot = q1.x * q2.x + q1.y * q2.y + q1.z * q2.z + q1.w * q2.w;

    // 内積が負なら片方を反転して最短経路を取る
    if (dot < 0.0f) {
        q2 = { -q2.x, -q2.y, -q2.z, -q2.w };
        dot = -dot;
    }

    // 角度がほぼゼロなら線形補間で代用
    constexpr float kLinearInterpolationThreshold = 0.9995f;
    if (dot > kLinearInterpolationThreshold) {
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

/**
 * @brief クォータニオンの積（ハミルトン積）
 * @note 結果は q2 の回転を先に、q1 の回転を後に適用する合成になる（MakeRotateXYZQuaternion と同じ規約）
 */
inline Quaternion Multiply(const Quaternion& q1, const Quaternion& q2) {
    return {
        q1.w * q2.x + q1.x * q2.w + q1.y * q2.z - q1.z * q2.y,
        q1.w * q2.y - q1.x * q2.z + q1.y * q2.w + q1.z * q2.x,
        q1.w * q2.z + q1.x * q2.y - q1.y * q2.x + q1.z * q2.w,
        q1.w * q2.w - q1.x * q2.x - q1.y * q2.y - q1.z * q2.z
    };
}

/** @brief 長さ1に正規化したクォータニオンを返す（長さがほぼ0なら単位回転を返す） */
inline Quaternion Normalize(const Quaternion& q) {
    constexpr float kMinLength = 1.0e-6f;
    const float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (len < kMinLength) {
        return { 0.0f, 0.0f, 0.0f, 1.0f };
    }
    return { q.x / len, q.y / len, q.z / len, q.w / len };
}

/** @brief 逆向きの回転（単位クォータニオンの共役）を返す */
inline Quaternion Conjugate(const Quaternion& q) {
    return { -q.x, -q.y, -q.z, q.w };
}

/** @brief ベクトルを単位クォータニオンで回す（q * v * q の共役と同じ） */
inline Vector3 RotateVector(const Quaternion& q, const Vector3& v) {
    // v + 2w(u×v) + 2u×(u×v)（u はクォータニオンのベクトル部）で、行列を作らずに回す
    const Vector3 u = { q.x, q.y, q.z };
    const Vector3 uv = Cross(u, v);
    const Vector3 uuv = Cross(u, uv);
    return {
        v.x + 2.0f * (q.w * uv.x + uuv.x),
        v.y + 2.0f * (q.w * uv.y + uuv.y),
        v.z + 2.0f * (q.w * uv.z + uuv.z)
    };
}

/**
 * @brief from の向きを to の向きへ最短で回すクォータニオンを作る
 * @note どちらかの長さがほぼ0なら単位回転。真逆の向きの時は from に垂直な軸で半回転する
 */
inline Quaternion MakeRotationFromTo(const Vector3& from, const Vector3& to) {
    constexpr float kMinLength = 1.0e-6f;
    constexpr float kOppositeThreshold = -0.9999f;
    if (Length(from) < kMinLength || Length(to) < kMinLength) {
        return { 0.0f, 0.0f, 0.0f, 1.0f };
    }
    const Vector3 a = Normalize(from);
    const Vector3 b = Normalize(to);
    const float dot = Dot(a, b);
    if (dot < kOppositeThreshold) {
        // 真逆なら外積が決まらないので、from と垂直な軸（X軸と平行に近ければY軸から作る）で180度回す
        const Vector3 helper = std::fabs(a.x) < 0.5f ? Vector3 { 1.0f, 0.0f, 0.0f } : Vector3 { 0.0f, 1.0f, 0.0f };
        const Vector3 axis = Normalize(Cross(a, helper));
        return { axis.x, axis.y, axis.z, 0.0f };
    }
    // 半角の公式: 軸は外積、w は 1 + cos で、最後に正規化して長さを揃える
    const Vector3 axis = Cross(a, b);
    return Normalize(Quaternion { axis.x, axis.y, axis.z, 1.0f + dot });
}

} // namespace engine
