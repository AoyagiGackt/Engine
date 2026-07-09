#include "Player.h"
#include "CharacterVisuals.h"
#include "GameConstants.h"
#include "Input.h"
#include "ModelCommon.h"
#include "OutlineEffect.h"
#include "Weapon.h"
#include "WeaponManager.h"
#include <algorithm>
#include <cmath>
using namespace engine;
using namespace engine::graphics;
using namespace engine::game;

namespace {
// 本体・武器の黒縁アウトライン（背景との同化と武器の視認性の低さを補う）
// 幅・濃さともに控えめにし、「異常が起きているように見える」ほど主張しないようにする
constexpr Vector4 kOutlineColor = { 0.0f, 0.0f, 0.0f, 0.55f };
constexpr float   kOutlineWidth = 0.006f;

// 斬撃モーションの再生速度倍率（コンボのテンポに合わせて少し速める）
constexpr float kAttackAnimSpeed = 1.5f;

// フィニッシャー：静止集中の長さ（GamePlayScene/BattleTestScene の斬撃線バラマキ演出と一致させる：
// kFinisherChargeDelay → 斬撃線を kFinisherSlashLines 本 kFinisherLineInterval 間隔で出す → kFinisherImpactDelay で解放）
// 専用の納刀ポーズ素材が無いため、Idle/IdleHold を静止させて代用する
constexpr float kFinisherChargeDuration   = GameConstants::kFinisherChargeDelay
                                           + GameConstants::kFinisherSlashLines * GameConstants::kFinisherLineInterval
                                           + GameConstants::kFinisherImpactDelay;
constexpr float kFinisherReleaseAnimSpeed = 3.0f; // 解放の一閃は目にも留まらぬ速さで

// 武器奪取の刺突（ぶっ刺す→奪う演出の仮モーション、専用素材が無いため斬撃を流用）
constexpr float kStealStabAnimSpeed = 1.2f;
}

void Player::Initialize(ModelCommon* modelCommon)
{
    modelCommon_ = modelCommon;

    // スキンメッシュ共通設定（両フォームのリグで共有）
    skinCommon_ = std::make_unique<SkinCommon>();
    skinCommon_->Initialize(modelCommon->GetDxCommon());

    SkinnedObject3d::SetCommonModelCommon(modelCommon);
    SkinnedObject3d::SetCommonCamera(Object3d::GetCommonCamera());

    // 見た目リグの共通セットアップ（静的モデル + スキンモデル + アニメーション一式）
    auto initRig = [&](CharacterRig& rig, const RigVisualDef& def) {
        // 残像・分身演出用の静的モデル（ボーンなし、ボーン付きモデルと同じ見た目）
        rig.staticModel = std::make_unique<Model>();
        rig.staticModel->Initialize(modelCommon, def.staticModelPath, def.texture);

        // 本体（ボーンアニメーション付き）
        std::string modelPath = std::string(def.dir) + "/" + def.file;
        rig.skinnedModel = std::make_unique<SkinnedModel>();
        rig.skinnedModel->Initialize(modelCommon->GetDxCommon(), modelPath, def.texture);

        rig.modelScale    = def.scale;
        rig.modelOffsetY  = def.offsetY;
        rig.meleeBoneName = def.meleeBone;
        rig.gunBoneName   = def.gunBone;

        rig.idleAnim        = LoadAnimationFile(def.dir, def.file, def.idle);
        rig.runAnim         = LoadAnimationFile(def.dir, def.file, def.run);
        rig.jumpAnim        = LoadAnimationFile(def.dir, def.file, def.jump);
        rig.runningJumpAnim = LoadAnimationFile(def.dir, def.file, def.runningJump);
        rig.swimAnim        = LoadAnimationFile(def.dir, def.file, def.swim);
        rig.idleHoldAnim    = LoadAnimationFile(def.dir, def.file, def.idleHold);
        rig.runHoldAnim     = LoadAnimationFile(def.dir, def.file, def.runHold);
        rig.slashAnim       = LoadAnimationFile(def.dir, def.file, def.slash);
        rig.punchAnim       = LoadAnimationFile(def.dir, def.file, def.punch);

        rig.object = std::make_unique<SkinnedObject3d>();
        rig.object->Initialize(skinCommon_.get());
        rig.object->SetModel(rig.skinnedModel.get());
        rig.object->SetSkeleton(Skeleton::Create(LoadNodeHierarchyFromFile(def.dir, def.file)));
        rig.object->SetAnimation(rig.idleAnim);
        // ライティング有効 + リムライトで背景からシルエットを分離させる
        rig.object->SetEnableLighting(true);
        rig.object->SetRimColor({ 0.4f, 0.9f, 1.0f });
        rig.object->SetRimPower(2.5f);
        rig.object->SetRimIntensity(1.2f);
        rig.object->SetEnableRim(true);
        rig.object->SetScale({ def.scale, def.scale, def.scale });
        rig.object->SetPosition({ pos_.x, pos_.y + def.offsetY, pos_.z });
        rig.object->Update();
    };
    initRig(normalRig_,   kNormalRigVisual);
    initRig(awakenedRig_, kAwakenedRigVisual);
    rig_ = &normalRig_;
    animState_ = AnimState::Idle;

    afterImageRenderer_.Initialize(modelCommon, rig_->staticModel.get(), rig_->modelScale);

    // 手持ち武器の共通セットアップ（読み込み + リムライト設定）
    auto initHeldWeapon = [&](std::unique_ptr<Model>& model, std::unique_ptr<Object3d>& object,
                              const char* modelPath, const char* texturePath) {
        model = std::make_unique<Model>();
        model->Initialize(modelCommon, modelPath, texturePath);
        object = std::make_unique<Object3d>();
        object->Initialize(modelCommon);
        object->SetModel(model.get());
        object->SetEnableLighting(true);
        object->SetRimColor({ 0.4f, 0.9f, 1.0f });
        object->SetRimPower(2.5f);
        object->SetRimIntensity(1.2f);
        object->SetEnableRim(true);
    };

    // 右手武器（トランスフォームは毎フレーム SetLocalMatrix で与える、現在のスタイルの1つだけ表示）
    heldWeapons_.clear();
    for (const auto& asset : kHeldWeaponVisuals) {
        HeldWeaponSlot slot;
        slot.type          = asset.type;
        slot.gripScale     = asset.gripScale;
        slot.gripRotate    = asset.gripRotate;
        slot.gripTranslate = asset.gripTranslate;
        initHeldWeapon(slot.model, slot.object, asset.modelPath, asset.texturePath);
        heldWeapons_.push_back(std::move(slot));
    }

    // 銃（左手ボーン追従、Gキーで切り替えた1丁だけ表示）
    guns_.clear();
    for (const auto& asset : kGunVisuals) {
        GunSlot slot;
        slot.type          = asset.type;
        slot.gripScale     = asset.gripScale;
        slot.gripRotate    = asset.gripRotate;
        slot.gripTranslate = asset.gripTranslate;
        initHeldWeapon(slot.model, slot.object, asset.modelPath, asset.texturePath);
        guns_.push_back(std::move(slot));
    }
}

