/** @file main.cpp
 * @brief 数学と当たり判定のテスト（入力と期待値）
 */
#include "MakeAffine.h"
#include "Collision.h"
#include "TestReport.h"
#include <cstddef>
#include <iterator>
#include <cstdio>
#include <source_location>
#include <string>
using namespace engine;

namespace engine::tests {
namespace {

constexpr float kInverseEpsilon = 1e-3f;
constexpr std::size_t kMatrixElementNameCapacity = 48;

// 以下のテストにある数値は、テストの入力と期待値
/** @brief ベクトルの各要素を共通の誤差比較で確かめる */
void CheckVector3(TestReport::TestResults& results, const Vector3& actual,
    const Vector3& expected, const std::string& name,
    const std::source_location& where = std::source_location::current())
{
    constexpr const char* kAxisNames[] = { "x", "y", "z" };
    const float actualAxes[] = { actual.x, actual.y, actual.z };
    const float expectedAxes[] = { expected.x, expected.y, expected.z };
    for (std::size_t axis = 0; axis < std::size(kAxisNames); ++axis)
    {
        results.CheckNear(actualAxes[axis], expectedAxes[axis], name + " (" + kAxisNames[axis] + ")",
            TestReport::TestResults::kDefaultEpsilon, where);
    }
}

/** @brief ベクトルの四則演算・正規化・幾何計算を確かめる */
void TestVector3(TestReport::TestResults& results)
{
    Vector3 a = { 1.0f, 2.0f, 3.0f };
    Vector3 b = { 4.0f, 5.0f, 6.0f };

    Vector3 sum = a + b;
    CheckVector3(results, sum, { 5.0f, 7.0f, 9.0f }, "Vector3 operator+");

    Vector3 scaled = a * 2.0f;
    CheckVector3(results, scaled, { 2.0f, 4.0f, 6.0f }, "Vector3 operator*");

    results.CheckNear(Length({ 3.0f, 4.0f, 0.0f }), 5.0f, "Length (3-4-5)");
    results.CheckNear(Distance({ 0.0f, 0.0f, 0.0f }, { 3.0f, 4.0f, 0.0f }), 5.0f, "Distance");

    Vector3 n = Normalize(Vector3 { 0.0f, 5.0f, 0.0f });
    results.CheckNear(n.y, 1.0f, "Normalize");
    Vector3 zero = Normalize(Vector3 { 0.0f, 0.0f, 0.0f });
    CheckVector3(results, zero, { 0.0f, 0.0f, 0.0f }, "Normalize(zero vector)");

    results.CheckNear(Dot({ 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }), 0.0f, "Dot (垂直なら0)");
    results.CheckNear(Dot({ 2.0f, 0.0f, 0.0f }, { 3.0f, 0.0f, 0.0f }), 6.0f, "Dot (平行)");

    Vector3 cross = Cross({ 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f });
    results.CheckNear(cross.z, 1.0f, "Cross (X×Y=Z)");

    Vector3 sub = Subtract(b, a);
    CheckVector3(results, sub, { 3.0f, 3.0f, 3.0f }, "Subtract");

    Vector3 mid = Lerp(a, b, 0.5f);
    CheckVector3(results, mid, { 2.5f, 3.5f, 4.5f }, "Lerp(t=0.5)");

    /** @brief Clamp のテストの入力と、範囲に収めた期待値 */
    struct ClampCase {
        float input;
        float lower;
        float upper;
        float expected;
        const char* name;
    };
    constexpr ClampCase kClampCases[] = {
        { 5.0f, 0.0f, 10.0f, 5.0f, "Clamp inside range" },
        { -1.0f, 0.0f, 10.0f, 0.0f, "Clamp below range" },
        { 11.0f, 0.0f, 10.0f, 10.0f, "Clamp above range" },
    };
    for (const auto& sample : kClampCases)
    {
        results.CheckNear(Clamp(sample.input, sample.lower, sample.upper), sample.expected, sample.name);
    }
}

/** @brief 行列の合成・逆行列・転置を確かめる */
void TestMatrix4x4(TestReport::TestResults& results)
{
    Matrix4x4 identity = MakeIdentity4x4();
    Matrix4x4 translate = MakeTranslateMatrix({ 1.0f, 2.0f, 3.0f });

    Matrix4x4 result = Multiply(identity, translate);
    results.CheckNear(result.m[3][0], 1.0f, "単位行列との積は変化しない (x)");
    results.CheckNear(result.m[3][1], 2.0f, "単位行列との積は変化しない (y)");
    results.CheckNear(result.m[3][2], 3.0f, "単位行列との積は変化しない (z)");

    Matrix4x4 scale = MakeScaleMatrix({ 2.0f, 3.0f, 4.0f });
    results.CheckNear(scale.m[0][0], 2.0f, "MakeScaleMatrix (x)");
    results.CheckNear(scale.m[1][1], 3.0f, "MakeScaleMatrix (y)");
    results.CheckNear(scale.m[2][2], 4.0f, "MakeScaleMatrix (z)");

    Matrix4x4 affine = MakeAffineMatrix({ 1.0f, 1.0f, 1.0f }, Vector3{ 0.0f, 0.0f, 0.0f }, { 5.0f, 6.0f, 7.0f });
    results.CheckNear(affine.m[3][0], 5.0f, "MakeAffineMatrix (平行移動成分 x)");
    results.CheckNear(affine.m[3][1], 6.0f, "MakeAffineMatrix (平行移動成分 y)");
    results.CheckNear(affine.m[3][2], 7.0f, "MakeAffineMatrix (平行移動成分 z)");

    // 逆行列 × 元の行列 ≒ 単位行列（要素ごとに比較し、崩れている成分がわかるようにする）
    Matrix4x4 m = MakeAffineMatrix({ 2.0f, 3.0f, 1.0f }, Vector3{ 0.4f, 0.1f, 0.2f }, { 10.0f, -5.0f, 2.0f });
    Matrix4x4 inv = Inverse(m);
    Matrix4x4 shouldBeIdentity = Multiply(m, inv);
    for (int i = 0; i < std::size(shouldBeIdentity.m); ++i)
    {
        for (int j = 0; j < std::size(shouldBeIdentity.m[i]); ++j)
        {
            char name[kMatrixElementNameCapacity];
            std::snprintf(name, sizeof(name), "Inverse(m) * m ≒ 単位行列 [%d][%d]", i, j);
            float expected = (i == j) ? 1.0f : 0.0f;
            results.CheckNear(shouldBeIdentity.m[i][j], expected, name, kInverseEpsilon);
        }
    }

    Matrix4x4 t = Transpose(translate);
    results.CheckNear(t.m[0][3], 1.0f, "Transpose (x)");
    results.CheckNear(t.m[1][3], 2.0f, "Transpose (y)");
    results.CheckNear(t.m[2][3], 3.0f, "Transpose (z)");
}

/** @brief クォータニオンの補間の両端を確かめる */
void TestQuaternion(TestReport::TestResults& results)
{
    Quaternion q1 = { 0.0f, 0.0f, 0.0f, 1.0f };
    Quaternion q2 = { 0.0f, 1.0f, 0.0f, 0.0f };

    Quaternion atStart = Slerp(q1, q2, 0.0f);
    results.CheckNear(atStart.w, 1.0f, "Slerp(t=0) は開始値と一致");

    Quaternion atEnd = Slerp(q1, q2, 1.0f);
    results.CheckNear(atEnd.y, 1.0f, "Slerp(t=1) は終了値と一致");
}

/** @brief 基本の形どうしの重なりとレイ判定を確かめる */
void TestCollision(TestReport::TestResults& results)
{
    Vector3 min1 = { 0.0f, 0.0f, 0.0f };
    Vector3 max1 = { 1.0f, 1.0f, 1.0f };
    Vector3 min2 = { 0.5f, 0.5f, 0.5f };
    Vector3 max2 = { 1.5f, 1.5f, 1.5f };
    results.Check(IsCollisionAABB(min1, max1, min2, max2), "IsCollisionAABB (重なりあり)");

    Vector3 farMin = { 10.0f, 10.0f, 10.0f };
    Vector3 farMax = { 11.0f, 11.0f, 11.0f };
    results.Check(!IsCollisionAABB(min1, max1, farMin, farMax), "IsCollisionAABB (重なりなし)");

    results.Check(IsCollisionSphereAABB({ 0.5f, 0.5f, 0.5f }, 0.1f, min1, max1), "IsCollisionSphereAABB (内部)");
    results.Check(!IsCollisionSphereAABB({ 100.0f, 100.0f, 100.0f }, 0.1f, min1, max1), "IsCollisionSphereAABB (遠い)");

    results.Check(IsCollisionRaySphere({ 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 5.0f, 0.0f, 0.0f }, 1.0f),
        "IsCollisionRaySphere (命中)");
    results.Check(!IsCollisionRaySphere({ 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 5.0f, 0.0f }, 1.0f),
        "IsCollisionRaySphere (逸れる)");
}

// 実際にゲーム側で使われている engine::Collision クラスの形状テスト
/** @brief 当たり判定の形と共通の判定の窓口を確かめる */
void TestCollisionShapes(TestReport::TestResults& results)
{
    // ---- 球 × 球 ----
    Sphere s1{ { 0.0f, 0.0f, 0.0f }, 1.0f };
    Sphere s2{ { 1.5f, 0.0f, 0.0f }, 1.0f };
    Sphere s3{ { 3.0f, 0.0f, 0.0f }, 1.0f };
    results.Check(Collision::CheckCollision(s1, s2), "Sphere x Sphere (重なりあり)");
    results.Check(!Collision::CheckCollision(s1, s3), "Sphere x Sphere (重なりなし)");

    // ---- AABB × AABB ----
    AABB box1{ { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f } };
    AABB box2{ { 0.5f, 0.5f, 0.5f }, { 1.5f, 1.5f, 1.5f } };
    AABB box3{ { 10.0f, 10.0f, 10.0f }, { 11.0f, 11.0f, 11.0f } };
    results.Check(Collision::CheckCollision(box1, box2), "AABB x AABB (重なりあり)");
    results.Check(!Collision::CheckCollision(box1, box3), "AABB x AABB (重なりなし)");

    // ---- 球 × AABB（両方向のオーバーロード）----
    Sphere sInBox{ { 0.5f, 0.5f, 0.5f }, 0.1f };
    Sphere sFarBox{ { 100.0f, 100.0f, 100.0f }, 0.1f };
    results.Check(Collision::CheckCollision(sInBox, box1),  "Sphere x AABB (内部)");
    results.Check(Collision::CheckCollision(box1, sInBox),  "AABB x Sphere (引数順の逆でも同じ結果)");
    results.Check(!Collision::CheckCollision(sFarBox, box1), "Sphere x AABB (遠い)");

    // ---- 球 × カプセル ----
    Capsule vertCap{ { 0.0f, 0.0f, 0.0f }, { 0.0f, 2.0f, 0.0f }, 0.5f };
    Sphere sOnAxis{ { 0.0f, 1.0f, 0.0f }, 0.3f };
    Sphere sFarAxis{ { 5.0f, 1.0f, 0.0f }, 0.3f };
    results.Check(Collision::CheckCollision(sOnAxis, vertCap),  "Sphere x Capsule (軸上)");
    results.Check(!Collision::CheckCollision(sFarAxis, vertCap), "Sphere x Capsule (遠い)");

    // ---- カプセル × カプセル ----
    Capsule crossA{ { 0.0f, 0.0f, 0.0f }, { 2.0f, 2.0f, 0.0f }, 0.3f };
    Capsule crossB{ { 0.0f, 2.0f, 0.0f }, { 2.0f, 0.0f, 0.0f }, 0.3f };
    Capsule farCap{ { 10.0f, 0.0f, 0.0f }, { 12.0f, 0.0f, 0.0f }, 0.3f };
    results.Check(Collision::CheckCollision(crossA, crossB), "Capsule x Capsule (交差)");
    results.Check(!Collision::CheckCollision(crossA, farCap), "Capsule x Capsule (遠い)");

    // ---- カプセル × AABB ----
    Capsule throughBox{ { 0.0f, -2.0f, 0.0f }, { 0.0f, 2.0f, 0.0f }, 0.5f };
    AABB centerBox{ { -0.5f, -0.5f, -0.5f }, { 0.5f, 0.5f, 0.5f } };
    results.Check(Collision::CheckCollision(throughBox, centerBox), "Capsule x AABB (貫通)");
    results.Check(!Collision::CheckCollision(farCap, centerBox),    "Capsule x AABB (遠い)");

    // ---- Collider 経由の形状ディスパッチ ----
    Collider colliderSphere; colliderSphere.SetAsSphere(sInBox);
    Collider colliderAABB;   colliderAABB.SetAsAABB(box1);
    Collider colliderCapA;   colliderCapA.SetAsCapsule(crossA);
    Collider colliderCapB;   colliderCapB.SetAsCapsule(crossB);
    results.Check(Collision::CheckCollision(colliderSphere, colliderAABB) == Collision::CheckCollision(sInBox, box1),
        "Collider dispatch (Sphere x AABB) は直接呼び出しと一致");
    results.Check(Collision::CheckCollision(colliderCapA, colliderCapB) == Collision::CheckCollision(crossA, crossB),
        "Collider dispatch (Capsule x Capsule) は直接呼び出しと一致");

    // ---- レイキャスト: Ray x AABB ----
    Ray rayHitBox{ { -5.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
    RaycastResult rrBox;
    results.Check(Collision::Raycast(rayHitBox, box1, rrBox), "Raycast vs AABB (命中)");
    results.CheckNear(rrBox.point.x, 0.0f, "Raycast vs AABB (ヒット点のX座標)");

    Ray rayMissBox{ { -5.0f, 5.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
    RaycastResult rrMiss;
    results.Check(!Collision::Raycast(rayMissBox, box1, rrMiss), "Raycast vs AABB (逸れる)");

    // tmin は 0.0f から始まり負にはならないため、内部始点は distance 0 の命中として扱われる
    Ray rayInsideBox{ { 0.5f, 0.5f, 0.5f }, { 1.0f, 0.0f, 0.0f } };
    RaycastResult rrInside;
    results.Check(Collision::Raycast(rayInsideBox, box1, rrInside), "Raycast vs AABB (内部始点は distance 0 で命中)");
    results.CheckNear(rrInside.distance, 0.0f, "Raycast vs AABB (内部始点の distance は 0)");

    // ---- レイキャスト: Ray x 球 ----
    Ray rayHitSphere{ { -5.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
    RaycastResult rrSphere;
    results.Check(Collision::Raycast(rayHitSphere, s1, rrSphere), "Raycast vs Sphere (外側から命中)");

    Ray rayInSphere{ { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
    RaycastResult rrInSphere;
    results.Check(Collision::Raycast(rayInSphere, s1, rrInSphere), "Raycast vs Sphere (内部始点は奥側で命中)");

    Ray rayMissSphere{ { -5.0f, 5.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
    RaycastResult rrMissSphere;
    results.Check(!Collision::Raycast(rayMissSphere, s1, rrMissSphere), "Raycast vs Sphere (逸れる)");

    // ---- レイキャスト: Ray x カプセル ----
    Ray rayHitCylinder{ { -5.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
    RaycastResult rrCyl;
    results.Check(Collision::Raycast(rayHitCylinder, vertCap, rrCyl), "Raycast vs Capsule (胴体に命中)");

    Ray rayHitCap{ { -5.0f, 2.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
    RaycastResult rrCap;
    results.Check(Collision::Raycast(rayHitCap, vertCap, rrCap), "Raycast vs Capsule (端の球に命中)");

    Ray rayMissCapsule{ { -5.0f, 10.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
    RaycastResult rrMissCap;
    results.Check(!Collision::Raycast(rayMissCapsule, vertCap, rrMissCap), "Raycast vs Capsule (逸れる)");

    // ---- レイキャスト: Collider 経由の形状ディスパッチ ----
    Collider colliderS1; colliderS1.SetAsSphere(s1);
    RaycastResult rrDirect, rrDispatch;
    Collision::Raycast(rayHitSphere, s1, rrDirect);
    Collision::Raycast(rayHitSphere, colliderS1, rrDispatch);
    results.CheckNear(rrDispatch.distance, rrDirect.distance, "Raycast dispatch (Sphere) は直接呼び出しと一致");
}

} // namespace

/** @brief シーン遷移とHUDの構造のテストを実行する */
int RunArchitectureTests();

/** @brief 登録した数学のテストをすべて実行し、失敗の数を返す */
int RunMathTests()
{
    /** @brief テストの区切りの名前と、実行するテスト関数の組 */
    struct TestSection {
        const char* name;
        void (*run)(TestReport::TestResults&);
    };
    constexpr TestSection kSections[] = {
        { "Vector3", TestVector3 },
        { "Matrix4x4", TestMatrix4x4 },
        { "Quaternion", TestQuaternion },
        { "Collision (Math functions)", TestCollision },
        { "Collision (Sphere/AABB/Capsule/Raycast)", TestCollisionShapes },
    };
    TestReport::TestResults results;
    for (const auto& section : kSections)
    {
        results.RunSection(section.name, section.run);
    }
    results.PrintSummary("Mathematics");
    return results.FailureCount();
}
} // namespace engine::tests

/** @brief 両方のテストを実行し、失敗があれば終了コードで知らせる */
int main()
{
    const int mathFailures = engine::tests::RunMathTests();
    const int architectureFailures = engine::tests::RunArchitectureTests();
    return (mathFailures == 0 && architectureFailures == 0) ? 0 : 1;
}
