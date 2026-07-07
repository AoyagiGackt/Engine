// Engine/math のヘッダオンリー数学ライブラリに対する軽量ユニットテスト 仮
// 外部テストフレームワークは使わず、assert相当の比較関数と手動のテストランナーのみで構成する
#include "MakeAffine.h"
#include "Collision.h"
#include <cstdio>
#include <string>
#include <vector>
#include <Windows.h>
using namespace engine;

namespace {

int g_failCount = 0;
int g_testCount = 0;
std::vector<std::string> g_failedNames;

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
        g_failedNames.push_back(testName);
        std::printf("[FAIL] %s\n", testName.c_str());
    }
}

// 数値比較用。失敗時に期待値と実測値を表示する
void CheckNear(float actual, float expected, const std::string& testName, float epsilon = kEpsilon)
{
    ++g_testCount;
    if (!NearlyEqual(actual, expected, epsilon)) {
        ++g_failCount;
        g_failedNames.push_back(testName);
        std::printf("[FAIL] %s (expected %.4f, got %.4f)\n", testName.c_str(), expected, actual);
    }
}

// テスト関数をカテゴリ単位で実行し、区切りと通過数を表示する
void RunSection(const char* name, void(*fn)())
{
    std::printf("\n-- %s --\n", name);
    int beforeTotal = g_testCount;
    int beforeFail  = g_failCount;
    fn();
    int total = g_testCount - beforeTotal;
    int fail  = g_failCount - beforeFail;
    std::printf("   %d / %d passed\n", total - fail, total);
}

void TestVector3()
{
    Vector3 a = { 1.0f, 2.0f, 3.0f };
    Vector3 b = { 4.0f, 5.0f, 6.0f };

    Vector3 sum = a + b;
    CheckNear(sum.x, 5.0f, "Vector3 operator+ (x)");
    CheckNear(sum.y, 7.0f, "Vector3 operator+ (y)");
    CheckNear(sum.z, 9.0f, "Vector3 operator+ (z)");

    Vector3 scaled = a * 2.0f;
    CheckNear(scaled.x, 2.0f, "Vector3 operator* (x)");
    CheckNear(scaled.y, 4.0f, "Vector3 operator* (y)");
    CheckNear(scaled.z, 6.0f, "Vector3 operator* (z)");

    CheckNear(Length({ 3.0f, 4.0f, 0.0f }), 5.0f, "Length (3-4-5)");
    CheckNear(Distance({ 0.0f, 0.0f, 0.0f }, { 3.0f, 4.0f, 0.0f }), 5.0f, "Distance");

    Vector3 n = Normalize({ 0.0f, 5.0f, 0.0f });
    CheckNear(n.y, 1.0f, "Normalize");
    Vector3 zero = Normalize({ 0.0f, 0.0f, 0.0f });
    CheckNear(zero.x, 0.0f, "Normalize(zero vector) は 0 のまま (x)");
    CheckNear(zero.y, 0.0f, "Normalize(zero vector) は 0 のまま (y)");
    CheckNear(zero.z, 0.0f, "Normalize(zero vector) は 0 のまま (z)");

    CheckNear(Dot({ 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }), 0.0f, "Dot (垂直なら0)");
    CheckNear(Dot({ 2.0f, 0.0f, 0.0f }, { 3.0f, 0.0f, 0.0f }), 6.0f, "Dot (平行)");

    Vector3 cross = Cross({ 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f });
    CheckNear(cross.z, 1.0f, "Cross (X×Y=Z)");

    Vector3 sub = Subtract(b, a);
    CheckNear(sub.x, 3.0f, "Subtract (x)");
    CheckNear(sub.y, 3.0f, "Subtract (y)");
    CheckNear(sub.z, 3.0f, "Subtract (z)");

    Vector3 mid = Lerp(a, b, 0.5f);
    CheckNear(mid.x, 2.5f, "Lerp(t=0.5) (x)");
    CheckNear(mid.y, 3.5f, "Lerp(t=0.5) (y)");
    CheckNear(mid.z, 4.5f, "Lerp(t=0.5) (z)");

    CheckNear(Clamp(5.0f, 0.0f, 10.0f), 5.0f, "Clamp (範囲内)");
    CheckNear(Clamp(-1.0f, 0.0f, 10.0f), 0.0f, "Clamp (下限)");
    CheckNear(Clamp(11.0f, 0.0f, 10.0f), 10.0f, "Clamp (上限)");
}