void Player::UpdateAnimationState(bool isMoving)
{
    // フィニッシャー溜め中は静止ポーズを維持（タイマー管理は Update() 側）
    if (finisherCharging_) { return; }

    // 攻撃モーション中は再生し切るまで状態遷移しない
    if (attackAnimTimer_ > 0.0f) {
        attackAnimTimer_ -= GameConstants::kFrameDeltaTime;
        if (attackAnimTimer_ > 0.0f) { return; }
        rig_->object->SetAnimSpeed(1.0f); // 攻撃用の速度倍率を戻す
    }

    AnimState newState = inWater_    ? AnimState::Swim
                        : !onGround_ ? AnimState::Jump
                        : isMoving   ? AnimState::Run
                                     : AnimState::Idle;
    // 構え系の近接武器、または銃を左手に構えている間は武器持ちバリエーション（IdleHold/RunHold）を使う
    // （銃は常時左手に追従表示されるため、素のIdle/Runのままだと構えていないように見えてしまう）
    const Skeleton& skel = rig_->object->GetSkeleton();
    bool hasGunBone = skel.jointMap.find(rig_->gunBoneName) != skel.jointMap.end();
    bool hold = UsesHoldPose(WeaponManager::GetInstance()->GetCurrent().type) || hasGunBone;
    if (newState == animState_ && hold == animHold_) { return; }
    animState_ = newState;
    animHold_  = hold;

    switch (animState_) {
    case AnimState::Swim: rig_->object->SetAnimation(rig_->swimAnim); break;
    case AnimState::Run:  rig_->object->SetAnimation(hold ? rig_->runHoldAnim : rig_->runAnim); break;
    case AnimState::Jump: rig_->object->SetAnimation(isMoving ? rig_->runningJumpAnim : rig_->jumpAnim); break;
    default:              rig_->object->SetAnimation(hold ? rig_->idleHoldAnim : rig_->idleAnim); break;
    }
}

