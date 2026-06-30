#pragma once
#include "MakeAffine.h"
namespace engine::graphics {
class FrustumCuller {
public:
    void Update(const Matrix4x4& viewProjection);
    bool IsVisible(const Vector3& center, const Vector3& halfExtents) const;
    bool IsSphereVisible(const Vector3& center, float radius) const;

private:
    struct Plane { float a, b, c, d; };
    Plane planes_[6];
};

} // namespace engine::graphics
