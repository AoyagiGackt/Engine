// Engine/math のヘッダオンリー数学ライブラリに対する軽量ユニットテスト 仮
// 外部テストフレームワークは使わず、assert相当の比較関数と手動のテストランナーのみで構成する。
#include "MakeAffine.h"
#include <cstdio>
#include <string>

namespace {

int g_failCount = 0;
int g_testCount = 0;

constexpr float kEpsilon = 1e-4f;

bool NearlyEqual(float a, float b, float epsilon = kEpsilon)
{
    return std::fabs(a - b) <= epsilon;
}

void Check(bool condition, const std::string& testName)
{
    ++g_testCount;
    if (!condition) {
        ++g_failCount;
        std::printf("[FAIL] %s\n", testName.c_str());
    }
}

void TestVector3()
{
    Vector3 a = { 1.0f, 2.0f, 3.0f };
    Vector3 b = { 4.0f, 5.0f, 6.0f };

    Vector3 sum = a + b;
    Check(NearlyEqual(sum.x, 5.0f) && NearlyEqual(sum.y, 7.0f) && NearlyEqual(sum.z, 9.0f), "Vector3 operator+");

    Vector3 scaled = a * 2.0f;
    Check(NearlyEqual(scaled.x, 2.0f) && NearlyEqual(scaled.y, 4.0f) && NearlyEqual(scaled.z, 6.0f), "Vector3 operator*");

    Check(NearlyEqual(Length({ 3.0f, 4.0f, 0.0f }), 5.0f), "Length (3-4-5)");
    Check(NearlyEqual(Distance({ 0.0f, 0.0f, 0.0f }, { 3.0f, 4.0f, 0.0f }), 5.0f), "Distance");

    Vector3 n = Normalize({ 0.0f, 5.0f, 0.0f });
    Check(NearlyEqual(n.y, 1.0f), "Normalize");
    Vector3 zero = Normalize({ 0.0f, 0.0f, 0.0f });
    Check(NearlyEqual(zero.x, 0.0f) && NearlyEqual(zero.y, 0.0f) && NearlyEqual(zero.z, 0.0f), "Normalize(zero vector) は 0 のまま");

    Check(NearlyEqual(Dot({ 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }), 0.0f), "Dot (垂直なら0)");
    Check(NearlyEqual(Dot({ 2.0f, 0.0f, 0.0f }, { 3.0f, 0.0f, 0.0f }), 6.0f), "Dot (平行)");

    Vector3 cross = Cross({ 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f });
    Check(NearlyEqual(cross.z, 1.0f), "Cross (X×Y=Z)");

    Vector3 sub = Subtract(b, a);
    Check(NearlyEqual(sub.x, 3.0f) && NearlyEqual(sub.y, 3.0f) && NearlyEqual(sub.z, 3.0f), "Subtract");

    Vector3 mid = Lerp(a, b, 0.5f);
    Check(NearlyEqual(mid.x, 2.5f) && NearlyEqual(mid.y, 3.5f) && NearlyEqual(mid.z, 4.5f), "Lerp(t=0.5)");

    Check(NearlyEqual(Clamp(5.0f, 0.0f, 10.0f), 5.0f), "Clamp (範囲内)");
    Check(NearlyEqual(Clamp(-1.0f, 0.0f, 10.0f), 0.0f), "Clamp (下限)");
    Check(NearlyEqual(Clamp(11.0f, 0.0f, 10.0f), 10.0f), "Clamp (上限)");
}

void TestMatrix4x4()
{
    Matrix4x4 identity = MakeIdentity4x4();
    Matrix4x4 translate = MakeTranslateMatrix({ 1.0f, 2.0f, 3.0f });

    Matrix4x4 result = Multiply(identity, translate);
    Check(NearlyEqual(result.m[3][0], 1.0f) && NearlyEqual(result.m[3][1], 2.0f) && NearlyEqual(result.m[3][2], 3.0f),
        "単位行列との積は変化しない");

    Matrix4x4 scale = MakeScaleMatrix({ 2.0f, 3.0f, 4.0f });
    Check(NearlyEqual(scale.m[0][0], 2.0f) && NearlyEqual(scale.m[1][1], 3.0f) && NearlyEqual(scale.m[2][2], 4.0f),
        "MakeScaleMatrix");

    Matrix4x4 affine = MakeAffineMatrix({ 1.0f, 1.0f, 1.0f }, Vector3{ 0.0f, 0.0f, 0.0f }, { 5.0f, 6.0f, 7.0f });
    Check(NearlyEqual(affine.m[3][0], 5.0f) && NearlyEqual(affine.m[3][1], 6.0f) && NearlyEqual(affine.m[3][2], 7.0f),
        "MakeAffineMatrix (平行移動成分)");

    // 逆行列 × 元の行列 ≒ 単位行列
    Matrix4x4 m = MakeAffineMatrix({ 2.0f, 3.0f, 1.0f }, Vector3{ 0.4f, 0.1f, 0.2f }, { 10.0f, -5.0f, 2.0f });
    Matrix4x4 inv = Inverse(m);
    Matrix4x4 shouldBeIdentity = Multiply(m, inv);
    bool identityOk = true;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            float expected = (i == j) ? 1.0f : 0.0f;
            if (!NearlyEqual(shouldBeIdentity.m[i][j], expected, 1e-3f)) { identityOk = false; }
        }
    }
    Check(identityOk, "Inverse(m) * m ≒ 単位行列");

    Matrix4x4 t = Transpose(translate);
    Check(NearlyEqual(t.m[0][3], 1.0f) && NearlyEqual(t.m[1][3], 2.0f) && NearlyEqual(t.m[2][3], 3.0f), "Transpose");
}