void Player::PlayStealStab()
{
    PlayAttackAnim(rig_->slashAnim, kStealStabAnimSpeed);
}

int Player::GetComboMax() const
{
    // 現在の武器の地上コンボ段数を基準にする（HUDの「x段目/最大」表示用）
    const MeleeComboSet& set = GetMeleeComboSet(WeaponManager::GetInstance()->GetCurrent().type);
    return set.groundCount + skillMods_.comboMaxBonus;
}

void Player::PlayAttackAnim(const Animation& anim, float speed)
{
    rig_->object->SetAnimation(anim);
    rig_->object->SetAnimSpeed(speed);
    animState_       = AnimState::Attack;
    attackAnimTimer_ = anim.duration / speed;
}

void Player::Update(Input* input, const Vector3& enemyPos)
{
    justJumped_        = false;
    justLanded_        = false;
    justEnteredWater_  = false;
    justExitedWater_   = false;
    justComboHit_      = false;
    justFired_         = false;
    justBlinked_       = false;
    justChargedGauge_  = false;
    justSpinShot_      = false;
    justLaunched_      = false;
    justRampageHit_    = false;
    justRampageFinish_ = false;
    justFinisherSlash_ = false;
    prevOnGround_     = onGround_;
    prevInWater_      = inWater_;

    // スタイルチェンジ（1〜5キー）
    auto* wm = WeaponManager::GetInstance();
    for (int i = 0; i < wm->GetCount(); ++i) {
        if (input->TriggerKey(static_cast<uint8_t>(DIK_1 + i))) { wm->SelectIndex(i); }
    }

    GetPhysicsState(inWater_).Update(*this, input);

    pos_.x = std::clamp(pos_.x, kMinX_, kMaxX_);

    // ── 銃切り替え（G キー、循環）────────────────────────────────────
    if (input->TriggerKey(DIK_G)) {
        wm->SelectNextRanged();
        gunCombo_.Reset(); // 撃ちかけのコンボは持ち越さない
    }

    // ── 射撃コンボ（K キー、水上のみ）────────────────────────────────
    // 押下の瞬間ではなく段の shotTime で発砲する。段数・弾数・リコイルは銃種別の GunShotDef が持つ
    if (!inWater_ && !finisherCharging_ && input->TriggerKey(DIK_K)) {
        gunCombo_.TryShoot(wm->GetRanged().type);
    }
    gunCombo_.Update(GameConstants::kFrameDeltaTime);
    if (gunCombo_.JustShot()) {
        justFired_ = true;
    }
    // 前後移動（踏み込み / 反動バックステップ）。空中では暴れるので地上のみ
    if (onGround_ && gunCombo_.GetMoveDelta() != 0.0f) {
        pos_.x = std::clamp(pos_.x + lastDirX_ * gunCombo_.GetMoveDelta(), kMinX_, kMaxX_);
    }

    // ── 格闘コンボ / 打ち上げ / 乱舞（L キー、水上のみ）─────────────
    // S(↓)+L は打ち上げ技。覚醒ソードの L は従来どおり乱舞へ
    if (!inWater_ && !finisherCharging_ && input->TriggerKey(DIK_L)) {
        if (rampagePhase_ != RampagePhase::Inactive) {
            GetRampageState(rampagePhase_).HandleAttackInput(*this, input, enemyPos);
        } else if (wm->GetCurrent().type == WeaponType::Sword && isAwakened_) {
            // 乱舞開始：まず敵に向かって突進（打ち上げフェーズ）
            meleeCombo_.Reset();
            rampagePhase_     = RampagePhase::Launch;
            juggleSlashCount_ = 0;
            juggleAngleIdx_   = 0;
        } else {
            bool launcherInput = input->PushKey(DIK_S) || input->PushKey(DIK_DOWN);
            meleeCombo_.TryAttack(wm->GetCurrent().type, launcherInput, !onGround_);
        }
    }

    // ── 近接コンボ進行（段の開始・ヒット発火・前進・打ち上げ追撃猶予）──
    meleeCombo_.Update(GameConstants::kFrameDeltaTime);
    if (const MeleeAttackDef* atk = meleeCombo_.GetActive()) {
        if (meleeCombo_.JustStartedStep()) {
            // 段ごとにモーションを頭から再生（速度も段の定義に従う）
            PlayAttackAnim(atk->slashAnim ? rig_->slashAnim : rig_->punchAnim, atk->animSpeed);
        }
        if (meleeCombo_.JustHit()) {
            justComboHit_ = true;
            comboStep_    = meleeCombo_.GetStep();
            if (atk->launcher) { launchFollowTimer_ = kLaunchFollowWindow_; }
        }
        // 踏み込み（斬りながら前へ出ることで空振り感を減らす）
        if (meleeCombo_.GetLungeDelta() > 0.0f) {
            pos_.x = std::clamp(pos_.x + lastDirX_ * meleeCombo_.GetLungeDelta(), kMinX_, kMaxX_);
        }
        // 空中攻撃中は滞空（落下を弱めてエアコンボを繋ぎやすくする）
        if (!onGround_ && velocityY_ < 0.0f) { velocityY_ *= 0.5f; }
    }
    if (launchFollowTimer_ > 0.0f) {
        launchFollowTimer_ -= GameConstants::kFrameDeltaTime;
    }

    // ── フィニッシャースラッシュ（F キー、水上のみ、覚醒ゲージ満タン時のみ）─────
    // 静止して集中 → 溜め切ったら一閃、の2段構成にする
    if (!inWater_ && !isAwakened_ && !finisherCharging_ && input->TriggerKey(DIK_F) && awakenGauge_ >= 1.0f) {
        justFinisherSlash_   = true;
        awakenGauge_         = 0.0f; // ゲージを全消費
        finisherCharging_    = true;
        finisherChargeTimer_ = kFinisherChargeDuration;
        meleeCombo_.Reset(); // 進行中のコンボは打ち切って静止に入る
        {
            // 銃は常時左手に追従表示されるため、構え系近接武器と同様に扱う（UpdateAnimationStateと同じ判定）
            const Skeleton& skel = rig_->object->GetSkeleton();
            bool hasGunBone = skel.jointMap.find(rig_->gunBoneName) != skel.jointMap.end();
            bool hold = UsesHoldPose(wm->GetCurrent().type) || hasGunBone;
            rig_->object->SetAnimation(hold ? rig_->idleHoldAnim : rig_->idleAnim);
        }
        rig_->object->SetAnimSpeed(0.0f); // 呼吸すら止めるように完全静止
    }

    // ── フィニッシャー溜めの経過 → 解放（一閃）─────────────────────
    if (finisherCharging_) {
        finisherChargeTimer_ -= GameConstants::kFrameDeltaTime;
        if (finisherChargeTimer_ <= 0.0f) {
            finisherCharging_ = false;
            PlayAttackAnim(rig_->slashAnim, kFinisherReleaseAnimSpeed);
        }
    }

    // ── 攻撃ヒットによるゲージ蓄積 ───────────────────────────────────
    if (!isAwakened_) {
        if (justComboHit_) { awakenGauge_ = (std::min)(awakenGauge_ + 0.08f * skillMods_.gaugeChargeMult, 1.0f); }
        if (justFired_)    { awakenGauge_ = (std::min)(awakenGauge_ + 0.04f * skillMods_.gaugeChargeMult, 1.0f); }
        if (justSpinShot_) { awakenGauge_ = (std::min)(awakenGauge_ + 0.02f * skillMods_.gaugeChargeMult, 1.0f); }
    }

    // ── スペースキー（武器タイプ別）──────────────────────────────
    if (!inWater_ && !finisherCharging_) {
        WeaponType wtype = wm->GetCurrent().type;

        // 連射クールダウンは常に消化（Ball モード以外でも自然に減る）
        shootCooldown_ -= GameConstants::kFrameDeltaTime;
        if (shootCooldown_ < 0.0f) { shootCooldown_ = 0.0f; }

        GetWeaponBehavior(wtype).Update(*this, input);

        // Ball モード以外 / 着地時はスピン角をリセット
        if (wtype != WeaponType::Ball || onGround_) { spinAngle_ = 0.0f; }
        isUpsideDown_ = (spinAngle_ > 90.0f && spinAngle_ < 270.0f);
    }

    // ── 乱舞フェーズ更新 ──────────────────────────────────────────
    GetRampageState(rampagePhase_).UpdatePhysics(*this, enemyPos);

    // 乱舞中の自動スラッシュにも斬撃モーションを合わせる（フィニッシャーは溜め→解放側で再生する）
    if (justRampageHit_ || justRampageFinish_) {
        PlayAttackAnim(rig_->slashAnim, kAttackAnimSpeed);
    }

    // ── 覚醒発動（R キー）────────────────────────────────────────
    if (!finisherCharging_ && input->TriggerKey(DIK_R) && awakenGauge_ >= 0.3f && !isAwakened_) {
        isAwakened_  = true;
        awakenTimer_ = kAwakenDuration_;
    }

    // ── 覚醒ゲージ管理 ────────────────────────────────────────────
    // 覚醒中のみ消費する未覚醒時は自然減衰させず、溜めた分を維持する
    if (isAwakened_) {
        awakenTimer_ -= GameConstants::kFrameDeltaTime;
        awakenGauge_  = (std::max)(awakenGauge_ - GameConstants::kFrameDeltaTime / kAwakenDuration_, 0.0f);
        if (awakenTimer_ <= 0.0f || awakenGauge_ <= 0.0f) {
            isAwakened_  = false;
            awakenTimer_ = 0.0f;
            awakenGauge_ = 0.0f;
        }
    }

    // ── 覚醒フォーム切り替え（覚醒中はメカモデルへ丸ごと差し替え）──────
    CharacterRig* desiredRig = isAwakened_ ? &awakenedRig_ : &normalRig_;
    if (rig_ != desiredRig) {
        rig_ = desiredRig;
        afterImageRenderer_.SetModel(rig_->staticModel.get(), rig_->modelScale);
        // 旧リグで再生中の攻撃モーションは持ち越せないため打ち切り、
        // 直後の UpdateAnimationState に新リグの通常モーションを選び直させる
        // （animState_ は移動系と必ず不一致になる Attack を番兵にする）
        attackAnimTimer_ = 0.0f;
        rig_->object->SetAnimSpeed(1.0f);
        animState_ = AnimState::Attack;
    }

    // ── 敵とのめり込み防止（乱舞の突進/ジャグルは意図的に密着させる演出なので対象外）──
    if (rampagePhase_ == RampagePhase::Inactive) {
        float dx = pos_.x - enemyPos.x;
        if (std::abs(dx) < kMinEnemyDistanceX_) {
            float dir = (dx >= 0.0f) ? 1.0f : -1.0f;
            pos_.x = std::clamp(enemyPos.x + dir * kMinEnemyDistanceX_, kMinX_, kMaxX_);
        }
    }

    // 入水・出水判定（物理後の位置で確定）
    inWater_          = (pos_.y < waterLevel_);
    justEnteredWater_ = !prevInWater_ && inWater_;
    justExitedWater_  = prevInWater_  && !inWater_;

    // 入水衝撃吸収（落下速度を大きく減衰）
    if (justEnteredWater_) { velocityY_ *= 0.4f; }

    float yaw = (lastDirX_ >= 0.0f) ? GameConstants::kHalfPi : -GameConstants::kHalfPi;

    // ── 覚醒残像スポーン＆フェード ──
    Vector3 modelPos = { pos_.x, pos_.y + rig_->modelOffsetY, pos_.z };
    bool isRampage = (rampagePhase_ != RampagePhase::Inactive);
    afterImageRenderer_.Update(isAwakened_ || isRampage, isRampage, modelPos, yaw, spinAngle_);

    // ── アニメーション状態（接地中の左右移動入力で Idle/Run、空中で Jump）──
    bool isMovingHoriz = input->PushKey(DIK_A) || input->PushKey(DIK_D)
                       || input->PushKey(DIK_LEFT) || input->PushKey(DIK_RIGHT);
    UpdateAnimationState(isMovingHoriz);

    // ── プレイヤー色 ──
    if (finisherCharging_) {
        rig_->object->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f }); // 色自体は白のまま、リムの発光だけで魅せる
    } else if (rampagePhase_ != RampagePhase::Inactive) {
        float t = std::sin(juggleSlashCount_ * 3.0f) * 0.5f + 0.5f;
        rig_->object->SetColor({ 1.0f, 1.0f - t * 0.4f, 1.0f - t * 0.6f, 1.0f }); // 白→青白点滅
    } else if (justChargedGauge_) {
        rig_->object->SetColor({ 1.0f, 0.85f, 0.0f, 1.0f }); // 黄（ハンマーチャージ）
    } else if (isAwakened_) {
        float t = std::sin(awakenTimer_ * 6.0f) * 0.3f + 0.7f;
        rig_->object->SetColor({ 0.15f * t, 0.55f * t, 1.0f, 1.0f }); // 青くパルス
    } else {
        rig_->object->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
    }

    // 溜めが深まるほどリムライトを強めて「集中が高まる」感じを出す（解放の瞬間が一番明るい）
    float rimIntensity = finisherCharging_
        ? 1.2f + (1.0f - finisherChargeTimer_ / kFinisherChargeDuration) * 3.0f
        : 1.2f;
    rig_->object->SetRimIntensity(rimIntensity);
    for (auto& slot : heldWeapons_) { slot.object->SetRimIntensity(rimIntensity); }
    for (auto& slot : guns_)        { slot.object->SetRimIntensity(rimIntensity); }

    // 攻撃段ごとの体の傾き（前後=X/左右ロール=Z）。Ball旋回のspinAngle_とはZ軸を共有するが
    // 両者は排他（旋回はBall選択中のみ、攻撃レンはコンボ中のみ）なので単純加算でよい
    Vector3 bodyLean = meleeCombo_.GetBodyLeanOffset();
    rig_->object->SetPosition(modelPos);
    rig_->object->SetRotation({ bodyLean.x, yaw, spinAngle_ * GameConstants::kDegToRad + bodyLean.z });
    rig_->object->Update();

    // ── 現在のスタイルに対応する武器を右手ボーンに追従 ──────────────
    // 攻撃中は段ごとのスイング回転（振りかぶり→振り抜き）をグリップ回転へ加算し、
    // 共通の腕モーションでも武器ごとに違う軌道に見せる
    WeaponType meleeType = wm->GetCurrent().type;
    activeHeldIndex_ = -1;
    for (int i = 0; i < static_cast<int>(heldWeapons_.size()); ++i) {
        if (heldWeapons_[i].type == meleeType) { activeHeldIndex_ = i; break; }
    }
    if (activeHeldIndex_ >= 0) {
        auto& slot = heldWeapons_[activeHeldIndex_];
        Vector3 rot = slot.gripRotate + meleeCombo_.GetSwingOffset();
        AttachHeldWeapon(slot.object.get(), rig_->meleeBoneName, slot.gripScale, rot, slot.gripTranslate);
    }

    // ── 選択中の銃を左手ボーンに追従（Gキーで切り替えた1丁だけ表示）──────
    // 射撃コンボ中は段ごとの構え→リコイルの回転オフセットをグリップ回転へ加算し、
    // 同じ左手ボーンでも銃ごとに違う撃ち方に見せる
    {
        GunType gunType = wm->GetRanged().type;
        activeGunIndex_ = -1;
        for (int i = 0; i < static_cast<int>(guns_.size()); ++i) {
            if (guns_[i].type == gunType) { activeGunIndex_ = i; break; }
        }
        const Skeleton& skel = rig_->object->GetSkeleton();
        gunVisible_ = (skel.jointMap.find(rig_->gunBoneName) != skel.jointMap.end());
        if (gunVisible_ && activeGunIndex_ >= 0) {
            auto& slot = guns_[activeGunIndex_];
            Vector3 rot = slot.gripRotate + gunCombo_.GetPoseOffset();
            AttachHeldWeapon(slot.object.get(), rig_->gunBoneName, slot.gripScale, rot, slot.gripTranslate);
        }
    }
}

