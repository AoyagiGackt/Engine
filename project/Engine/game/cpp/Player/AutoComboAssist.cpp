/**
 * @file AutoComboAssist.cpp
 * @brief 攻撃ボタンだけで技を出し分ける入力アシスト（AutoComboAssist）の実装
 */
#include "AutoComboAssist.h"
#include "MeleeCombo.h"
#include <algorithm>
#include <iterator>
#include <vector>
using namespace engine::game;

namespace {

constexpr int kNotUsed = 0; ///< その締め方を使わない
constexpr int kAfterFullCombo = -1; ///< 地上コンボを最後まで振ってから
constexpr int kEarlyStep = 2; ///< 早めに切り上げる回の段

/**
 * @brief コンボ1周ぶんの締め方
 * @note どちらも kNotUsed なら、打ち上げず地上コンボの締めで敵を打ち飛ばす
 */
struct ComboRoute {
    int launchAfterStep; ///< この段まで振ったら打ち上げて空中へ
    int skillAfterStep; ///< この段が当たったら固有技の連撃へ繋ぐ
};

/** @brief 1周ごとにランダムに選ぶルート */
constexpr ComboRoute kRoutes[] = {
    { kAfterFullCombo, kNotUsed }, // 最後まで振る → 打ち上げ → 空中コンボ
    { kNotUsed, kNotUsed }, // 地上コンボの締めで打ち飛ばす
    { kEarlyStep, kNotUsed }, // 早めに打ち上げて空中へ
    { kNotUsed, kAfterFullCombo }, // 最後まで振る → 固有技で締める
    { kNotUsed, kEarlyStep }, // 早めに固有技へ繋ぐ
};

/** @brief ルート表の段指定を、その武器の地上コンボの段数に合わせた段番号にする（使わないなら0） */
int ResolveStep(int routeStep, int groundCount)
{
    if (routeStep == kNotUsed) {
        return 0;
    }
    if (routeStep == kAfterFullCombo) {
        return groundCount;
    }
    return (std::min)(routeStep, groundCount);
}

/** @brief その武器でルートが使えるか（打ち上げ技や固有技の連撃を持たない武器では、それを使うルートを外す） */
bool IsRouteAvailable(const ComboRoute& route, WeaponType type)
{
    const bool needsLauncher = route.launchAfterStep != kNotUsed;
    const bool needsSkill = route.skillAfterStep != kNotUsed;
    return (!needsLauncher || GetMeleeComboSet(type).launcher != nullptr)
        && (!needsSkill || GetSkillSequence(type).count > 0);
}

} // namespace

AutoComboAssist::AutoComboAssist()
    : rng_(std::random_device {}())
{
}

void AutoComboAssist::SetEnabled(bool enabled)
{
    enabled_ = enabled;
    if (!enabled_) {
        jumpRequestTimer_ = 0.0f;
        skillRequestTimer_ = 0.0f;
    }
}

int AutoComboAssist::LaunchStep(WeaponType type) const
{
    const ComboRoute& route = kRoutes[routeIndex_];
    if (!IsRouteAvailable(route, type)) {
        return 0;
    }
    return ResolveStep(route.launchAfterStep, GetMeleeComboSet(type).ground.count);
}

int AutoComboAssist::SkillStep(WeaponType type) const
{
    const ComboRoute& route = kRoutes[routeIndex_];
    if (!IsRouteAvailable(route, type)) {
        return 0;
    }
    return ResolveStep(route.skillAfterStep, GetMeleeComboSet(type).ground.count);
}

void AutoComboAssist::PickNextRoute(WeaponType type)
{
    std::vector<std::size_t> candidates;
    for (std::size_t i = 0; i < std::size(kRoutes); ++i) {
        if (i != routeIndex_ && IsRouteAvailable(kRoutes[i], type)) {
            candidates.push_back(i);
        }
    }
    if (candidates.empty()) {
        return;
    }
    std::uniform_int_distribution<std::size_t> pick(0, candidates.size() - 1);
    routeIndex_ = candidates[pick(rng_)];
}

bool AutoComboAssist::WantsLauncher(WeaponType type, const MeleeComboController& combo, bool airborne) const
{
    if (!enabled_ || airborne) {
        return false;
    }
    const MeleeAttackDef* active = combo.GetActive();
    if (active == nullptr || active->launcher) {
        return false;
    }
    const int launchStep = LaunchStep(type);
    return launchStep > 0 && combo.GetStep() >= launchStep;
}

void AutoComboAssist::OnStepStarted(WeaponType type, const MeleeComboController& combo, bool airborne)
{
    const MeleeAttackDef* active = combo.GetActive();
    if (!enabled_ || airborne || active == nullptr || combo.IsSequenceActive()) {
        return;
    }
    // 締めの段を振っている間に次のルートへ替えると、締めの直後に次のルートの打ち上げが出てしまうので、
    // ルートを選び直すのは次の周の最初の地上の段が始まった時にする
    if (routeFinished_) {
        PickNextRoute(type);
        routeFinished_ = false;
    }
    // 打ち上げた回は打ち上げ技で、打ち上げも固有技も使わない回は地上コンボの締めで1周とみなす
    // （固有技の回は OnStepHit() で固有技を予約した時に1周とみなす）
    const bool launched = active->launcher;
    const bool groundFinished = LaunchStep(type) == 0 && SkillStep(type) == 0
        && combo.GetStep() >= GetMeleeComboSet(type).ground.count;
    if (launched || groundFinished) {
        routeFinished_ = true;
    }
}

void AutoComboAssist::OnStepHit(WeaponType type, const MeleeComboController& combo, bool airborne)
{
    const MeleeAttackDef* active = combo.GetActive();
    if (!enabled_ || airborne || active == nullptr || active->launcher || combo.IsSequenceActive()) {
        return;
    }
    const int skillStep = SkillStep(type);
    if (skillStep > 0 && combo.GetStep() >= skillStep) {
        skillRequestTimer_ = kRequestWindow_;
        routeFinished_ = true;
    }
}

void AutoComboAssist::OnLauncherHit()
{
    if (enabled_) {
        jumpRequestTimer_ = kRequestWindow_;
    }
}

bool AutoComboAssist::ConsumeJumpRequest()
{
    if (jumpRequestTimer_ <= 0.0f) {
        return false;
    }
    jumpRequestTimer_ = 0.0f;
    return true;
}

bool AutoComboAssist::ConsumeSkillRequest()
{
    if (skillRequestTimer_ <= 0.0f) {
        return false;
    }
    skillRequestTimer_ = 0.0f;
    return true;
}

void AutoComboAssist::Update(float dt)
{
    jumpRequestTimer_ = (std::max)(jumpRequestTimer_ - dt, 0.0f);
    skillRequestTimer_ = (std::max)(skillRequestTimer_ - dt, 0.0f);
}
