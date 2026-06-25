#include "FrustumCuller.h"
#include <cmath>

void FrustumCuller::Update(const Matrix4x4& m)
{
    // Gribb-Hartmann method (row-vector convention: v * M)
    // row i of m is m.m[i][*]
    // left:   row3 + row0
    planes_[0] = { m.m[0][3] + m.m[0][0], m.m[1][3] + m.m[1][0], m.m[2][3] + m.m[2][0], m.m[3][3] + m.m[3][0] };
    // right:  row3 - row0
    planes_[1] = { m.m[0][3] - m.m[0][0], m.m[1][3] - m.m[1][0], m.m[2][3] - m.m[2][0], m.m[3][3] - m.m[3][0] };
    // bottom: row3 + row1
    planes_[2] = { m.m[0][3] + m.m[0][1], m.m[1][3] + m.m[1][1], m.m[2][3] + m.m[2][1], m.m[3][3] + m.m[3][1] };
    // top:    row3 - row1
    planes_[3] = { m.m[0][3] - m.m[0][1], m.m[1][3] - m.m[1][1], m.m[2][3] - m.m[2][1], m.m[3][3] - m.m[3][1] };
    // near:   row2 only (DirectX NDC: near plane at z=0, not z=-1 as in OpenGL)
    planes_[4] = { m.m[0][2], m.m[1][2], m.m[2][2], m.m[3][2] };
    // far:    row3 - row2
    planes_[5] = { m.m[0][3] - m.m[0][2], m.m[1][3] - m.m[1][2], m.m[2][3] - m.m[2][2], m.m[3][3] - m.m[3][2] };

    for (auto& p : planes_) {
        float len = std::sqrt(p.a * p.a + p.b * p.b + p.c * p.c);
        if (len > 1e-6f) {
            float inv = 1.0f / len;
            p.a *= inv; p.b *= inv; p.c *= inv; p.d *= inv;
        }
    }
}

bool FrustumCuller::IsVisible(const Vector3& center, const Vector3& half) const
{
    for (const auto& p : planes_) {
        // positive vertex: corner furthest in plane normal direction
        float px = (p.a >= 0.0f) ? (center.x + half.x) : (center.x - half.x);
        float py = (p.b >= 0.0f) ? (center.y + half.y) : (center.y - half.y);
        float pz = (p.c >= 0.0f) ? (center.z + half.z) : (center.z - half.z);
        if (p.a * px + p.b * py + p.c * pz + p.d < 0.0f) {
            return false;
        }
    }
    return true;
}

bool FrustumCuller::IsSphereVisible(const Vector3& center, float radius) const
{
    for (const auto& p : planes_) {
        if (p.a * center.x + p.b * center.y + p.c * center.z + p.d < -radius) {
            return false;
        }
    }
    return true;
}
