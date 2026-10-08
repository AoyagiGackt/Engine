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
        c.SetAsAABB({ { pos_.x - 0.5f, pos_.y - 0.5f, -0.5f },
            { pos_.x + 0.5f, pos_.y + 0.5f, 0.5f } });
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

bool Player::IsGreatswordSpinning() const
{
        if (!greatswordThrowActive_) {
            return false;
        }
        const float spinElapsed = greatswordThrowTimer_ - kGreatswordThrowTravelTime_;
        return spinElapsed >= 0.0f && spinElapsed < kGreatswordVortexMaxDuration_;
    }
