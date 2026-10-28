/**
 * @file PlayerWeaponBehaviors.cpp
 * @brief 武器種別ごとの固有技（Weapon Behavior Strategy）と、その状態を持つBehaviorの実装
 */
#include "Easing.h"
#include "GameConstants.h"
#include "Input.h"
#include "Player.h"
#include "PlayerTuning.h"
#include "Weapon.h"
#include <algorithm>
#include <array>
#include <cmath>


namespace engine::game {
namespace {
/** @brief 読み込み済みの調整値を取得する */
const PlayerTuning& Tuning() {
    return PlayerTuning::Get();
}

/** @brief クールタイムを1フレームぶん減らす（0で止める） */
void Cooldown(float& timer, float step) {
    timer = (std::max)(timer - step, 0.0f);
}
} // namespace



/** @brief 固有技の連撃1種ぶんの設定（どの連撃表を使い、クールタイムはどの調整値か） */
struct SkillSequenceDef {
    WeaponType type;
    float PlayerTuning::* cooldownSeconds;
    bool groundOnly; ///< 地上でだけ出せるか
    bool slamsOnFinish; ///< 締めで地面を叩いて衝撃波を出すか
    bool rampageWhenAwakened; ///< 覚醒中の攻撃ボタンが覚醒乱舞になるか
};

constexpr SkillSequenceDef kSwordSequence = { WeaponType::kSword, &PlayerTuning::swordSkillCooldown, false, false, true };
constexpr SkillSequenceDef kSpearSequence = { WeaponType::kSpear, &PlayerTuning::spearSkillCooldown, false, false, false };
constexpr SkillSequenceDef kHammerSequence = { WeaponType::kHammer, &PlayerTuning::hammerSkillCooldown, true, true, false };
constexpr SkillSequenceDef kDaggerSequence = { WeaponType::kDagger, &PlayerTuning::daggerSkillCooldown, false, false, false };

/** @brief 武器固有の連続攻撃を共通の段階処理で進める振る舞い */
class Player::SkillSequenceBehavior : public IWeaponBehavior {
public:
    /** @brief 固有技の連撃の定義を受け取って初期化する */
    explicit SkillSequenceBehavior(const SkillSequenceDef& def)
        : def_(def) {
    }

    /** @brief 固有技の入力を受けて連撃を始める */
    void Update(Player& player, Input* input) override {
        if (def_.groundOnly && !player.IsOnGround()) {
            return;
        }
        player.TryStartSkillSequence(input, def_.type, cooldown_, Tuning().*def_.cooldownSeconds);
    }
    /** @brief 固有技のクールタイムを進める */
    void Tick(Player&, Input*, float cooldownStep) override {
        Cooldown(cooldown_, cooldownStep);
    }
    /** @brief この武器の固有技で乱舞を始めるかを返す */
    bool StartsRampage(const Player& player) const override {
        return def_.rampageWhenAwakened && player.IsAwakened();
    }
    /** @brief 固有技の最後の段で叩きつけの衝撃を出すかを返す */
    bool SlamsOnSkillFinish() const override {
        return def_.slamsOnFinish;
    }

private:
    const SkillSequenceDef& def_;
    float cooldown_ = 0.0f;
};

/** @brief 短剣の連続攻撃を制御する振る舞い */
class Player::DaggerBehavior final : public SkillSequenceBehavior {
public:
    /** @brief 短剣の連撃の定義で初期化する */
    DaggerBehavior()
        : SkillSequenceBehavior(kDaggerSequence) {
    }