void TestMatrix4x4()
{
    Matrix4x4 identity = MakeIdentity4x4();
    Matrix4x4 translate = MakeTranslateMatrix({ 1.0f, 2.0f, 3.0f });

    Matrix4x4 result = Multiply(identity, translate);
    CheckNear(result.m[3][0], 1.0f, "単位行列との積は変化しない (x)");
    CheckNear(result.m[3][1], 2.0f, "単位行列との積は変化しない (y)");
    CheckNear(result.m[3][2], 3.0f, "単位行列との積は変化しない (z)");

    Matrix4x4 scale = MakeScaleMatrix({ 2.0f, 3.0f, 4.0f });
    CheckNear(scale.m[0][0], 2.0f, "MakeScaleMatrix (x)");
    CheckNear(scale.m[1][1], 3.0f, "MakeScaleMatrix (y)");
    CheckNear(scale.m[2][2], 4.0f, "MakeScaleMatrix (z)");

    Matrix4x4 affine = MakeAffineMatrix({ 1.0f, 1.0f, 1.0f }, Vector3{ 0.0f, 0.0f, 0.0f }, { 5.0f, 6.0f, 7.0f });
    CheckNear(affine.m[3][0], 5.0f, "MakeAffineMatrix (平行移動成分 x)");
    CheckNear(affine.m[3][1], 6.0f, "MakeAffineMatrix (平行移動成分 y)");
    CheckNear(affine.m[3][2], 7.0f, "MakeAffineMatrix (平行移動成分 z)");

    // 逆行列 × 元の行列 ≒ 単位行列（要素ごとに比較し、崩れている成分がわかるようにする）
    Matrix4x4 m = MakeAffineMatrix({ 2.0f, 3.0f, 1.0f }, Vector3{ 0.4f, 0.1f, 0.2f }, { 10.0f, -5.0f, 2.0f });
    Matrix4x4 inv = Inverse(m);
    Matrix4x4 shouldBeIdentity = Multiply(m, inv);
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            char name[48];
            std::snprintf(name, sizeof(name), "Inverse(m) * m ≒ 単位行列 [%d][%d]", i, j);
            float expected = (i == j) ? 1.0f : 0.0f;
            CheckNear(shouldBeIdentity.m[i][j], expected, name, 1e-3f);
        }
    }

    Matrix4x4 t = Transpose(translate);
    CheckNear(t.m[0][3], 1.0f, "Transpose (x)");
    CheckNear(t.m[1][3], 2.0f, "Transpose (y)");
    CheckNear(t.m[2][3], 3.0f, "Transpose (z)");
}