void Player::AttachHeldWeapon(Object3d* obj, const char* boneName,
    const Vector3& gripScale, const Vector3& gripRotate, const Vector3& gripTranslate)
{
    AttachToBone(obj, rig_->object->GetSkeleton(), rig_->object->GetWorldMatrix(),
        boneName, gripScale, gripRotate, gripTranslate);
}

void Player::Draw()
{
    // 残像（プレイヤーより先に描画して後ろに見えるようにする）
    afterImageRenderer_.Draw();

    // 黒縁アウトライン（本体・武器とも先に描いてから通常描画で上書きする）
    auto* outline = OutlineEffect::GetInstance();
    outline->SetColor(kOutlineColor);
    outline->SetWidth(kOutlineWidth);
    outline->BeginOutlinePass();
    if (activeHeldIndex_ >= 0) { heldWeapons_[activeHeldIndex_].object->DrawOutline(outline); }
    if (gunVisible_ && activeGunIndex_ >= 0) { guns_[activeGunIndex_].object->DrawOutline(outline); }
    rig_->object->DrawOutline(outline);
    if (modelCommon_) {
        modelCommon_->CommonDrawSettings();
        // ルートシグネチャの切り替えでライト/シャドウマップの束縛が失われているため再バインドする
        Object3d::RebindCommonLighting(modelCommon_->GetDxCommon()->GetCommandList());
    }

    // 通常描画
    if (activeHeldIndex_ >= 0) { heldWeapons_[activeHeldIndex_].object->Draw(); }
    if (gunVisible_ && activeGunIndex_ >= 0) { guns_[activeGunIndex_].object->Draw(); }
    rig_->object->Draw();
}