    /** @brief 空中ダッシュ中はダッシュを進め、そうでなければ固有技の入力を受ける */
    void Update(Player& player, Input* input) override {
        // 空中ダッシュ（機動力枠）。滑っている間は落下を止め、足場から足場へ飛び移れるようにする
        if (airDash_.active) {
            player.StopVerticalMotion();
            player.AdvanceDash(airDash_);
            return;
        }
        if (player.IsOnGround()) {
            // 地上では乱れ斬り: 回転を挟んだ高速の斬撃を重ね、回りながら斬り上げて敵を打ち飛ばす
            SkillSequenceBehavior::Update(player, input);
            return;
        }
        if (input->TriggerAction(Input::Action::kSkill) && airDashAvailable_) {
            const PlayerTuning& t = Tuning();
            airDashAvailable_ = false;
            player.StopVerticalMotion();
            player.BeginDash(airDash_, player.GetLastDirX() * t.daggerAirDashDist * player.GetSkillMods().blinkDistMult);
            player.PlaySkillAnim(&CharacterRig::runningJumpAnim, t.daggerAirDashAnimSpeed);
        }
    }
    /** @brief 攻撃などで中断された時に空中ダッシュを止める */
    void Interrupt() override {
        airDash_.active = false;
    }
    /** @brief 着地したら空中ダッシュをまた使えるようにする */
    void OnLanded() override {
        airDashAvailable_ = true;
    }

private:
    DashMotion airDash_;
    bool airDashAvailable_ = true;
};

/** @brief 球武器の投射と固有技を制御する振る舞い */
class Player::BallBehavior final : public IWeaponBehavior {
public:
    /** @brief 固有技の長押し中は球を連射し、空中ではくるくる回る */
    void Update(Player& player, Input* input) override {
        // スピン連射 + 空中くるくる
        if (!input->PushAction(Input::Action::kSkill)) {
            return;
        }
        const PlayerTuning& t = Tuning();
        if (input->TriggerAction(Input::Action::kSkill)) {
            player.PlaySkillAnim(&CharacterRig::punchAnim, t.ballSpinShotAnimSpeed);
        }
        if (shootCooldown_ <= 0.0f) {
            player.SetFrameEvent(FrameEvent::kSpinShot);
            shootCooldown_ = t.ballShootInterval * player.GetSkillMods().fireIntervalMult;
        }
        if (!player.IsOnGround()) {
            player.AddSpin(t.ballSpinSpeed);
        }
    }
    /** @brief 連射の間隔のクールタイムを進める */
    void Tick(Player&, Input*, float) override {
        Cooldown(shootCooldown_, gameConstants::kFrameDeltaTime);
    }
    // 転がっている間は空中で回り続け、着地したら戻す
    bool KeepsSpin(const Player& player) const override {
        return !player.IsOnGround();
    }

private:
    float shootCooldown_ = 0.0f;
};

/** @brief 大剣の投擲と回転攻撃を制御する振る舞い */
class Player::GreatswordBehavior final : public IWeaponBehavior {
public:
    /** @brief 固有技の入力で大剣を投げる（投げた後の進行は Tick で扱う） */
    void Update(Player& player, Input* input) override {
        // 投げ回転斬り 大剣そのものを投げ、途中で静止して渦のように回転し、周囲の敵を巻き込みながら多段ヒットする
        // 発生（投げる瞬間）だけここで扱う。進行中の状態（飛行→渦→帰還）はTick()が装備武器に関係なく毎フレーム進める
        if (active_ || !input->TriggerAction(Input::Action::kSkill) || cooldown_ > 0.0f) {
            return;
        }
        const PlayerTuning& t = Tuning();
        startPos_ = player.GetSkillHoldPosition();
        restPos_ = startPos_;
        restPos_.x = player.ClampToStageX(startPos_.x + player.GetLastDirX() * t.greatswordThrowDist);
        timer_ = 0.0f;
        spinHitTimer_ = 0.0f;
        active_ = true;
        returnCaptured_ = false;
        cooldown_ = t.greatswordThrowCooldown;
        player.SetFrameEvent(FrameEvent::kGreatswordThrown);
        player.PlaySkillAnim(&CharacterRig::slashAnim, t.greatswordThrowAnimSpeed);
    }

    /** @brief 投げた大剣の飛行・渦・帰還を、装備中の武器に関係なく進める */
    void Tick(Player& player, Input* input, float cooldownStep) override {
        Cooldown(cooldown_, cooldownStep);
        if (!active_) {
            return;
        }
        const PlayerTuning& t = Tuning();
        timer_ += gameConstants::kFrameDeltaTime;
        if (timer_ < t.greatswordThrowTravelTime) {
            return; // 飛んでいる最中（静止するまで）はまだ渦を巻かない
        }

        const float spinElapsed = timer_ - t.greatswordThrowTravelTime;
        if (spinElapsed < t.greatswordVortexMaxDuration) {
            // 渦の最中にもう一度スペースを押したら、上限まで待たずにその場で帰還を開始する（手動リコール）
            if (input->TriggerAction(Input::Action::kSkill)) {
                timer_ = t.greatswordThrowTravelTime + t.greatswordVortexMaxDuration;
                return;
            }
            spinHitTimer_ += gameConstants::kFrameDeltaTime;
            if (spinHitTimer_ >= t.greatswordSpinHitInterval) {
                spinHitTimer_ -= t.greatswordSpinHitInterval;
                player.SetFrameEvent(FrameEvent::kGreatswordSpinHit);
            }
            return;
        }

        // 渦が終わったら、瞬間移動で戻さず手元へ飛んで帰るフェーズへ（帰還先はこの瞬間の位置を1回だけ記録）
        if (!returnCaptured_) {
            returnTargetPos_ = player.GetSkillHoldPosition();
            returnCaptured_ = true;
        }
        if (spinElapsed - t.greatswordVortexMaxDuration >= t.greatswordReturnTime) {
            active_ = false; // 帰還完了、次フレームから手元のボーン追従に戻る
        }
    }

