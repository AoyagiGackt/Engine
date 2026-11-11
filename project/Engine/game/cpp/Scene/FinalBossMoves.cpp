/**
 * @file FinalBossMoves.cpp
 * @brief ラスボスの技（間合いを測る・走って詰めて連撃・突き抜け斬り・撃ち出す剣・範囲斬り・瞬身・怯み）と技の選び方
 * @note 技は IMove の派生クラスで、技の中の手順はメンバ関数へのポインタを次の手順に差し替えて進める
 */
#include "FinalBossBattle.h"
#include "EnemyEntity.h"
#include "FinalBossTuning.h"
#include "GameConstants.h"
#include "HitEffectLibrary.h"
#include "MeleeCombo.h"
#include "Player.h"
#include <algorithm>
#include <cmath>

using namespace engine::graphics;

namespace engine::game {
namespace {
constexpr float kMeasureBand = 0.5f; ///< 保とうとする距離の遊び
constexpr float kParabolaPeakScale = 4.0f; ///< t(1-t) の山の高さを1にする倍率（跳びの弧）
constexpr float kLeapLandGap = 0.8f; ///< 跳び斬りでプレイヤーの手前に降りる距離
constexpr float kRushRecoverSeconds = 0.45f;
constexpr float kIaiMinDistance = 3.0f; ///< 突き抜け斬りを選ぶ最短の距離
constexpr float kIaiCutSpacing = 1.2f; ///< 通り道に走らせる斬線の間隔
constexpr float kIaiWindupLean = 0.3f; ///< 構えで前へかがむ角度
constexpr float kPhantomMinDistance = 3.5f; ///< 撃ち出す剣を選びやすくなる距離
constexpr float kPhantomRecoverSeconds = 0.4f;
constexpr float kPhantomRaiseTime = 0.2f; ///< 剣を呼ぶ間の、段の姿勢の時刻
constexpr int kPhantomRageExtra = 3; ///< 後半戦に増える剣の数
constexpr float kSpacePulseInterval = 0.25f; ///< 範囲斬りの予兆の輪を出す間隔
constexpr float kSpaceMarkStartScale = 0.6f; ///< 範囲斬りの範囲の印の、出始めの大きさの割合
constexpr float kSpaceCrossAngle = 0.7f; ///< 交差する2本の斬線の傾き
constexpr int kSpaceStep = 2; ///< 範囲斬りで見せる段
constexpr float kBlinkBehindChance = 0.6f; ///< 瞬身で背後へ回る確率
constexpr float kBlinkFollowThink = 0.15f; ///< 瞬身の直後に次の技までの間
constexpr float kBlinkCloseDistance = 2.0f; ///< これより近いと瞬身を選びやすい
constexpr int kRageSlashExtra = 1; ///< 後半戦に増える連撃の数
constexpr int kNoWeight = 0;
constexpr int kLowWeight = 1;
constexpr int kMidWeight = 2;
constexpr int kHighWeight = 3;

/** @brief 調整値の短い呼び名 */
const FinalBossTuning& Tune() {
    return FinalBossTuningStore::Get();
}

/** @brief 0〜1 の一様な乱数 */
float Random01(std::mt19937& rng) {
    return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng);
}
} // namespace

// ══════════════════════════════════════════════════════
// 技の型
// ══════════════════════════════════════════════════════

/** @brief ボスの技1つ（Begin で始め、Update が true を返したら終わり） */
class FinalBossBattle::IMove {
public:
    virtual ~IMove() = default;
    /** @brief 技を始める */
    virtual void Begin(FinalBossBattle& battle) = 0;
    /** @brief 1フレーム進める（終わったら true） */
    virtual bool Update(FinalBossBattle& battle, float deltaTime) = 0;
    /** @brief 今の間合いで選ぶ重み（0 なら選ばない） */
    virtual int Weight(const FinalBossBattle& battle, float distance) const = 0;
    /** @brief 終わった後に次の技までの間を決めるなら、その秒数（負なら考える時間を使う） */
    virtual float FollowUpThink() const {
        return -1.0f;
    }
};