void TestQuaternion()
{
    Quaternion q1 = { 0.0f, 0.0f, 0.0f, 1.0f };
    Quaternion q2 = { 0.0f, 1.0f, 0.0f, 0.0f };

    Quaternion atStart = Slerp(q1, q2, 0.0f);
    Check(NearlyEqual(atStart.w, 1.0f), "Slerp(t=0) は開始値と一致");

    Quaternion atEnd = Slerp(q1, q2, 1.0f);
    Check(NearlyEqual(atEnd.y, 1.0f), "Slerp(t=1) は終了値と一致");
}

void TestCollision()
{
    Vector3 min1 = { 0.0f, 0.0f, 0.0f };
    Vector3 max1 = { 1.0f, 1.0f, 1.0f };
    Vector3 min2 = { 0.5f, 0.5f, 0.5f };
    Vector3 max2 = { 1.5f, 1.5f, 1.5f };
    Check(IsCollisionAABB(min1, max1, min2, max2), "IsCollisionAABB (重なりあり)");

    Vector3 farMin = { 10.0f, 10.0f, 10.0f };
    Vector3 farMax = { 11.0f, 11.0f, 11.0f };
    Check(!IsCollisionAABB(min1, max1, farMin, farMax), "IsCollisionAABB (重なりなし)");

    Check(IsCollisionSphereAABB({ 0.5f, 0.5f, 0.5f }, 0.1f, min1, max1), "IsCollisionSphereAABB (内部)");
    Check(!IsCollisionSphereAABB({ 100.0f, 100.0f, 100.0f }, 0.1f, min1, max1), "IsCollisionSphereAABB (遠い)");

    Check(IsCollisionRaySphere({ 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 5.0f, 0.0f, 0.0f }, 1.0f),
        "IsCollisionRaySphere (命中)");
    Check(!IsCollisionRaySphere({ 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 5.0f, 0.0f }, 1.0f),
        "IsCollisionRaySphere (逸れる)");
}

} // namespace

int main()
{
    TestVector3();
    TestMatrix4x4();
    TestQuaternion();
    TestCollision();

    std::printf("\n%d / %d tests passed\n", g_testCount - g_failCount, g_testCount);
    return g_failCount == 0 ? 0 : 1;
}