    /** @brief 持ち替えた時に投げた大剣を引っ込め、状態を仕切り直す */
    void OnUnequipped() override {
        // 渦の途中で持ち替えても居残らないよう仕切り直す
        active_ = false;
        returnCaptured_ = false;
    }

    /** @brief 大剣を投げていて手元に無いかを返す */
    bool IsDisarmed() const override {
        return active_;
    }

    /** @brief 投げた大剣の位置と回転を返す（投げていない時は false） */
    bool GetThrownPose(Vector3& outPosition, float& outSpinZ) const override {
        if (!active_) {
            return false;
        }
        const PlayerTuning& t = Tuning();
        if (timer_ < t.greatswordThrowTravelTime) {
            // 飛行中: 手元から静止地点へ補間しながら飛んでいく
            outPosition = Lerp(startPos_, restPos_, easing::EaseOutQuad(timer_ / t.greatswordThrowTravelTime));
        } else {
            const float spinElapsed = timer_ - t.greatswordThrowTravelTime;
            if (spinElapsed < t.greatswordVortexMaxDuration) {
                // 渦の最中: その場に留まり、渦らしく小さく上下に揺れる
                outPosition = restPos_;
                outPosition.y += std::sin(spinElapsed * t.greatswordVortexBobSpeed) * t.greatswordVortexBobAmplitude;
            } else {
                // 帰還中: 渦の中心から手元へ飛んで戻る（瞬間移動に見えないよう補間する）
                const float returnElapsed = spinElapsed - t.greatswordVortexMaxDuration;
                const float progress = (std::min)(returnElapsed / t.greatswordReturnTime, 1.0f);
                outPosition = Lerp(restPos_, returnTargetPos_, easing::EaseInQuad(progress));
            }
        }
        // 目にも留まらぬ速さで回転させ続ける（飛行中→静止後の渦で途切れず連続した見た目にする）
        outSpinZ = timer_ * t.greatswordThrowSpinSpeed;
        return true;
    }

    /** @brief 静止して渦を巻いている最中か（飛行中・帰還中はfalse） */
    bool IsSpinning() const {
        if (!active_) {
            return false;
        }
        const PlayerTuning& t = Tuning();
        const float spinElapsed = timer_ - t.greatswordThrowTravelTime;
        return spinElapsed >= 0.0f && spinElapsed < t.greatswordVortexMaxDuration;
    }
    /** @brief 大剣が渦を巻いて止まる位置を返す */
    const Vector3& GetRestPosition() const {
        return restPos_;
    }

private:
    /** @brief 2点の間を割合 t で補間した位置を返す */
    static Vector3 Lerp(const Vector3& from, const Vector3& to, float t) {
        return { from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t, from.z + (to.z - from.z) * t };
    }

