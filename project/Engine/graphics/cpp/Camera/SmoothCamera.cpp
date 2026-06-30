#include "SmoothCamera.h"
using namespace engine;
using namespace engine::graphics;

SmoothCamera::SmoothCamera(Camera* camera)
    : camera_(camera)
{
}

void SmoothCamera::SetTarget(const Vector3& pos, const Vector3& rot)
{
    targetPos_ = pos;
    targetRot_ = rot;
}

void SmoothCamera::SetSmoothFrames(int frames)
{
    smoothFrames_ = (frames < 1) ? 1 : frames;
}

void SmoothCamera::Update()
{
    posHistory_.push_back(targetPos_);
    rotHistory_.push_back(targetRot_);

    while ((int)posHistory_.size() > smoothFrames_) {
        posHistory_.pop_front();
        rotHistory_.pop_front();
    }

    Vector3 avgPos = {};
    Vector3 avgRot = {};
    for (const auto& p : posHistory_) {
        avgPos.x += p.x; avgPos.y += p.y; avgPos.z += p.z;
    }
    for (const auto& r : rotHistory_) {
        avgRot.x += r.x; avgRot.y += r.y; avgRot.z += r.z;
    }
    float n = static_cast<float>(posHistory_.size());

    camera_->SetTranslate({ avgPos.x / n, avgPos.y / n, avgPos.z / n });
    camera_->SetRotate({ avgRot.x / n, avgRot.y / n, avgRot.z / n });
}