/** @brief 間合いを測る（プレイヤーの周りを回りながら、決めた距離を保つ） */
class FinalBossBattle::MeasureMove : public FinalBossBattle::IMove {
public:
    /** @brief 回る向きを決めて始める */
    void Begin(FinalBossBattle& battle) override {
        timer_ = 0.0f;
        orbitDir_ = Random01(*battle.links_.rng) < 0.5f ? -1.0f : 1.0f;
    }
    /** @brief 回り込みと距離の調整 */
    bool Update(FinalBossBattle& battle, float deltaTime) override {
        const FinalBossTuning& t = Tune();
        timer_ += deltaTime;
        // ボスがプレイヤーの周りを回るのは、闘技場をプレイヤーの周りに逆へ回すのと同じ
        const float playerX = battle.links_.player->GetPosition().x;
        const Vector3 savedCenter = battle.arenaCenter_;
        const float savedYaw = battle.arenaYaw_;
        battle.RotateArenaAbout(playerX, orbitDir_ * t.measureOrbitSpeed * battle.SpeedMult() * deltaTime);
        if (!battle.InsideArena({ battle.bossX_, 0.0f, 0.0f }) || !battle.InsideArena(battle.links_.player->GetPosition())) {
            battle.arenaCenter_ = savedCenter;
            battle.arenaYaw_ = savedYaw;
        }
        // 遠ければ寄り、近ければ下がる
        const float dir = battle.DirectionToPlayer();
        const float distance = battle.DistanceToPlayer();
        const float step = t.walkSpeed * battle.SpeedMult() * deltaTime;
        if (distance > t.measureDistance + kMeasureBand) {
            battle.bossX_ = battle.ClampBossX(battle.bossX_ + dir * step);
        } else if (distance < t.measureDistance - kMeasureBand) {
            battle.bossX_ = battle.ClampBossX(battle.bossX_ - dir * step);
        }
        battle.bossFacing_ = dir;
        battle.pose_ = { };
        battle.pose_.moving = true;
        return timer_ >= t.measureSeconds;
    }
    /** @brief いつでも選べる */
    int Weight(const FinalBossBattle&, float) const override {
        return kMidWeight;
    }

private:
    float timer_ = 0.0f;
    float orbitDir_ = 1.0f;
};

/** @brief 走って詰めて連撃し、締めは跳び斬り */
class FinalBossBattle::RushMove : public FinalBossBattle::IMove {
public:
    /** @brief 走り出す */
    void Begin(FinalBossBattle&) override {
        timer_ = 0.0f;
        slashIndex_ = 0;
        hitDone_ = false;
        step_ = &RushMove::Run;
    }
    /** @brief 今の手順を進める */
    bool Update(FinalBossBattle& battle, float deltaTime) override {
        return (this->*step_)(battle, deltaTime);
    }
    /** @brief 離れているほど選びやすい */
    int Weight(const FinalBossBattle&, float distance) const override {
        return distance > Tune().rushStopDistance ? kHighWeight : kMidWeight;
    }

private:
    using Step = bool (RushMove::*)(FinalBossBattle&, float);

