/**
 * @file GimmickMotion.cpp
 * @brief 仕掛けの動き方ごとの姿・可動域・演出の実装
 */
#include "GimmickMotion.h"
#include "FallingFloorBehavior.h"
#include "LevelLoader.h"
#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>

namespace engine::game {

namespace {

/** @brief 動かない（none や未知の名前） */
class StillMotion : public GimmickMotion {
};

/** @brief 指定軸に沿ってsin波で往復する */
class SlideMotion : public GimmickMotion {
public:
    explicit SlideMotion(const Vector3& axis)
        : axis_(axis)
    {
    }
    GimmickPose Evaluate(const ObjectDesc& desc, float timer) const override
    {
        GimmickPose pose;
        pose.position = axis_ * (std::sin(timer * desc.motionSpeed) * desc.motionAmount);
        return pose;
    }
    Vector3 RangeAxis(const ObjectDesc& desc) const override { return axis_ * desc.motionAmount; }

private:
    Vector3 axis_;
};

/** @brief 指定軸まわりに一定速度で回り続ける */
class SpinMotion : public GimmickMotion {
public:
    SpinMotion(const Vector3& axis, bool carriesChildren)
        : axis_(axis)
        , carriesChildren_(carriesChildren)
    {
    }
    GimmickPose Evaluate(const ObjectDesc& desc, float timer) const override
    {
        GimmickPose pose;
        pose.rotation = axis_ * (timer * desc.motionSpeed);
        return pose;
    }
    bool CarriesChildren() const override { return carriesChildren_; }

private:
    Vector3 axis_;
    bool carriesChildren_;
};

/** @brief 乗ると光ってから崩れ落ち、しばらくして戻る床 */
class FallMotion : public GimmickMotion {
public:
    GimmickPose Evaluate(const ObjectDesc& desc, float timer) const override
    {
        const FallingFloorBehavior::Snapshot state = FallingFloorBehavior::Evaluate(timer, desc.motionSpeed, desc.motionAmount);
        GimmickPose pose;
        pose.position.y = state.yOffset;
        pose.visible = state.visible;
        pose.solid = state.solid;
        return pose;
    }
    void EmitEffects(engine::graphics::ParticleManager* pm, const ObjectDesc& desc, const Vector3& worldPosition,
        float timer, bool& wasSolid) const override
    {
        const FallingFloorBehavior::Snapshot state = FallingFloorBehavior::Evaluate(timer, desc.motionSpeed, desc.motionAmount);
        const Vector3 halfExtent = { 0.5f * std::abs(desc.scale.x), 0.5f * std::abs(desc.scale.y), 0.5f * std::abs(desc.scale.z) };
        FallingFloorBehavior::EmitDebris(pm, worldPosition, halfExtent, state, wasSolid);
    }
};

/** @brief 一定周期で見えたり消えたりする（当たり判定は残る） */
class BlinkMotion : public GimmickMotion {
public:
    GimmickPose Evaluate(const ObjectDesc& desc, float timer) const override
    {
        GimmickPose pose;
        pose.visible = std::sin(timer * desc.motionSpeed) >= 0.0f;
        return pose;
    }
};

//  自由動作（custom）の進行度と加減速

/** @brief 波のように往復する（-1〜1） */
float LoopProgress(float phase)
{
    return std::sin(phase);
}

/** @brief 等速の三角波で往復する（0→1→0、1周期=2） */
float PingPongProgress(float phase)
{
    constexpr float kPingPongPeriod = 2.0f;
    const float cycle = std::fmod(std::abs(phase), kPingPongPeriod);
    return cycle <= 1.0f ? cycle : kPingPongPeriod - cycle;
}

/** @brief 有効化から一度だけ進んで止まる（0〜1） */
float OnceProgress(float phase)
{
    return std::clamp(phase, 0.0f, 1.0f);
}

/** @brief 往復方式から進行度の求め方を返す */
float (*ProgressOf(MotionMode mode))(float)
{
    // MotionMode の並び順と一致させる
    static constexpr float (*kProgresses[])(float) = { &LoopProgress, &PingPongProgress, &OnceProgress };
    const size_t index = static_cast<size_t>(mode);
    return index < std::size(kProgresses) ? kProgresses[index] : &LoopProgress;
}

float LinearEase(float t)
{
    return t;
}

/** @brief 符号を保ったままsmoothstepで加減速を付ける */
float SmoothEase(float t)
{
    const float sign = t < 0.0f ? -1.0f : 1.0f;
    const float mag = std::abs(t);
    return sign * (mag * mag * (3.0f - 2.0f * mag));
}

/** @brief 加減速の種類から緩急の付け方を返す */
float (*EaseOf(MotionEase ease))(float)
{
    return ease == MotionEase::Smooth ? &SmoothEase : &LinearEase;
}

/** @brief 移動方向・回転量・往復方式・加減速を自由に組み合わせる */
class FreeformMotion : public GimmickMotion {
public:
    GimmickPose Evaluate(const ObjectDesc& desc, float timer) const override
    {
        // 進行度t（-1〜1または0〜1）をモードごとに求め、移動方向×量と回転量へ同じtを掛ける
        const float t = EaseOf(desc.motionEase)(ProgressOf(desc.motionMode)(timer * desc.motionSpeed));
        GimmickPose pose;
        pose.position = desc.motionAxis * (desc.motionAmount * t);
        pose.rotation = desc.motionRotation * t;
        return pose;
    }
    Vector3 RangeAxis(const ObjectDesc& desc) const override { return desc.motionAxis * desc.motionAmount; }
    bool IsOneWay(const ObjectDesc& desc) const override { return desc.motionMode == MotionMode::Once; }
    bool IsFreeform() const override { return true; }
    void Validate(const ObjectDesc& desc, std::vector<std::string>& issues) const override
    {
        if (desc.motionAxis.x == 0.0f && desc.motionAxis.y == 0.0f && desc.motionAxis.z == 0.0f
            && desc.motionRotation.x == 0.0f && desc.motionRotation.y == 0.0f && desc.motionRotation.z == 0.0f) {
            issues.push_back("カスタム動作の移動方向と回転量が両方0です: " + desc.name);
        }
    }
};

} // namespace

const GimmickMotion& GimmickMotion::Of(const std::string& name)
{
    static const StillMotion still;
    static const SlideMotion moveX({ 1.0f, 0.0f, 0.0f });
    static const SlideMotion moveY({ 0.0f, 1.0f, 0.0f });
    static const SpinMotion rotateY({ 0.0f, 1.0f, 0.0f }, false);
    // Z回転は歯車や観覧車のように、子の足場も一緒に回して見せる
    static const SpinMotion rotateZ({ 0.0f, 0.0f, 1.0f }, true);
    static const FallMotion fall;
    static const BlinkMotion blink;
    static const FreeformMotion custom;
    static const std::pair<const char*, const GimmickMotion*> kMotions[] = {
        { "none", &still },
        { "move_x", &moveX },
        { "move_y", &moveY },
        { "rotate_y", &rotateY },
        { "rotate_z", &rotateZ },
        { "fall", &fall },
        { "blink", &blink },
        { "custom", &custom },
    };
    for (const auto& [motionName, motion] : kMotions) {
        if (name == motionName) {
            return *motion;
        }
    }
    return still;
}

} // namespace engine::game
