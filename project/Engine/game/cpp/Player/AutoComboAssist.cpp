/**
 * @file AutoComboAssist.cpp
 * @brief 攻撃ボタンだけで技を出し分ける入力アシスト（AutoComboAssist）の実装
 */
#include "AutoComboAssist.h"
#include "MeleeCombo.h"
#include <algorithm>
#include <iterator>
#include <vector>

namespace engine::game {
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
    if (routeStep == kNotUsed)
    {
        return 0;
    }
    if (routeStep == kAfterFullCombo)
    {
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

/** @brief コンボルートを選ぶ乱数を初期化する */
AutoComboAssist::AutoComboAssist()
    : rng_(std::random_device {}())
{
}

/** @brief アシストの有効/無効を切り替える（無効にしたら出しかけの追撃ジャンプ・固有技も取り消す） */
void AutoComboAssist::SetEnabled(bool enabled)
{
    enabled_ = enabled;
    if (!enabled_)
    {
        jumpRequestTimer_ = 0.0f;
        skillRequestTimer_ = 0.0f;
    }
}

/** @brief 今のルートで打ち上げに移る地上コンボの段（1始まり）。打ち上げないルートなら0 */
int AutoComboAssist::LaunchStep(WeaponType type) const
{
    const ComboRoute& route = kRoutes[routeIndex_];
    if (!IsRouteAvailable(route, type))
    {
        return 0;
    }
    return ResolveStep(route.launchAfterStep, GetMeleeComboSet(type).ground.count);
}

/** @brief 今のルートで固有技に繋ぐ地上コンボの段（1始まり）。固有技を使わないルートなら0 */
int AutoComboAssist::SkillStep(WeaponType type) const
{
    const ComboRoute& route = kRoutes[routeIndex_];
    if (!IsRouteAvailable(route, type))
    {
        return 0;
    }
    return ResolveStep(route.skillAfterStep, GetMeleeComboSet(type).ground.count);
}

/** @brief その武器で使えるルートの中から、今のルート以外をランダムに選ぶ */
void AutoComboAssist::PickNextRoute(WeaponType type)
{
    std::vector<std::size_t> candidates;
    for (std::size_t i = 0; i < std::size(kRoutes); ++i)
    {
        if (i != routeIndex_ && IsRouteAvailable(kRoutes[i], type))
        {
            candidates.push_back(i);
        }
    }
    if (candidates.empty())
    {
        return;
    }
    std::uniform_int_distribution<std::size_t> pick(0, candidates.size() - 1);
    routeIndex_ = candidates[pick(rng_)];
}

/** @brief 次の段を打ち上げ技にするか（今のルートの打ち上げ段を振っている最中なら true） */
bool AutoComboAssist::WantsLauncher(WeaponType type, const MeleeComboController& combo, bool airborne) const
{
    if (!enabled_ || airborne)
    {
        return false;
    }
    const MeleeAttackDef* active = combo.GetActive();
    if (active == nullptr || active->launcher)
    {
        return false;
    }
    const int launchStep = LaunchStep(type);
    return launchStep > 0 && combo.GetStep() >= launchStep;
}

/** @brief 段が始まったことを伝える（前の周を締め終えていれば、ここで次のルートを選ぶ） */
void AutoComboAssist::OnStepStarted(WeaponType type, const MeleeComboController& combo, bool airborne)
{
    const MeleeAttackDef* active = combo.GetActive();
    if (!enabled_ || airborne || active == nullptr || combo.IsSequenceActive())
    {
        return;
    }
    // 締めの段を振っている間に次のルートへ替えると、締めの直後に次のルートの打ち上げが出てしまうので、
    // ルートを選び直すのは次の周の最初の地上の段が始まった時にする
    if (routeFinished_)
    {
        PickNextRoute(type);
        routeFinished_ = false;
    }
    // 打ち上げた回は打ち上げ技で、打ち上げも固有技も使わない回は地上コンボの締めで1周とみなす
    // （固有技の回は OnStepHit() で固有技を予約した時に1周とみなす）
    const bool launched = active->launcher;
    const bool groundFinished = LaunchStep(type) == 0 && SkillStep(type) == 0
        && combo.GetStep() >= GetMeleeComboSet(type).ground.count;
    if (launched || groundFinished)
    {
        routeFinished_ = true;
    }
}

/** @brief 段の本ヒットを伝える（固有技のルートで固有技の段に当たったら、固有技を予約する） */
void AutoComboAssist::OnStepHit(WeaponType type, const MeleeComboController& combo, bool airborne)
{
    const MeleeAttackDef* active = combo.GetActive();
    if (!enabled_ || airborne || active == nullptr || active->launcher || combo.IsSequenceActive())
    {
        return;
    }
    const int skillStep = SkillStep(type);
    if (skillStep > 0 && combo.GetStep() >= skillStep)
    {
        skillRequestTimer_ = kRequestWindow;
        routeFinished_ = true;
    }
}

/** @brief 打ち上げ技が当たったことを伝える（追撃ジャンプを予約する） */
void AutoComboAssist::OnLauncherHit()
{
    if (enabled_)
    {
        jumpRequestTimer_ = kRequestWindow;
    }
}

/** @brief 予約された追撃ジャンプを1回だけ取り出す（取り出したら予約は消える） */
bool AutoComboAssist::ConsumeJumpRequest()
{
    if (jumpRequestTimer_ <= 0.0f)
    {
        return false;
    }
    jumpRequestTimer_ = 0.0f;
    return true;
}

/** @brief 予約された固有技を1回だけ取り出す（取り出したら予約は消える） */
bool AutoComboAssist::ConsumeSkillRequest()
{
    if (skillRequestTimer_ <= 0.0f)
    {
        return false;
    }
    skillRequestTimer_ = 0.0f;
    return true;
}

/** @brief 1フレーム進める（取り出されなかった追撃ジャンプ・固有技の予約を期限切れにする） */
void AutoComboAssist::Update(float dt)
{
    jumpRequestTimer_ = (std::max)(jumpRequestTimer_ - dt, 0.0f);
    skillRequestTimer_ = (std::max)(skillRequestTimer_ - dt, 0.0f);
}

} // namespace engine::game