    /** @brief プレイヤーへ走って詰める */
    bool Run(FinalBossBattle& battle, float deltaTime) {
        const FinalBossTuning& t = Tune();
        timer_ += deltaTime;
        const float dir = battle.DirectionToPlayer();
        battle.bossFacing_ = dir;
        battle.bossX_ = battle.ClampBossX(battle.bossX_ + dir * t.runSpeed * battle.SpeedMult() * deltaTime);
        battle.pose_ = { };
        battle.pose_.moving = true;
        if (battle.DistanceToPlayer() <= t.rushStopDistance || timer_ >= t.runTimeout) {
            timer_ = 0.0f;
            step_ = &RushMove::Slash;
        }
        return false;
    }
    /** @brief 1振りずつ踏み込んで斬る */
    bool Slash(FinalBossBattle& battle, float deltaTime) {
        const FinalBossTuning& t = Tune();
        const float duration = t.slashSeconds / battle.SpeedMult();
        timer_ += deltaTime;
        const float progress = std::clamp(timer_ / duration, 0.0f, 1.0f);
        battle.pose_ = { };
        battle.pose_.attack = KatanaStep(slashIndex_);
        battle.pose_.attackTime = progress;
        // 踏み込みはプレイヤーの手前で止める（すり抜けて背後へ回らないように）
        const float lunged = battle.bossX_ + battle.bossFacing_ * t.slashLunge * deltaTime / duration;
        if (std::abs(battle.links_.player->GetPosition().x - lunged) > t.rushStopDistance * 0.5f) {
            battle.bossX_ = battle.ClampBossX(lunged);
        }
        if (!hitDone_ && progress >= t.slashHitRatio) {
            hitDone_ = true;
            const HitEffectPlayer fx(battle.links_.particles, battle.links_.shaker, battle.links_.rng);
            fx.Slash("lastboss_slash", { battle.bossX_ + battle.bossFacing_ * t.slashReach * 0.5f, t.shadowBaseY, 0.0f }, battle.bossFacing_);
            if (battle.StrikeAlong(battle.bossX_, battle.bossX_ + battle.bossFacing_ * t.slashReach, t.shadowBaseY, t.slashDamage)) {
                fx.Impact("lastboss_slash");
            }
        }
        if (progress < 1.0f) {
            return false;
        }
        // 次の振りへ。数を振り終えたらプレイヤーの手前へ跳ぶ
        ++slashIndex_;
        timer_ = 0.0f;
        hitDone_ = false;
        battle.bossFacing_ = battle.DirectionToPlayer();
        const int slashes = t.rushSlashes + (battle.rageStarted_ ? kRageSlashExtra : 0);
        if (slashIndex_ >= slashes) {
            leapFrom_ = battle.bossX_;
            leapTo_ = battle.ClampBossX(battle.links_.player->GetPosition().x - battle.bossFacing_ * kLeapLandGap);
            step_ = &RushMove::Leap;
        }
        return false;
    }
    /** @brief 跳んで、着地と同時に前後を斬る */
    bool Leap(FinalBossBattle& battle, float deltaTime) {
        const FinalBossTuning& t = Tune();
        const float duration = t.leapSeconds / battle.SpeedMult();
        timer_ += deltaTime;
        const float progress = std::clamp(timer_ / duration, 0.0f, 1.0f);
        battle.bossX_ = leapFrom_ + (leapTo_ - leapFrom_) * progress;
        battle.telegraph_.Circle(leapTo_, battle.FloorY(), t.leapReach, battle.TelegraphColor());
        battle.bossLift_ = kParabolaPeakScale * t.leapHeight * progress * (1.0f - progress);
        battle.pose_ = { };
        battle.pose_.airborne = true;
        battle.pose_.attack = KatanaStep(slashIndex_);
        battle.pose_.attackTime = progress;
        if (progress < 1.0f) {
            return false;
        }
        battle.bossLift_ = 0.0f;
        const HitEffectPlayer fx(battle.links_.particles, battle.links_.shaker, battle.links_.rng);
        const Vector3 landing = { battle.bossX_, t.shadowBaseY, 0.0f };
        fx.Impact("lastboss_heavy");
        fx.Ring("lastboss_heavy", landing);
        fx.Slash("lastboss_slash", landing, battle.bossFacing_);
        battle.StrikeAlong(battle.bossX_ - t.leapReach, battle.bossX_ + t.leapReach, t.shadowBaseY, t.leapDamage);
        timer_ = 0.0f;
        step_ = &RushMove::Recover;
        return false;
    }
    /** @brief 斬り終えて構え直す */
    bool Recover(FinalBossBattle& battle, float deltaTime) {
        timer_ += deltaTime;
        battle.pose_ = { };
        return timer_ >= kRushRecoverSeconds;
    }

    Step step_ = &RushMove::Run;
    float timer_ = 0.0f;
    int slashIndex_ = 0;
    bool hitDone_ = false;
    float leapFrom_ = 0.0f;
    float leapTo_ = 0.0f;
};