    bool active_ = false; ///< 投げてから手元へ戻るまでtrue（飛行中も含む）
    Vector3 startPos_ { }; ///< 投げた瞬間の手元位置（飛行中の補間の始点）
    Vector3 restPos_ { }; ///< 大剣が静止する位置（渦の中心）
    Vector3 returnTargetPos_ { }; ///< 帰還先（渦が終わった瞬間の手元位置を1回だけ記録する）
    bool returnCaptured_ = false;
    float timer_ = 0.0f; ///< 投げてからの経過秒（飛行→渦→帰還の順で進む）
    float spinHitTimer_ = 0.0f; ///< 渦の中でのヒット間隔用タイマー
    float cooldown_ = 0.0f;
};

/** @brief 鎌の固有攻撃を制御する振る舞い */
class Player::ScytheBehavior final : public IWeaponBehavior {
public:
    /** @brief 空中で固有技を押している間、降下を抑えて滞空する（地上では滞空時間を回復する） */
    void Update(Player& player, Input* input) override {
        // 滞空ホバー 空中限定で降下を抑える。時間制のリソースで無限滞空を防ぎ、着地で回復する
        const PlayerTuning& t = Tuning();
        if (player.IsOnGround()) {
            hoverTimer_ = (std::min)(hoverTimer_ + gameConstants::kFrameDeltaTime * t.scytheHoverRecoverRate, t.scytheHoverMax);
            return;
        }
        if (!input->PushAction(Input::Action::kSkill) || hoverTimer_ <= 0.0f) {
            return;
        }
        if (input->TriggerAction(Input::Action::kSkill)) {
            player.PlaySkillAnim(&CharacterRig::slashAnim, t.scytheHoverAnimSpeed);
            player.SetFrameEvent(FrameEvent::kScytheSpin);
        }
        player.LimitFallSpeed(t.scytheHoverVYCap);
        hoverTimer_ -= gameConstants::kFrameDeltaTime;
    }

private:
    float hoverTimer_ = Tuning().scytheHoverMax; ///< 残りの連続ホバー秒数
};

/** @brief 斧の固有攻撃と強化を制御する振る舞い */
class Player::AxeBehavior final : public IWeaponBehavior {
public:
    /** @brief 固有技の入力で突進し、一定時間ダメージが上がる状態にする */
    void Update(Player& player, Input* input) override {
        // バーサーク突進 突進しつつ、命中の有無に関わらず一定時間ダメージが上がる（狂戦士らしいリスク覚悟の一撃）
        if (dash_.active) {
            if (player.AdvanceDash(dash_)) {
                player.SetFrameEvent(FrameEvent::kAxeCharge);
            }
            return;
        }
        if (!input->TriggerAction(Input::Action::kSkill) || cooldown_ > 0.0f) {
            return;
        }
        const PlayerTuning& t = Tuning();
        player.BeginDash(dash_, player.GetLastDirX() * t.axeChargeDist);
        cooldown_ = t.axeSkillCooldown;
        rageTimer_ = t.axeRageDuration;
        player.PlaySkillAnim(&CharacterRig::punchAnim, t.axeChargeAnimSpeed);
    }
    /** @brief 固有技のクールタイムと強化の残り時間を進める */
    void Tick(Player&, Input*, float cooldownStep) override {
        Cooldown(cooldown_, cooldownStep);
        Cooldown(rageTimer_, gameConstants::kFrameDeltaTime);
    }
    /** @brief 攻撃などで中断された時に突進を止める */
    void Interrupt() override {
        dash_.active = false;
    }

    /** @brief 強化（ダメージ上昇）の最中かを返す */
    bool IsEnraged() const {
        return rageTimer_ > 0.0f;
    }

private:
    DashMotion dash_;
    float cooldown_ = 0.0f;
    float rageTimer_ = 0.0f;
};

/** @brief 武器種から共有する固有技の振る舞いを選ぶ定義集 */
struct Player::WeaponBehaviorSet {
    /** @brief 固有技を持たない武器 */
    class NoSkillBehavior final : public IWeaponBehavior {
    public:
        /** @brief 固有技の無い武器では何もしない */
        void Update(Player&, Input*) override { }
    };

    SkillSequenceBehavior sword { kSwordSequence };
    SkillSequenceBehavior spear { kSpearSequence };
    SkillSequenceBehavior hammer { kHammerSequence };
    DaggerBehavior dagger;
    BallBehavior ball;
    GreatswordBehavior greatsword;
    ScytheBehavior scythe;
    AxeBehavior axe;
    NoSkillBehavior none;

    // WeaponType の並び順
    std::array<IWeaponBehavior*, kWeaponTypeCount> byType = {
        &sword, &spear, &hammer, &dagger, &ball, &greatsword, &scythe, &axe,
    };

    /** @brief 種類に対応する共有定義を取得する */
    IWeaponBehavior& For(WeaponType type) {
        const size_t index = static_cast<size_t>(type);
        return index < byType.size() ? *byType[index] : none;
    }
};