void TestQuaternion()
{
    Quaternion q1 = { 0.0f, 0.0f, 0.0f, 1.0f };
    Quaternion q2 = { 0.0f, 1.0f, 0.0f, 0.0f };

    Quaternion atStart = Slerp(q1, q2, 0.0f);
    CheckNear(atStart.w, 1.0f, "Slerp(t=0) は開始値と一致");

    Quaternion atEnd = Slerp(q1, q2, 1.0f);
    CheckNear(atEnd.y, 1.0f, "Slerp(t=1) は終了値と一致");
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

// 実際にゲーム側で使われている engine::Collision クラスの形状テスト
void TestCollisionShapes()
{
    // ---- 球 × 球 ----
    Sphere s1{ { 0.0f, 0.0f, 0.0f }, 1.0f };
    Sphere s2{ { 1.5f, 0.0f, 0.0f }, 1.0f };
    Sphere s3{ { 3.0f, 0.0f, 0.0f }, 1.0f };
    Check(Collision::CheckCollision(s1, s2), "Sphere x Sphere (重なりあり)");
    Check(!Collision::CheckCollision(s1, s3), "Sphere x Sphere (重なりなし)");

    // ---- AABB × AABB ----
    AABB box1{ { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f } };
    AABB box2{ { 0.5f, 0.5f, 0.5f }, { 1.5f, 1.5f, 1.5f } };
    AABB box3{ { 10.0f, 10.0f, 10.0f }, { 11.0f, 11.0f, 11.0f } };
    Check(Collision::CheckCollision(box1, box2), "AABB x AABB (重なりあり)");
    Check(!Collision::CheckCollision(box1, box3), "AABB x AABB (重なりなし)");

    // ---- 球 × AABB（両方向のオーバーロード）----
    Sphere sInBox{ { 0.5f, 0.5f, 0.5f }, 0.1f };
    Sphere sFarBox{ { 100.0f, 100.0f, 100.0f }, 0.1f };
    Check(Collision::CheckCollision(sInBox, box1),  "Sphere x AABB (内部)");
    Check(Collision::CheckCollision(box1, sInBox),  "AABB x Sphere (引数順の逆でも同じ結果)");
    Check(!Collision::CheckCollision(sFarBox, box1), "Sphere x AABB (遠い)");

    // ---- 球 × カプセル ----
    Capsule vertCap{ { 0.0f, 0.0f, 0.0f }, { 0.0f, 2.0f, 0.0f }, 0.5f };
    Sphere sOnAxis{ { 0.0f, 1.0f, 0.0f }, 0.3f };
    Sphere sFarAxis{ { 5.0f, 1.0f, 0.0f }, 0.3f };
    Check(Collision::CheckCollision(sOnAxis, vertCap),  "Sphere x Capsule (軸上)");
    Check(!Collision::CheckCollision(sFarAxis, vertCap), "Sphere x Capsule (遠い)");

    // ---- カプセル × カプセル ----
    Capsule crossA{ { 0.0f, 0.0f, 0.0f }, { 2.0f, 2.0f, 0.0f }, 0.3f };
    Capsule crossB{ { 0.0f, 2.0f, 0.0f }, { 2.0f, 0.0f, 0.0f }, 0.3f };
    Capsule farCap{ { 10.0f, 0.0f, 0.0f }, { 12.0f, 0.0f, 0.0f }, 0.3f };
    Check(Collision::CheckCollision(crossA, crossB), "Capsule x Capsule (交差)");
    Check(!Collision::CheckCollision(crossA, farCap), "Capsule x Capsule (遠い)");

    // ---- カプセル × AABB ----
    Capsule throughBox{ { 0.0f, -2.0f, 0.0f }, { 0.0f, 2.0f, 0.0f }, 0.5f };
    AABB centerBox{ { -0.5f, -0.5f, -0.5f }, { 0.5f, 0.5f, 0.5f } };
    Check(Collision::CheckCollision(throughBox, centerBox), "Capsule x AABB (貫通)");
    Check(!Collision::CheckCollision(farCap, centerBox),    "Capsule x AABB (遠い)");

    // ---- Collider 経由の形状ディスパッチ ----
    Collider colliderSphere; colliderSphere.SetAsSphere(sInBox);
    Collider colliderAABB;   colliderAABB.SetAsAABB(box1);
    Collider colliderCapA;   colliderCapA.SetAsCapsule(crossA);
    Collider colliderCapB;   colliderCapB.SetAsCapsule(crossB);
    Check(Collision::CheckCollision(colliderSphere, colliderAABB) == Collision::CheckCollision(sInBox, box1),
        "Collider dispatch (Sphere x AABB) は直接呼び出しと一致");
    Check(Collision::CheckCollision(colliderCapA, colliderCapB) == Collision::CheckCollision(crossA, crossB),
        "Collider dispatch (Capsule x Capsule) は直接呼び出しと一致");

    // ---- レイキャスト: Ray x AABB ----
    Ray rayHitBox{ { -5.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
    RaycastResult rrBox;
    Check(Collision::Raycast(rayHitBox, box1, rrBox), "Raycast vs AABB (命中)");
    CheckNear(rrBox.point.x, 0.0f, "Raycast vs AABB (ヒット点のX座標)");

    Ray rayMissBox{ { -5.0f, 5.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
    RaycastResult rrMiss;
    Check(!Collision::Raycast(rayMissBox, box1, rrMiss), "Raycast vs AABB (逸れる)");

    // tmin は 0.0f から始まり負にはならないため、内部始点は distance 0 の命中として扱われる
    Ray rayInsideBox{ { 0.5f, 0.5f, 0.5f }, { 1.0f, 0.0f, 0.0f } };
    RaycastResult rrInside;
    Check(Collision::Raycast(rayInsideBox, box1, rrInside), "Raycast vs AABB (内部始点は distance 0 で命中)");
    CheckNear(rrInside.distance, 0.0f, "Raycast vs AABB (内部始点の distance は 0)");

    // ---- レイキャスト: Ray x 球 ----
    Ray rayHitSphere{ { -5.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
    RaycastResult rrSphere;
    Check(Collision::Raycast(rayHitSphere, s1, rrSphere), "Raycast vs Sphere (外側から命中)");

    Ray rayInSphere{ { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
    RaycastResult rrInSphere;
    Check(Collision::Raycast(rayInSphere, s1, rrInSphere), "Raycast vs Sphere (内部始点は奥側で命中)");

    Ray rayMissSphere{ { -5.0f, 5.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
    RaycastResult rrMissSphere;
    Check(!Collision::Raycast(rayMissSphere, s1, rrMissSphere), "Raycast vs Sphere (逸れる)");

    // ---- レイキャスト: Ray x カプセル ----
    Ray rayHitCylinder{ { -5.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
    RaycastResult rrCyl;
    Check(Collision::Raycast(rayHitCylinder, vertCap, rrCyl), "Raycast vs Capsule (胴体に命中)");

    Ray rayHitCap{ { -5.0f, 2.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
    RaycastResult rrCap;
    Check(Collision::Raycast(rayHitCap, vertCap, rrCap), "Raycast vs Capsule (端の球に命中)");

    Ray rayMissCapsule{ { -5.0f, 10.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
    RaycastResult rrMissCap;
    Check(!Collision::Raycast(rayMissCapsule, vertCap, rrMissCap), "Raycast vs Capsule (逸れる)");

    // ---- レイキャスト: Collider 経由の形状ディスパッチ ----
    Collider colliderS1; colliderS1.SetAsSphere(s1);
    RaycastResult rrDirect, rrDispatch;
    Collision::Raycast(rayHitSphere, s1, rrDirect);
    Collision::Raycast(rayHitSphere, colliderS1, rrDispatch);
    CheckNear(rrDispatch.distance, rrDirect.distance, "Raycast dispatch (Sphere) は直接呼び出しと一致");
}

} // namespace

int main()
{
    // ソースはUTF-8だが、コンソールの既定コードページ（日本語環境ではShift-JIS）のままだと
    // 日本語のテスト名が文字化けするため、コンソール出力をUTF-8として解釈させる
    SetConsoleOutputCP(CP_UTF8);

    RunSection("Vector3", TestVector3);
    RunSection("Matrix4x4", TestMatrix4x4);
    RunSection("Quaternion", TestQuaternion);
    RunSection("Collision (Math関数)", TestCollision);
    RunSection("Collision (Sphere/AABB/Capsule/Raycast)", TestCollisionShapes);

    if (!g_failedNames.empty()) {
        std::printf("\n[FAILED TESTS]\n");
        for (const auto& name : g_failedNames) {
            std::printf("  - %s\n", name.c_str());
        }
    }

    std::printf("\n%d / %d tests passed\n", g_testCount - g_failCount, g_testCount);
    return g_failCount == 0 ? 0 : 1;
}