/** @brief 突き抜け斬り（光って構え、プレイヤーを突き抜けて斬り抜け、遅れて通り道に斬線が走る） */
class FinalBossBattle::IaiMove : public FinalBossBattle::IMove {
public:
    /** @brief 光って構える */
    void Begin(FinalBossBattle& battle) override {
        timer_ = 0.0f;
        step_ = &IaiMove::Windup;
        const HitEffectPlayer fx(battle.links_.particles, battle.links_.shaker, battle.links_.rng);
        fx.Ring("lastboss_iai_glint", { battle.bossX_, Tune().shadowBaseY, 0.0f });
    }
    /** @brief 今の手順を進める */
    bool Update(FinalBossBattle& battle, float deltaTime) override {
        timer_ += deltaTime;
        return (this->*step_)(battle);
    }
    /** @brief 離れている時だけ選ぶ（後半戦は選びやすい） */
    int Weight(const FinalBossBattle& battle, float distance) const override {
        if (distance < kIaiMinDistance) {
            return kNoWeight;
        }
        return battle.rageStarted_ ? kHighWeight : kMidWeight;
    }

private:
    using Step = bool (IaiMove::*)(FinalBossBattle&);

    /** @brief 構えて溜める */
    bool Windup(FinalBossBattle& battle) {
        battle.bossFacing_ = battle.DirectionToPlayer();
        // 通り抜ける道（今のプレイヤーの先まで）を床に出す
        const float pathEnd = battle.ClampBossX(battle.links_.player->GetPosition().x + battle.bossFacing_ * Tune().iaiDashPast);
        battle.telegraph_.Line(battle.bossX_, pathEnd, battle.FloorY(), Tune().telegraphLineWidth, battle.TelegraphColor());
        battle.pose_ = { };
        battle.pose_.attack = KatanaStep(0);
        battle.pose_.lean = { kIaiWindupLean, 0.0f, 0.0f };
        if (timer_ >= Tune().iaiWindup / battle.SpeedMult()) {
            from_ = battle.bossX_;
            to_ = battle.ClampBossX(battle.links_.player->GetPosition().x + battle.bossFacing_ * Tune().iaiDashPast);
            timer_ = 0.0f;
            step_ = &IaiMove::Dash;
        }
        return false;
    }
    /** @brief 一瞬で通り抜ける */
    bool Dash(FinalBossBattle& battle) {
        const float progress = std::clamp(timer_ / Tune().iaiDashSeconds, 0.0f, 1.0f);
        battle.bossX_ = from_ + (to_ - from_) * progress;
        battle.pose_ = { };
        battle.pose_.attack = KatanaStep(1);
        battle.pose_.attackTime = progress;
        if (progress >= 1.0f) {
            battle.bossFacing_ = battle.DirectionToPlayer();
            timer_ = 0.0f;
            step_ = &IaiMove::Cut;
        }
        return false;
    }
    /** @brief 遅れて通り道に斬線を走らせる */
    bool Cut(FinalBossBattle& battle) {
        battle.pose_ = { };
        if (timer_ < Tune().iaiCutDelay) {
            return false;
        }
        const FinalBossTuning& t = Tune();
        const HitEffectPlayer fx(battle.links_.particles, battle.links_.shaker, battle.links_.rng);
        const float length = std::abs(to_ - from_);
        const int cuts = (std::max)(1, static_cast<int>(std::ceil(length / kIaiCutSpacing)));
        for (int i = 0; i <= cuts; ++i) {
            const float x = from_ + (to_ - from_) * static_cast<float>(i) / static_cast<float>(cuts);
            fx.Slash("lastboss_iai_cut", { x, t.shadowBaseY, 0.0f }, to_ >= from_ ? 1.0f : -1.0f);
        }
        fx.Impact("lastboss_iai");
        battle.StrikeAlong(from_, to_, t.shadowBaseY, t.iaiDamage);
        timer_ = 0.0f;
        step_ = &IaiMove::Recover;
        return false;
    }
    /** @brief 刀を納めて構え直す */
    bool Recover(FinalBossBattle& battle) {
        battle.pose_ = { };
        return timer_ >= Tune().iaiRecover;
    }

    Step step_ = &IaiMove::Windup;
    float timer_ = 0.0f;
    float from_ = 0.0f;
    float to_ = 0.0f;
};