// ============================================================
//  Physics State（水中/水上）
// ============================================================

void Player::UnderwaterPhysicsState::Update(Player& player, Input* input) const
{
    const float speedMult = (player.isAwakened_ ? 1.5f : 1.0f) * player.skillMods_.speedMult;

    // 横移動（水の抵抗で遅い）
    if (input->PushKey(DIK_A) || input->PushKey(DIK_LEFT))  { player.pos_.x -= kWaterSpeed_ * speedMult; player.lastDirX_ = -1.0f; }
    if (input->PushKey(DIK_D) || input->PushKey(DIK_RIGHT)) { player.pos_.x += kWaterSpeed_ * speedMult; player.lastDirX_ =  1.0f; }

    // 浮力（弱い下向き加速）
    player.velocityY_ -= kWaterGravity_;
    player.velocityY_  = (std::max)(player.velocityY_, kSinkMaxVY_);

    // ジャンプ長押し = 上昇スイム
    if (input->PushKey(DIK_W) || input->PushKey(DIK_UP) || input->PushKey(DIK_SPACE)) {
        player.velocityY_ = (std::min)(player.velocityY_ + kSwimAccel_, kSwimMaxVY_);
    }

    player.pos_.y += player.velocityY_;

    // 床クランプ（水底でも止まる）
    if (player.pos_.y <= kGroundY_) {
        player.pos_.y     = kGroundY_;
        player.velocityY_ = 0.0f;
        player.onGround_  = true;
    } else {
        player.onGround_ = false;
    }

    // 天井クランプ
    if (player.pos_.y > kCeilingY_) {
        player.pos_.y     = kCeilingY_;
        player.velocityY_ = 0.0f;
    }
}

