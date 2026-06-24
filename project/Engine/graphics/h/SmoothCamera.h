#pragma once
#include "Camera.h"
#include "MakeAffine.h"
#include <deque>

class SmoothCamera {
public:
    explicit SmoothCamera(Camera* camera);

    void SetTarget(const Vector3& pos, const Vector3& rot = {});
    void SetSmoothFrames(int frames);
    void Update();

    const Vector3& GetTargetPos() const { return targetPos_; }

    // Accessors for SceneEditor compatibility
    Vector3*             GetTargetPosPtr()     { return &targetPos_; }
    Vector3*             GetTargetRotPtr()     { return &targetRot_; }
    int*                 GetSmoothFramesPtr()  { return &smoothFrames_; }
    std::deque<Vector3>* GetPosHistoryPtr()    { return &posHistory_; }
    std::deque<Vector3>* GetRotHistoryPtr()    { return &rotHistory_; }

private:
    Camera*             camera_       = nullptr;
    Vector3             targetPos_    = {};
    Vector3             targetRot_    = {};
    int                 smoothFrames_ = 1;
    std::deque<Vector3> posHistory_;
    std::deque<Vector3> rotHistory_;
};