/** @brief 撃ち出す剣（背後に剣を並べ、1本ずつプレイヤーへ撃ち出す） */
class FinalBossBattle::PhantomMove : public FinalBossBattle::IMove {
public:
    /** @brief 剣を並べる */
    void Begin(FinalBossBattle& battle) override {
        const int count = Tune().phantomCount + (battle.rageStarted_ ? kPhantomRageExtra : 0);
        battle.SummonPhantoms(count);
        timer_ = 0.0f;
        total_ = Tune().phantomSummonSeconds + static_cast<float>(count) * Tune().phantomInterval + kPhantomRecoverSeconds;
    }
    /** @brief 撃ち終えるまで構えて待つ（剣の動きは UpdatePhantoms） */
    bool Update(FinalBossBattle& battle, float deltaTime) override {
        timer_ += deltaTime;
        battle.bossFacing_ = battle.DirectionToPlayer();
        battle.pose_ = { };
        battle.pose_.attack = KatanaStep(kSpaceStep);
        battle.pose_.attackTime = kPhantomRaiseTime;
        return timer_ >= total_;
    }
    /** @brief 離れている時に選びやすい */
    int Weight(const FinalBossBattle& battle, float distance) const override {
        if (!battle.PhantomsCleared()) {
            return kNoWeight;
        }
        return distance >= kPhantomMinDistance ? kMidWeight : kLowWeight;
    }

private:
    float timer_ = 0.0f;
    float total_ = 0.0f;
};

/** @brief 範囲斬り（プレイヤーの足元に予兆を出し、遅れて範囲ごと斬る。後半戦は3か所） */
class FinalBossBattle::SpaceSlashMove : public FinalBossBattle::IMove {
public:
    /** @brief 斬る場所を決める */
    void Begin(FinalBossBattle& battle) override {
        const float playerX = battle.links_.player->GetPosition().x;
        centers_.clear();
        centers_.push_back(playerX);
        if (battle.rageStarted_) {
            centers_.push_back(battle.ClampBossX(playerX - Tune().spaceSpacing));
            centers_.push_back(battle.ClampBossX(playerX + Tune().spaceSpacing));
        }
        timer_ = 0.0f;
        pulseTimer_ = 0.0f;
        struck_ = false;
    }
    /** @brief 予兆の輪を出し続け、時間が来たら斬る */
    bool Update(FinalBossBattle& battle, float deltaTime) override {
        const FinalBossTuning& t = Tune();
        timer_ += deltaTime;
        pulseTimer_ -= deltaTime;
        const float telegraph = t.spaceTelegraph / battle.SpeedMult();
        const HitEffectPlayer fx(battle.links_.particles, battle.links_.shaker, battle.links_.rng);
        battle.bossFacing_ = battle.DirectionToPlayer();
        battle.pose_ = { };
        battle.pose_.attack = KatanaStep(kSpaceStep);
        battle.pose_.attackTime = struck_ ? std::clamp((timer_ - telegraph) / t.slashSeconds, 0.0f, 1.0f) : 0.0f;
        if (!struck_) {
            // 斬られる範囲を床に出し、時間が来るにつれて本来の大きさへ広げる
            const float grow = kSpaceMarkStartScale + (1.0f - kSpaceMarkStartScale) * std::clamp(timer_ / telegraph, 0.0f, 1.0f);
            for (float x : centers_) {
                battle.telegraph_.Circle(x, battle.FloorY(), t.spaceRadius * grow, battle.TelegraphColor());
            }
        }
        if (!struck_ && pulseTimer_ <= 0.0f) {
            pulseTimer_ = kSpacePulseInterval;
            for (float x : centers_) {
                fx.Ring("lastboss_space_warn", { x, t.shadowBaseY, 0.0f });
            }
        }
        if (!struck_ && timer_ >= telegraph) {
            struck_ = true;
            fx.Impact("lastboss_space");
            for (float x : centers_) {
                const Vector3 center = { x, t.shadowBaseY, 0.0f };
                fx.Slash("lastboss_space", center, 1.0f, { .angle = kSpaceCrossAngle });
                fx.Slash("lastboss_space", center, 1.0f, { .angle = -kSpaceCrossAngle });
                battle.StrikeAlong(x - t.spaceRadius, x + t.spaceRadius, t.shadowBaseY, t.spaceDamage);
            }
        }
        return timer_ >= telegraph + t.spaceRecover;
    }
    /** @brief いつでも選べる（後半戦は選びやすい） */
    int Weight(const FinalBossBattle& battle, float) const override {
        return battle.rageStarted_ ? kHighWeight : kMidWeight;
    }

private:
    std::vector<float> centers_;
    float timer_ = 0.0f;
    float pulseTimer_ = 0.0f;
    bool struck_ = false;
};