void Player::GroundedPhysicsState::Update(Player& player, Input* input) const
{
    const float speedMult = (player.isAwakened_ ? 1.5f : 1.0f) * player.skillMods_.speedMult;
    const float jumpMult  = (player.isAwakened_ ? 1.3f : 1.0f) * player.skillMods_.jumpMult;

    if (player.rampagePhase_ == RampagePhase::Inactive && !player.finisherCharging_) {
        if (input->PushKey(DIK_A) || input->PushKey(DIK_LEFT))  { player.pos_.x -= kSpeed_ * speedMult; player.lastDirX_ = -1.0f; }
        if (input->PushKey(DIK_D) || input->PushKey(DIK_RIGHT)) { player.pos_.x += kSpeed_ * speedMult; player.lastDirX_ =  1.0f; }
    }

    if (player.onGround_ && !player.finisherCharging_) {
        if (input->TriggerKey(DIK_W) || input->TriggerKey(DIK_UP)) {
            // 打ち上げ直後は追撃用に高く跳べる（浮かせた敵にジャンプで追いつく）
            float followMult = (player.launchFollowTimer_ > 0.0f) ? kLaunchFollowJumpMult_ : 1.0f;
            player.velocityY_  = kJumpPower_ * jumpMult * followMult;
            player.onGround_   = false;
            player.justJumped_ = true;
        }
    }

    player.velocityY_ -= kGravity_;
    player.pos_.y     += player.velocityY_;

    if (player.pos_.y <= kGroundY_) {
        player.pos_.y     = kGroundY_;
        player.velocityY_ = 0.0f;
        player.onGround_  = true;
    }
    if (player.pos_.y > kCeilingY_) {
        player.pos_.y     = kCeilingY_;
        player.velocityY_ = 0.0f;
    }

    player.justLanded_ = !player.prevOnGround_ && player.onGround_;
}