 // namespace engine::game

/** @brief 武器ごとの振る舞いをまとめたセットを生成する */
Player::Player()
    : weaponBehaviors_(std::make_unique<WeaponBehaviorSet>()) {
}

/** @brief unique_ptr で持つ不完全型のメンバを破棄するため、定義を cpp に置く */
Player::~Player() = default;

/** @brief 武器の種類に対応する振る舞いを返す */
Player::IWeaponBehavior& Player::BehaviorFor(WeaponType type) {
    return weaponBehaviors_->For(type);
}

/** @brief 武器の種類に対応する振る舞いを返す（読み取り専用） */
const Player::IWeaponBehavior& Player::BehaviorFor(WeaponType type) const {
    return weaponBehaviors_->For(type);
}

/** @brief 全武器のBehaviorの踏み込み系の動きを打ち切る（回避・ワープ用） */
void Player::InterruptWeaponSkills() {
    for (IWeaponBehavior* behavior : weaponBehaviors_->byType) {
        behavior->Interrupt();
    }
}

/** @brief 全武器のBehaviorの進行中の技を仕切り直す（持ち替え用） */
void Player::ResetWeaponSkills() {
    for (IWeaponBehavior* behavior : weaponBehaviors_->byType) {
        behavior->OnUnequipped();
    }
}

/** @brief 全武器のBehaviorのクールタイムと、手元を離れた技を進める */
void Player::TickWeaponSkills(Input* input, float cooldownStep) {
    for (IWeaponBehavior* behavior : weaponBehaviors_->byType) {
        behavior->Tick(*this, input, cooldownStep);
    }
}

/** @brief 着地を全武器のBehaviorへ知らせる */
void Player::NotifyWeaponSkillsLanded() {
    for (IWeaponBehavior* behavior : weaponBehaviors_->byType) {
        behavior->OnLanded();
    }
}

/** @brief Spaceが押され、クールタイムが明けていれば、武器の固有技の連撃を始める */
bool Player::TryStartSkillSequence(Input* input, WeaponType type, float& cooldown, float cooldownSeconds) {
    // 段の進行・ヒット・振りの見た目は通常コンボと同じ仕組み（MeleeComboController）に乗せる
    // オートコンボのルートが固有技で締める回なら、ボタンを押さなくても出す
    const bool requested = input->TriggerAction(Input::Action::kSkill) || autoCombo_.ConsumeSkillRequest();
    if (!requested || cooldown > 0.0f || meleeCombo_.IsSequenceActive()) {
        return false;
    }
    const ComboArray<MeleeAttackDef>& sequence = GetSkillSequence(type);
    if (sequence.count <= 0) {
        return false;
    }
    // 1段目のモーションは次フレームのHandleMeleeCombat()で段の開始として再生される
    meleeCombo_.StartSequence(sequence);
    cooldown = cooldownSeconds;
    return true;
}

/** @brief 今のリグの指定クリップを攻撃モーションとして再生する */
void Player::PlaySkillAnim(engine::graphics::Animation CharacterRig::* clip, float speed) {
    PlayAttackAnim(rig_->*clip, speed);
}

/** @brief 体を空中で回す（1周したら0へ戻す） */
void Player::AddSpin(float degrees) {
    spinAngle_ += degrees;
    if (spinAngle_ >= kFullTurnDegrees) {
        spinAngle_ -= kFullTurnDegrees;
    }
}

/** @brief 投げた武器などを浮かせる高さの位置（足元の pos_ から胸の高さへ上げた位置） */
Vector3 Player::GetSkillHoldPosition() const {
    return { pos_.x, pos_.y + Tuning().skillHoldHeight, pos_.z };
}

/** @brief グレートソード: 投げ回転斬りで大剣が静止し渦を巻いている最中か（飛行中・帰還中はfalse） */
bool Player::IsGreatswordSpinning() const {
    return weaponBehaviors_->greatsword.IsSpinning();
}

/** @brief 投げた大剣の今の位置を返す */
Vector3 Player::GetGreatswordThrowPos() const {
    return weaponBehaviors_->greatsword.GetRestPosition();
}

/** @brief 斧の強化（ダメージ上昇）の最中かを返す */
bool Player::IsAxeEnraged() const {
    return weaponBehaviors_->axe.IsEnraged();
}

/** @brief アックスの怒り強化中に攻撃力へ掛けるべき倍率を返す（強化中でなければ1.0） */
float Player::GetAxeRageMult() const {
    return IsAxeEnraged() ? Tuning().axeRageDamageMult : 1.0f;
}

} // namespace engine::game