/** @brief 瞬身（消えて、プレイヤーの背後か離れた所に出る） */
class FinalBossBattle::BlinkMove : public FinalBossBattle::IMove {
public:
    /** @brief 次の1回は必ず離れる（怯みから立ち直る時） */
    void ForceAway() {
        forceAway_ = true;
    }
    /** @brief 消える */
    void Begin(FinalBossBattle& battle) override {
        const HitEffectPlayer fx(battle.links_.particles, battle.links_.shaker, battle.links_.rng);
        fx.Ring("lastboss_blink", { battle.bossX_, Tune().shadowBaseY, 0.0f });
        battle.bossVanished_ = true;
        battle.pose_ = { };
        behind_ = !forceAway_ && Random01(*battle.links_.rng) < kBlinkBehindChance;
        forceAway_ = false;
        timer_ = 0.0f;
    }
    /** @brief 時間が来たら決めた側に出る */
    bool Update(FinalBossBattle& battle, float deltaTime) override {
        const FinalBossTuning& t = Tune();
        timer_ += deltaTime;
        if (timer_ < t.blinkSeconds) {
            return false;
        }
        // ボスから見たプレイヤーの側。背後ならその先、離れるならその逆へ
        const float playerX = battle.links_.player->GetPosition().x;
        const float side = playerX >= battle.bossX_ ? 1.0f : -1.0f;
        const float target = behind_ ? playerX + side * t.blinkBehindDistance : playerX - side * t.blinkAwayDistance;
        battle.bossX_ = battle.ClampBossX(target);
        battle.bossVanished_ = false;
        battle.bossFacing_ = battle.DirectionToPlayer();
        const HitEffectPlayer fx(battle.links_.particles, battle.links_.shaker, battle.links_.rng);
        fx.Ring("lastboss_blink", { battle.bossX_, t.shadowBaseY, 0.0f });
        return true;
    }
    /** @brief 近づかれている時に選びやすい */
    int Weight(const FinalBossBattle&, float distance) const override {
        return distance < kBlinkCloseDistance ? kHighWeight : kLowWeight;
    }
    /** @brief 出てすぐ次の技へつなぐ */
    float FollowUpThink() const override {
        return kBlinkFollowThink;
    }

private:
    float timer_ = 0.0f;
    bool behind_ = false;
    bool forceAway_ = false;
};

/** @brief 怯み（短い間にダメージが溜まるとのけぞり、明けたら瞬身で離れる。技の選び直しでは選ばない） */
class FinalBossBattle::StaggerMove : public FinalBossBattle::IMove {
public:
    /** @brief のけぞる */
    void Begin(FinalBossBattle& battle) override {
        timer_ = 0.0f;
        battle.bossLift_ = 0.0f;
        const HitEffectPlayer fx(battle.links_.particles, battle.links_.shaker, battle.links_.rng);
        fx.Impact("lastboss_stagger");
    }
    /** @brief 時間が来たら瞬身で離れる */
    bool Update(FinalBossBattle& battle, float deltaTime) override {
        timer_ += deltaTime;
        battle.pose_ = { };
        battle.pose_.lean = { -Tune().staggerLean, 0.0f, 0.0f };
        if (timer_ < Tune().staggerSeconds) {
            return false;
        }
        static_cast<BlinkMove*>(battle.blinkMove_)->ForceAway();
        battle.BeginMove(*battle.blinkMove_);
        return false;
    }
    /** @brief 選ばない */
    int Weight(const FinalBossBattle&, float) const override {
        return kNoWeight;
    }

private:
    float timer_ = 0.0f;
};

// ══════════════════════════════════════════════════════
// 技の一覧と選び方
// ══════════════════════════════════════════════════════

/** @brief 段階・技・剣を空で用意する（中身は Initialize で作る） */
FinalBossBattle::FinalBossBattle() = default;
/** @brief 技の実体を破棄する（IMove の定義があるこのファイルで破棄する） */
FinalBossBattle::~FinalBossBattle() = default;

