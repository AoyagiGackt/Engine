/**
 * @file Player.cpp
 * @brief プレイヤー（Player）の初期化と1フレームの更新の実装
 */
#include "Player.h"
#include "GroundSupport.h"
#include "CharacterVisuals.h"
#include "Easing.h"
#include "GameConstants.h"
#include "GravityBody.h"
#include "Input.h"
#include "ModelCommon.h"
#include "ModelManager.h"
#include "OutlineEffect.h"
#include "PipelineStateGuard.h"
#include "PlayerTuning.h"
#include "Weapon.h"
#include "WeaponManager.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>
using namespace engine::graphics;

namespace engine::game {
namespace {
/** @brief 読み込み済みの調整値を取得する */
const PlayerTuning& Tuning() {
    return PlayerTuning::Get();
}

/** @brief 体の移動キーの高さを最初の値に固定する（高さは物理で決めるため） */
void LockClipVerticalRootMotion(Animation& animation) {
    auto body = animation.nodeAnimations.find("Body");
    if (body == animation.nodeAnimations.end() || body->second.translate.keyframes.empty()) {
        return;
    }

    const float baseY = body->second.translate.keyframes.front().value.y;
    for (auto& keyframe : body->second.translate.keyframes) {
        keyframe.value.y = baseY;
    }
}
}

// ══════════════════════════════════════════════════════
// 初期化
// ══════════════════════════════════════════════════════

/** @brief 描画基盤・アニメーション・手持ち武器とプレイヤー状態を初期化する */
void Player::Initialize(ModelCommon* modelCommon) {
    modelCommon_ = modelCommon;

    // スキンメッシュ共通設定（両フォームのリグで共有）
    skinCommon_ = SkinnedObject3d::CreateSharedCommon(modelCommon);

    // 見た目リグの共通セットアップ（静的モデル + スキンモデル + アニメーション一式）
    auto initRig = [&](CharacterRig& rig, const RigVisualDef& def) {
        // 残像・分身演出用の静的モデル（ボーンなし、ボーン付きモデルと同じ見た目）
        rig.staticModel = ModelManager::GetInstance()->GetOrLoad(modelCommon, def.staticModelPath, def.texture);

        // 本体（ボーンアニメーション付き）
        std::string modelPath = std::string(def.dir) + "/" + def.file;
        rig.skinnedModel = ModelManager::GetInstance()->GetOrLoadSkinned(modelCommon->GetDxCommon(), modelPath, def.texture);

        rig.modelScale = def.scale;
        rig.modelOffsetY = def.offsetY;
        rig.meleeBoneName = def.meleeBone;
        rig.gunBoneName = def.gunBone;
        rig.motionRigId = def.file;

        rig.idleAnim = LoadAnimationFile(def.dir, def.file, def.idle);
        rig.runAnim = LoadAnimationFile(def.dir, def.file, def.run);
        rig.jumpAnim = LoadAnimationFile(def.dir, def.file, def.jump);
        rig.runningJumpAnim = LoadAnimationFile(def.dir, def.file, def.runningJump);
        LockClipVerticalRootMotion(rig.jumpAnim);
        LockClipVerticalRootMotion(rig.runningJumpAnim);
        rig.swimAnim = LoadAnimationFile(def.dir, def.file, def.swim);
        rig.idleHoldAnim = LoadAnimationFile(def.dir, def.file, def.idleHold);
        // 待機時に体が上下しないよう、装備あり・なしの両方の待機の高さを固定する
        LockClipVerticalRootMotion(rig.idleAnim);
        LockClipVerticalRootMotion(rig.idleHoldAnim);
        rig.runHoldAnim = LoadAnimationFile(def.dir, def.file, def.runHold);
        rig.slashAnim = LoadAnimationFile(def.dir, def.file, def.slash);
        rig.punchAnim = LoadAnimationFile(def.dir, def.file, def.punch);

        rig.object = std::make_unique<SkinnedObject3d>();
        rig.object->Initialize(skinCommon_.get());
        rig.object->SetModel(rig.skinnedModel);
        rig.object->SetSkeleton(Skeleton::Create(LoadNodeHierarchyFromFile(def.dir, def.file)));
        rig.object->SetPoseLayer(&motionLayer_);
        rig.object->SetAnimation(rig.idleAnim);
        // ライティング有効 + リムライトで背景からシルエットを分離させる
        rig.object->SetEnableLighting(true);
        rig.object->SetRimColor(kRimColor);
        rig.object->SetRimPower(kRimPower);
        rig.object->SetRimIntensity(kRimIntensity);
        rig.object->SetEnableRim(true);
        rig.object->SetScale({ def.scale, def.scale, def.scale });
        rig.object->SetPosition({ pos_.x, pos_.y + def.offsetY + groundVisualCorrection_, pos_.z });
        rig.object->Update();
    };
    initRig(normalRig_, kNormalRigVisual);
    initRig(awakenedRig_, kAwakenedRigVisual);
    rig_ = &normalRig_;
    animState_ = &IdleAnim();

    afterImageRenderer_.Initialize(modelCommon, rig_->staticModel, rig_->modelScale);

    // 手持ち武器の共通セットアップ（読み込み + リムライト設定）
    auto initHeldWeapon = [&](Model*& model, std::unique_ptr<Object3d>& object,
                              const char* modelPath, const char* texturePath) {
        model = ModelManager::GetInstance()->GetOrLoad(modelCommon, modelPath, texturePath);
        object = std::make_unique<Object3d>();
        object->Initialize(modelCommon);
        object->SetModel(model);
        object->SetEnableLighting(true);
        object->SetRimColor(kRimColor);
        object->SetRimPower(kRimPower);
        object->SetRimIntensity(kRimIntensity);
        object->SetEnableRim(true);
    };

    /** @brief 武器表の握り姿勢と描画実体を対応するスロットへ組み立て、所有権を移す */
    auto initializeSlots = [&](auto& slots, const auto& assets) {
        using Slot = typename std::decay_t<decltype(slots)>::value_type;
        slots.clear();
        for (const auto& asset : assets) {
            Slot slot;
            slot.type = asset.type;
            slot.gripScale = asset.gripScale;
            slot.gripRotate = asset.gripRotate;
            slot.gripTranslate = asset.gripTranslate;
            initHeldWeapon(slot.model, slot.object, asset.modelPath, asset.texturePath);
            slots.push_back(std::move(slot));
        }
    };
    // 右手の近接武器と、左手の銃を同じ手順で準備する。表示対象の選択は各更新処理が行う。
    initializeSlots(heldWeapons_, kHeldWeaponVisuals);
    initializeSlots(guns_, kGunVisuals);
}

/** @brief 武器奪取の刺突モーションを頭から再生する（外部トリガー用、既存の斬撃を流用） */
void Player::PlayStealStab() {
    // 武器奪取の刺突（専用素材が無いため斬撃を流用）
    PlayAttackAnim(rig_->slashAnim, Tuning().stealStabAnimSpeed);
}

/** @brief 被弾時の無敵時間を開始する */
void Player::OnHit() {
    invincibleTimer_ = Tuning().invincibleDuration;
}

/** @brief スキル補正込みのコンボ最大数を返す（現在の武器の地上コンボ段数基準） */
int Player::GetComboMax() const {
    if (!WeaponManager::GetInstance()->HasEquippedWeapon()) {
        return 0;
    }
    // 現在の武器の地上コンボ段数を基準にする（HUDのx段目/最大表示用）
    const MeleeComboSet& set = GetMeleeComboSet(WeaponManager::GetInstance()->GetCurrent().type);
    return set.ground.count + skillMods_.comboMaxBonus;
}

/** @brief 現在の武器と覚醒状態から攻撃ダメージ倍率を返す（覚醒中はweapons.jsonのawakened.damageMult） */
float Player::GetAwakenedDamageMult() const {
    const auto* wm = WeaponManager::GetInstance();
    return (isAwakened_ && wm->HasEquippedWeapon()) ? wm->GetCurrent().awakened.damageMult : 1.0f;
}

/** @brief 現在の武器と覚醒状態から固有技の判定範囲倍率を返す（非覚醒時は1.0） */
float Player::GetAwakenedSkillRadiusMult() const {
    const auto* wm = WeaponManager::GetInstance();
    return (isAwakened_ && wm->HasEquippedWeapon()) ? wm->GetCurrent().awakened.skillRadiusMult : 1.0f;
}

/** @brief 現在の武器と覚醒状態からノックバック倍率を返す（非覚醒時は1.0） */
float Player::GetAwakenedKnockbackMult() const {
    const auto* wm = WeaponManager::GetInstance();
    return (isAwakened_ && wm->HasEquippedWeapon()) ? wm->GetCurrent().awakened.knockbackMult : 1.0f;
}

/** @brief 回避ボタンの押下を先行入力として記憶する */
void Player::BufferDodgeInput(Input* input) {
    if (input->TriggerAction(Input::Action::kDodge)) {
        dodgeBufferFrames_ = Tuning().dodgeBufferFrames;
    }
}

/** @brief 回避入力の受付と回避移動の進行を処理する */
void Player::HandleDodge(Input* input) {
    dodgeCooldown_ = (std::max)(dodgeCooldown_ - gameConstants::kFrameDeltaTime, 0.0f);
    dodgeSpamTimer_ += gameConstants::kFrameDeltaTime;

    // 先行入力の残りフレームを消費しつつ、このフレームの押下も同じバッファに載せる
    // （シーンがBufferDodgeInput()を呼んでいなくても直押しで回避できるように）
    BufferDodgeInput(input);
    const bool dodgeRequested = dodgeBufferFrames_ > 0;
    dodgeBufferFrames_ = (std::max)(dodgeBufferFrames_ - 1, 0);

    // 進行中の回避を進める（イージングで滑らかに移動し、終わったら無敵を解く）
    if (dodgeActive_) {
        if (AdvanceHorizontalMove(dodgeTimer_, Tuning().dodgeDuration, dodgeStartX_, dodgeTargetX_)) {
            dodgeActive_ = false;
            dodgeCooldown_ = Tuning().dodgeCooldown;
        }
        return;
    }

    const bool blocked = inWater_ || finisherCharging_ || IsRampaging()
        || dodgeCooldown_ > 0.0f || warpActive_;
    if (blocked || !dodgeRequested) {
        return;
    }
    dodgeBufferFrames_ = 0; // 消費した先行入力で二重に回避しない

    // 方向は移動入力優先で、その方向を向いて前転する
    // 無ければ向いている方向の逆へバックステップ（向き自体は変えない＝敵を見たまま退く）
    const float heldDirX = GetHeldMoveDirection(input);
    dodgeBackward_ = heldDirX == 0.0f;
    const float dirX = dodgeBackward_ ? -lastDirX_ : heldDirX;
    if (!dodgeBackward_) {
        lastDirX_ = dirX;
    }

    // 短い間隔で繰り返した回避は連打として数える（危険の無い場面での乱用をスタイル評価で咎めるため）
    dodgeSpamCount_ = dodgeSpamTimer_ < Tuning().dodgeSpamWindow ? dodgeSpamCount_ + 1 : 0;
    dodgeSpamTimer_ = 0.0f;

    dodgeActive_ = true;
    SetFrameEvent(FrameEvent::kDodged);
    dodgeRewardClaimed_ = false;
    dodgeTimer_ = 0.0f;
    dodgeStartX_ = pos_.x;
    dodgeTargetX_ = std::clamp(pos_.x + dirX * Tuning().dodgeDistance, minX_, maxX_);

    // 回避はコンボの逃げ道。振りかけの段は打ち切り、踏み込み系の技も止める
    meleeCombo_.Reset();
    InterruptWeaponSkills();
    // バックステップは立ちジャンプ、前転は走りジャンプのモーションを速めて見せる
    const PlayerTuning& t = Tuning();
    PlayAttackAnim(dodgeBackward_ ? rig_->jumpAnim : rig_->runningJumpAnim,
        dodgeBackward_ ? t.backDodgeAnimSpeed : t.dodgeAnimSpeed);
}

/** @brief 時間を正規化して減速補間し、共通のステージ境界に収めてから終了を判定する */
bool Player::AdvanceHorizontalMove(float& timer, float duration, float startX, float targetX) {
    timer += gameConstants::kFrameDeltaTime;
    const float progress = std::clamp(timer / duration, 0.0f, 1.0f);
    pos_.x = std::clamp(startX + (targetX - startX) * easing::EaseOutQuad(progress), minX_, maxX_);
    return progress >= 1.0f;
}

/** @brief テレポート斬り入力の受付とワープ移動の進行を処理する */
void Player::HandleWarp(Input* input) {
    warpCooldown_ = (std::max)(warpCooldown_ - gameConstants::kFrameDeltaTime, 0.0f);

    // 進行中のワープを進める（イージングで滑らかに移動し、終わったら無敵を解く）
    if (warpActive_) {
        if (AdvanceHorizontalMove(warpTimer_, Tuning().warpDuration, warpStartX_, warpTargetX_)) {
            warpActive_ = false;
            warpCooldown_ = isAwakened_ ? Tuning().warpAwakenedCooldown : Tuning().warpCooldown;
        }
        return;
    }

    const bool blocked = inWater_ || finisherCharging_ || IsRampaging()
        || warpCooldown_ > 0.0f || dodgeActive_;
    const bool hasGauge = isAwakened_ || awakenGauge_ >= Tuning().warpGaugeCost;
    if (blocked || !hasGauge || !input->TriggerAction(Input::Action::kWarp)) {
        return;
    }

    if (!isAwakened_) {
        awakenGauge_ -= Tuning().warpGaugeCost;
    }

    warpActive_ = true;
    warpTimer_ = 0.0f;
    warpStartX_ = pos_.x;
    const float distance = isAwakened_ ? Tuning().warpAwakenedDistance : Tuning().warpDistance;
    warpTargetX_ = std::clamp(pos_.x + lastDirX_ * distance, minX_, maxX_);

    // ワープはコンボの逃げ道兼割り込み技。振りかけの段は打ち切り、踏み込み系の技も止める
    meleeCombo_.Reset();
    InterruptWeaponSkills();
    PlayAttackAnim(rig_->slashAnim, Tuning().warpAnimSpeed);
}

/** @brief 攻撃モーションを頭から再生し、再生し切るまで状態遷移をロックする */
void Player::PlayAttackAnim(const Animation& anim, float speed) {
    rig_->object->SetAnimation(anim);
    rig_->object->SetAnimSpeed(speed);
    animState_ = &AttackAnim();
    attackAnimTimer_ = anim.duration / speed;
}

// ══════════════════════════════════════════════════════
// フレーム更新
// ══════════════════════════════════════════════════════

/** @brief 入力・移動・戦闘・アニメーションなどプレイヤーの状態を1フレーム分更新する */
void Player::Update(Input* input, const Vector3& enemyPos, bool enemySolid, bool useStageFloor) {
    useStageFloor_ = useStageFloor;
    ResetFrameFlags();

    if (invincibleTimer_ > 0.0f) {
        invincibleTimer_ -= gameConstants::kFrameDeltaTime;
    }

    justDodgeWindowTimer_ = (std::max)(justDodgeWindowTimer_ - gameConstants::kFrameDeltaTime, 0.0f);

    HandleStyleSwitch(input);
    HandleDodge(input);
    HandleWarp(input);

    GetPhysicsState(inWater_).Update(*this, input);
    pos_.x = std::clamp(pos_.x, minX_, maxX_);
    if (onGround_) {
        NotifyWeaponSkillsLanded(); // 空中ダッシュなどは着地で回復する
    }

    HandleRangedCombat(input);
    HandleMeleeCombat(input, enemyPos);
    HandleFinisherSlash(input);
    HandleWeaponSkill(input);
    UpdateRampagePhysics(enemyPos);
    UpdateAwakenState(input);
    if (enemySolid) {
        ResolveEnemyOverlap(enemyPos);
    }
    UpdateWaterState();
    UpdateVisualState(input);
    AttachActiveWeapons();
}

/** @brief 指定した位置の真下にある床へ立った時の位置を返す（足元より下で一番高いブロックの上面に足を合わせる） */
Vector3 Player::FindStandingPosition(const Vector3& position, const std::vector<AABB>& blocks) const {
    const auto floor = FindSupportingFloor(position, kBodyHalfExtent, kBodyHalfExtent, kBodyHalfExtent, blocks);
    return floor ? Vector3 { position.x, *floor + kBodyHalfExtent, position.z } : position;
}

/** @brief ステージ上のsolidブロックとの当たり判定を解決する（Update()の後に毎フレーム呼ぶ） */
void Player::ResolveBlockCollision(const std::vector<AABB>& blocks) {
    groundVisualCorrection_ = 0.0f;
    if (blocks.empty()) {
        if (pos_.y > Tuning().groundY && velocityY_ <= 0.0f) {
            onGround_ = false;
        }
        return;
    }

    // プレイヤーの当たり判定はダミー等と同じpos_を中心とした1x1x1規約に合わせる
    constexpr float kHalf = kBodyHalfExtent;

    // 上昇中に頭がブロック下面を横切った場合は、下面の直下へ戻して上昇速度を止める。
    if (velocityY_ > 0.0f) {
        const float previousHeadY = pos_.y - velocityY_ + kHalf;
        const float currentHeadY = pos_.y + kHalf;
        float nearestCeiling = (std::numeric_limits<float>::max)();
        for (const auto& b : blocks) {
            const bool overlapXZ = (pos_.x + kHalf) > b.min.x && (pos_.x - kHalf) < b.max.x
                && (pos_.z + kHalf) > b.min.z && (pos_.z - kHalf) < b.max.z;
            if (!overlapXZ) {
                continue;
            }
            if (previousHeadY <= b.min.y && currentHeadY >= b.min.y) {
                nearestCeiling = (std::min)(nearestCeiling, b.min.y);
            }
        }
        if (nearestCeiling != (std::numeric_limits<float>::max)()) {
            pos_.y = nearestCeiling - kHalf;
            velocityY_ = 0.0f;
            onGround_ = false;
        }
    }

    // 1フレームの落下量は側面判定の余白より大きくなり得るため、今フレームで上面を
    // 上から跨いだブロックは着地扱いにする（側面押し出しで横へ弾くと着地できない）
    const float feetYBeforeResolve = pos_.y - kHalf;
    const float prevFeetY = pos_.y - velocityY_ - kHalf;
    // 上昇する床は1フレームで足元へ少し食い込むことがある。
    // その食い込みを側面衝突として横へ押し出さず、上面への着地として扱う。
    // 60fps時の移動床の最大移動量を少しだけ上回る値に留める。
    // 大きすぎる許容値は、床から離れた後も上面へ吸着して浮いて見える原因になる。
    constexpr float kMovingFloorTopTolerance = 0.04f;
    auto landedOnTop = [&](const AABB& b) {
        const bool crossedTopWhileFalling = prevFeetY >= b.max.y - kTopCrossTolerance;
        const bool shallowMovingFloorPenetration = onGround_
            && feetYBeforeResolve >= b.max.y - kMovingFloorTopTolerance
            && feetYBeforeResolve <= b.max.y + kMovingFloorTopTolerance;
        return velocityY_ <= 0.0f && (crossedTopWhileFalling || shallowMovingFloorPenetration);
    };

    // 水平方向  側面から重なっているブロックがあれば侵入量が小さい側へ押し出す
    for (const auto& b : blocks) {
        if (landedOnTop(b)) {
            continue;
        }
        bool overlapY = (pos_.y + kHalf) > b.min.y + kSideOverlapMargin && (pos_.y - kHalf) < b.max.y - kSideOverlapMargin;
        if (!overlapY) {
            continue;
        } // 乗っているだけの上面はここでは無視する

        bool overlapX = (pos_.x + kHalf) > b.min.x && (pos_.x - kHalf) < b.max.x;
        bool overlapZ = (pos_.z + kHalf) > b.min.z && (pos_.z - kHalf) < b.max.z;
        if (!overlapX || !overlapZ) {
            continue;
        }

        float pushLeft = b.min.x - (pos_.x + kHalf); // 負値＝左へ押し出す量
        float pushRight = b.max.x - (pos_.x - kHalf); // 正値＝右へ押し出す量

        // 横スクロールなので左右だけに押し出す。奥行き（Z）へ押し出すと、突進などで深くめり込んだ時に
        // ステージの奥や手前の列へずれ、壁や床の中に埋まったまま戻れなくなる
        // 移動前にブロックの左右どちらにいたかが分かれば、入ってきた側へ戻す
        // （突進やワープで一気に動いて壁の中心を越えても、壁の向こうへ抜けないように）
        const bool cameFromLeft = prevFrameX_ + kHalf <= b.min.x + kSideOverlapMargin;
        const bool cameFromRight = prevFrameX_ - kHalf >= b.max.x - kSideOverlapMargin;
        if (cameFromLeft) {
            pos_.x += pushLeft;
        } else if (cameFromRight) {
            pos_.x += pushRight;
        } else {
            pos_.x += (std::abs(pushLeft) < std::abs(pushRight)) ? pushLeft : pushRight;
        }
    }

    // 垂直方向  足元付近に上面があるブロックのうち一番高いものへ着地させる
    float feetY = pos_.y - kHalf;
    float bestTop = useStageFloor_ ? (std::numeric_limits<float>::lowest)() : Tuning().groundY;
    // 壁のように同じX/Zに積み重なったブロックの途中の継ぎ目に、ジャンプで壁へ突っ込んだ際に
    // 乗ってしまわないよう、頭上（立った時に体が収まる高さ）に別のブロックが無いことも確認する
    auto hasClearanceAbove = [&](const AABB& candidate) {
        for (const auto& other : blocks) {
            if (&other == &candidate) {
                continue;
            }
            bool otherOverlapXZ = (pos_.x + kHalf) > other.min.x && (pos_.x - kHalf) < other.max.x
                && (pos_.z + kHalf) > other.min.z && (pos_.z - kHalf) < other.max.z;
            if (!otherOverlapXZ) {
                continue;
            }
            if (other.min.y < candidate.max.y + kHalf * 2.0f && other.max.y > candidate.max.y) {
                return false;
            }
        }
        return true;
    };
    // 支持判定は体の幅よりかなり狭くし、見た目上ブロックの外（空中）なのに乗れてしまうのを防ぐ
    constexpr float kSupportHalf = 0.15f;
    for (const auto& b : blocks) {
        bool overlapXZ = (pos_.x + kSupportHalf) > b.min.x && (pos_.x - kSupportHalf) < b.max.x
            && (pos_.z + kSupportHalf) > b.min.z && (pos_.z - kSupportHalf) < b.max.z;
        if (!overlapXZ) {
            continue;
        }

        // X/Z が重なっている床のうち、足元より上に出ていない一番高い面を採用する
        // 床をエディタで上下させたときも、古い高さに張り付かないようにする
        // 上から跨いで落ちてきたフレームは、めり込み量が段差許容を超えていても着地を成立させる
        const bool reachable = b.max.y <= feetY + kGroundStepTolerance || landedOnTop(b);
        if (reachable && (b.max.y + kHalf) > bestTop && hasClearanceAbove(b)) {
            bestTop = b.max.y + kHalf;
        }
    }
    if (velocityY_ <= 0.0f && pos_.y <= bestTop + kLandingSnapTolerance) {
        pos_.y = bestTop;
        velocityY_ = 0.0f;
        onGround_ = true;
        SetFrameEvent(FrameEvent::kLanded, !prevOnGround_);
        NotifyWeaponSkillsLanded();
    } else if (velocityY_ <= 0.0f) {
        onGround_ = false;
        SetFrameEvent(FrameEvent::kLanded, false);
    }

    pos_.x = std::clamp(pos_.x, minX_, maxX_);
}

// ══════════════════════════════════════════════════════
// 表示更新と描画
// ══════════════════════════════════════════════════════

/** @brief 現在位置から worldDeltaX だけ先（移動範囲内に収める）へのダッシュを開始する */
void Player::BeginDash(DashMotion& dash, float worldDeltaX) {
    dash.active = true;
    dash.timer = 0.0f;
    dash.startX = pos_.x;
    dash.targetX = std::clamp(pos_.x + worldDeltaX, minX_, maxX_);
}

/** @brief ダッシュを1フレーム進める（pos_.xを更新）。ちょうど完了したフレームだけtrueを返す */
bool Player::AdvanceDash(DashMotion& dash) {
    if (!dash.active) {
        return false;
    }
    dash.timer += gameConstants::kFrameDeltaTime;
    const float t = std::clamp(dash.timer / Tuning().dashDuration, 0.0f, 1.0f);
    pos_.x = dash.startX + (dash.targetX - dash.startX) * easing::EaseOutQuad(t);
    if (t >= 1.0f) {
        dash.active = false;
        return true;
    }
    return false;
}

} // namespace engine::game