const Player::IPhysicsState& Player::GetPhysicsState(bool inWater)
{
    static GroundedPhysicsState   grounded;
    static UnderwaterPhysicsState underwater;
    return inWater ? static_cast<const IPhysicsState&>(underwater) : static_cast<const IPhysicsState&>(grounded);
}

// ============================================================
//  Rampage State（覚醒乱舞の進行フェーズ）
// ============================================================

void Player::InactiveRampageState::HandleAttackInput(Player&, Input*, const Vector3&) const
{
    // 通常時の攻撃入力は MeleeComboController（Player::Update 内）が担当するため何もしない
    // （乱舞の開始判定も覚醒ソード限定なので Update 側で行う）
}

void Player::LaunchRampageState::UpdatePhysics(Player& player, const Vector3& enemyPos) const
{
    // 敵X座標へ向かって突進（重力無効）
    // ステージ外の座標が渡されてもプレイヤーが到達できる位置にクランプ
    float targetX = std::clamp(enemyPos.x, kMinX_ + 0.5f, kMaxX_ - 0.5f);
    float dx  = targetX - player.pos_.x;
    float dir = (dx > 0.0f) ? 1.0f : -1.0f;
    player.lastDirX_  = dir;
    player.velocityY_ = 0.0f;
    player.pos_.x += dir * kRampageSpeed_;
    player.pos_.x  = std::clamp(player.pos_.x, kMinX_, kMaxX_);

    // 十分近づいたら打ち上げヒット → ジャグルフェーズへ
    if (std::abs(dx) < 1.0f) {
        player.justLaunched_ = true;
        player.velocityY_    = 0.25f;
        player.rampagePhase_ = RampagePhase::Juggle;
    }
}