/** @brief 分身に取らせる刀の地上コンボの段（段数を超えたら最後の段。表が空なら nullptr） */
const MeleeAttackDef* FinalBossBattle::KatanaStep(int index) {
    const auto& steps = GetMeleeComboSet(WeaponType::kSword).ground;
    return steps.count > 0 ? &steps[(std::min)(index, steps.count - 1)] : nullptr;
}

/** @brief 技の一覧を作る */
void FinalBossBattle::BuildMoves() {
    moves_.clear();
    moves_.push_back(std::make_unique<MeasureMove>());
    moves_.push_back(std::make_unique<RushMove>());
    moves_.push_back(std::make_unique<IaiMove>());
    moves_.push_back(std::make_unique<PhantomMove>());
    moves_.push_back(std::make_unique<SpaceSlashMove>());
    auto blink = std::make_unique<BlinkMove>();
    blinkMove_ = blink.get();
    moves_.push_back(std::move(blink));
    auto stagger = std::make_unique<StaggerMove>();
    staggerMove_ = stagger.get();
    moves_.push_back(std::move(stagger));
}

/** @brief 技を打ち切って構えへ戻す */
void FinalBossBattle::CancelMoves() {
    currentMove_ = nullptr;
    pose_ = { };
    bossLift_ = 0.0f;
    bossVanished_ = false;
    staggerAccum_ = 0;
    staggerTimer_ = 0.0f;
    for (PhantomBlade& blade : phantoms_) {
        blade.active = false;
    }
}

/** @brief 戦闘中の1フレーム（技を進め、終われば次の技を選ぶ） */
void FinalBossBattle::UpdateBoss(float deltaTime) {
    UpdateStagger(deltaTime);
    if (currentMove_ != nullptr) {
        IMove* running = currentMove_;
        if (running->Update(*this, deltaTime) && currentMove_ == running) {
            // 終わった技を覚えておき、同じ技を続けて出さない
            lastMove_ = running;
            currentMove_ = nullptr;
            pose_ = { };
            const float followUp = running->FollowUpThink();
            const float think = std::uniform_real_distribution<float>(Tune().thinkMinSeconds, Tune().thinkMaxSeconds)(*links_.rng);
            thinkTimer_ = followUp >= 0.0f ? followUp : think * (rageStarted_ ? Tune().rageThinkMult : 1.0f);
        }
        return;
    }
    // 技の合間はプレイヤーを向いて構える
    bossFacing_ = DirectionToPlayer();
    pose_ = { };
    thinkTimer_ -= deltaTime;
    if (thinkTimer_ <= 0.0f) {
        ChooseNextMove();
    }
}

/** @brief 次の技を間合いと後半戦かどうかで選ぶ（直前の技は選ばない） */
void FinalBossBattle::ChooseNextMove() {
    const float distance = DistanceToPlayer();
    int total = 0;
    for (const auto& move : moves_) {
        if (move.get() != lastMove_) {
            total += move->Weight(*this, distance);
        }
    }
    if (total <= 0) {
        return;
    }
    int pick = std::uniform_int_distribution<int>(0, total - 1)(*links_.rng);
    for (const auto& move : moves_) {
        if (move.get() == lastMove_) {
            continue;
        }
        pick -= move->Weight(*this, distance);
        if (pick < 0) {
            BeginMove(*move);
            return;
        }
    }
}

/** @brief 技を始める */
void FinalBossBattle::BeginMove(IMove& move) {
    currentMove_ = &move;
    move.Begin(*this);
}

/** @brief 一定時間に受けたダメージを数え、溜まったら怯ませる */
void FinalBossBattle::UpdateStagger(float deltaTime) {
    const int hp = boss_->GetHp();
    const int damage = lastBossHp_ - hp;
    lastBossHp_ = hp;
    if (damage > 0) {
        // 数えていなければ数え始める
        if (staggerTimer_ <= 0.0f) {
            staggerTimer_ = Tune().staggerWindow;
            staggerAccum_ = 0;
        }
        staggerAccum_ += damage;
    }
    staggerTimer_ = (std::max)(staggerTimer_ - deltaTime, 0.0f);
    if (staggerAccum_ >= Tune().staggerDamage && currentMove_ != staggerMove_ && currentMove_ != blinkMove_) {
        staggerAccum_ = 0;
        staggerTimer_ = 0.0f;
        BeginMove(*staggerMove_);
    }
}

} // namespace engine::game
