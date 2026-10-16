#include "Player.h"
using namespace engine::game;

void Player::EndRampage()
{
        if (rampage_->IsJuggling()) {
            rampage_ = &InactiveRampage();
        }
    }

void Player::SetHorizontalBounds(float minX, float maxX)
{
        minX_ = minX;
        maxX_ = maxX;
    }

Collider Player::GetCollider() const
{
        Collider c;
        c.SetAsAABB({ { pos_.x - kBodyHalfExtent_, pos_.y - kBodyHalfExtent_, -kBodyHalfExtent_ },
            { pos_.x + kBodyHalfExtent_, pos_.y + kBodyHalfExtent_, kBodyHalfExtent_ } });
        return c;
    }

bool Player::ConsumeJustDodge()
{
        if (!dodgeActive_ || dodgeRewardClaimed_) {
            return false;
        }
        dodgeRewardClaimed_ = true;
        return true;
    }

void Player::ChargeAwakenGauge(float amount)
{
        if (!isAwakened_) {
            awakenGauge_ = (std::min)(awakenGauge_ + amount * skillMods_.gaugeChargeMult, 1.0f);
        }
    }


bool Player::JustWeaponSkillHit() const
{
    // 固有技の攻撃判定になる出来事（シーン側はどれか1つでも立っていれば周囲へ判定を出す）
    static constexpr FrameEvent kSkillHitEvents[] = {
        FrameEvent::GreatswordSlam,
        FrameEvent::SpinShot,
        FrameEvent::ScytheSpin,
        FrameEvent::AxeCharge,
    };
    for (FrameEvent event : kSkillHitEvents) {
        if (HasFrameEvent(event)) {
            return true;
        }
    }
    return false;
}