void Player::JuggleRampageState::HandleAttackInput(Player& player, Input* input, const Vector3& enemyPos) const
{
    const int effectiveMax = kJuggleMaxSlashes_ + player.skillMods_.juggleMaxBonus;
    if (player.juggleSlashCount_ >= effectiveMax) { return; }

    // 乱舞中 L → 敵の周囲の次の角度へテレポートしてスラッシュ
    float angle = player.juggleAngleIdx_ * (GameConstants::kTwoPi / effectiveMax);
    float dx = std::cos(angle) * kJuggleRadius_;
    float dy = std::sin(angle) * kJuggleRadius_;
    player.pos_.x = std::clamp(enemyPos.x + dx, kMinX_, kMaxX_);
    player.pos_.y = std::clamp(enemyPos.y + dy, kGroundY_, kCeilingY_);
    player.velocityY_ = 0.0f;
    player.lastDirX_  = (enemyPos.x >= player.pos_.x) ? 1.0f : -1.0f;
    player.juggleAngleIdx_   = (player.juggleAngleIdx_ + 1) % effectiveMax;
    player.juggleSlashCount_++;

    bool isLast = (player.juggleSlashCount_ >= effectiveMax);
    player.justRampageHit_    = true;
    player.justRampageFinish_ = isLast;
    if (isLast) {
        player.rampagePhase_ = RampagePhase::Inactive;
    }
}

void Player::JuggleRampageState::UpdatePhysics(Player& player, const Vector3& enemyPos) const
{
    // ジャグル中は重力無効でホバリング
    player.velocityY_ = 0.0f;
}

const Player::IRampageState& Player::GetRampageState(RampagePhase phase)
{
    static InactiveRampageState inactive;
    static LaunchRampageState   launch;
    static JuggleRampageState   juggle;
    switch (phase) {
    case RampagePhase::Launch: return launch;
    case RampagePhase::Juggle: return juggle;
    default:                   return inactive;
    }
}

// ============================================================
//  Weapon Behavior Strategy（武器種別ごとのスペースキー挙動）
// ============================================================

void Player::DaggerBehavior::Update(Player& player, Input* input) const
{
    // ブリンク（瞬間移動）
    if (input->TriggerKey(DIK_SPACE)) {
        player.pos_.x += player.lastDirX_ * kBlinkDist_ * player.skillMods_.blinkDistMult;
        player.pos_.x = std::clamp(player.pos_.x, kMinX_, kMaxX_);
        player.justBlinked_ = true;
    }
}

void Player::HammerBehavior::Update(Player& player, Input* input) const
{
    // ゲージチャージ（長押し約3秒で満タン）覚醒中は蓄積しない
    if (!player.isAwakened_ && input->PushKey(DIK_SPACE)) {
        player.justChargedGauge_ = true;
        player.awakenGauge_ = (std::min)(
            player.awakenGauge_ + kGaugeCharge_ * GameConstants::kFrameDeltaTime * player.skillMods_.gaugeChargeMult, 1.0f);
    }
}

void Player::BallBehavior::Update(Player& player, Input* input) const
{
    // スピン連射 + 空中くるくる
    if (input->PushKey(DIK_SPACE)) {
        if (player.shootCooldown_ <= 0.0f) {
            player.justSpinShot_  = true;
            player.shootCooldown_ = kShootInterval_ * player.skillMods_.fireIntervalMult;
        }
        if (!player.onGround_) {
            player.spinAngle_ += kSpinSpeed_;
            if (player.spinAngle_ >= 360.0f) { player.spinAngle_ -= 360.0f; }
        }
    }
}

const Player::IWeaponBehavior& Player::GetWeaponBehavior(WeaponType type)
{
    static DaggerBehavior        dagger;
    static HammerBehavior        hammer;
    static BallBehavior          ball;
    static DefaultWeaponBehavior def;
    switch (type) {
    case WeaponType::Dagger: return dagger;
    case WeaponType::Hammer: return hammer;
    case WeaponType::Ball:   return ball;
    default:                 return def;
    }
}
