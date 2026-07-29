/**
 * @file Player.cpp
 * @brief Playerのプレイヤーの操作、戦闘、状態遷移に関する具体的な処理を実装するファイル
 */
#include "Player.h"
#include "CharacterVisuals.h"
#include "Easing.h"
#include "GameConstants.h"
#include "GravityBody.h"
#include "Input.h"
#include "ModelCommon.h"
#include "OutlineEffect.h"
#include "PipelineStateGuard.h"
#include "Weapon.h"
#include "WeaponManager.h"
#include <algorithm>
#include <cmath>
#include <limits>
using namespace engine;
using namespace engine::graphics;
using namespace engine::game;

namespace {
// 本体・武器の黒縁アウトライン（背景との同化と武器の視認性の低さを補う）
// 幅・濃さともに控えめにし、異常が起きているように見えるほど主張しないようにする
constexpr Vector4 kOutlineColor = { 0.0f, 0.0f, 0.0f, 0.55f };
constexpr float kOutlineWidth = 0.006f;

// フィニッシャー 静止集中の長さ（GamePlayScene/BattleTestScene の斬撃線バラマキ演出と一致させる
// kFinisherChargeDelay → 斬撃線を kFinisherSlashLines 本 kFinisherLineInterval 間隔で出す → kFinisherImpactDelay で解放）
// 専用の納刀ポーズ素材が無いため、Idle/IdleHold を静止させて代用する
// （PlayerCombatInput.cppのHandleFinisherSlashでも同じ値を使うため定義を重複させている）
constexpr float kFinisherChargeDuration = GameConstants::kFinisherChargeDelay
    + GameConstants::kFinisherSlashLines * GameConstants::kFinisherLineInterval
    + GameConstants::kFinisherImpactDelay;

// 武器奪取の刺突（ぶっ刺す→奪う演出の仮モーション、専用素材が無いため斬撃を流用）
constexpr float kStealStabAnimSpeed = 1.2f;

void LockJumpClipVerticalRootMotion(Animation& animation)
{
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

        rig.modelScale = def.scale;
        rig.modelOffsetY = def.offsetY;
        rig.meleeBoneName = def.meleeBone;
        rig.gunBoneName = def.gunBone;

        rig.idleAnim = LoadAnimationFile(def.dir, def.file, def.idle);
        rig.runAnim = LoadAnimationFile(def.dir, def.file, def.run);
        rig.jumpAnim = LoadAnimationFile(def.dir, def.file, def.jump);
        rig.runningJumpAnim = LoadAnimationFile(def.dir, def.file, def.runningJump);
        LockJumpClipVerticalRootMotion(rig.jumpAnim);
        LockJumpClipVerticalRootMotion(rig.runningJumpAnim);
        rig.swimAnim = LoadAnimationFile(def.dir, def.file, def.swim);
        rig.idleHoldAnim = LoadAnimationFile(def.dir, def.file, def.idleHold);
        rig.runHoldAnim = LoadAnimationFile(def.dir, def.file, def.runHold);
        rig.slashAnim = LoadAnimationFile(def.dir, def.file, def.slash);
        rig.punchAnim = LoadAnimationFile(def.dir, def.file, def.punch);

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
        rig.object->SetPosition({ pos_.x, pos_.y + def.offsetY + groundVisualCorrection_, pos_.z });
        rig.object->Update();
    };
    initRig(normalRig_, kNormalRigVisual);
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
        slot.type = asset.type;
        slot.gripScale = asset.gripScale;
        slot.gripRotate = asset.gripRotate;
        slot.gripTranslate = asset.gripTranslate;
        initHeldWeapon(slot.model, slot.object, asset.modelPath, asset.texturePath);
        heldWeapons_.push_back(std::move(slot));
    }

    // 銃（左手ボーン追従、Gキーで切り替えた1丁だけ表示）
    guns_.clear();
    for (const auto& asset : kGunVisuals) {
        GunSlot slot;
        slot.type = asset.type;
        slot.gripScale = asset.gripScale;
        slot.gripRotate = asset.gripRotate;
        slot.gripTranslate = asset.gripTranslate;
        initHeldWeapon(slot.model, slot.object, asset.modelPath, asset.texturePath);
        guns_.push_back(std::move(slot));
    }
}

void Player::SetStaticVisualModel(const std::string& modelPath, const std::string& texturePath)
{
    if (modelPath.empty() || !modelCommon_) {
        staticOverrideObject_.reset();
        staticOverrideModel_.reset();
        staticOverrideFootOffset_ = 0.0f;
        staticOverrideModelPath_.clear();
        staticOverrideTexturePath_.clear();
        return;
    }
    staticOverrideModelPath_ = modelPath;
    staticOverrideTexturePath_ = texturePath.empty() ? "Resources/white.png" : texturePath;
    staticOverrideModel_ = std::make_unique<Model>();
    staticOverrideModel_->Initialize(modelCommon_, modelPath, staticOverrideTexturePath_);
    staticOverrideObject_ = std::make_unique<Object3d>();
    staticOverrideObject_->Initialize(modelCommon_);
    staticOverrideObject_->SetModel(staticOverrideModel_.get());
    staticOverrideObject_->SetEnableLighting(true);

    // 差し替えモデルは身長がまちまちなので、当たり判定(1x1x1 AABB)の高さに合わせて自動スケールする
    // 原点が中心にあるモデル（足元が0でない）でも、最下点をAABB下端に合わせられるよう足元オフセットも計算する
    float minY = (std::numeric_limits<float>::max)();
    float maxY = (std::numeric_limits<float>::lowest)();
    for (const auto& v : staticOverrideModel_->GetVertices()) {
        minY = (std::min)(minY, v.position.y);
        maxY = (std::max)(maxY, v.position.y);
    }
    const float height = maxY - minY;
    const float scale = (height > 0.001f) ? (1.0f / height) : 0.5f;
    staticOverrideObject_->SetScale({ scale, scale, scale });
    staticOverrideFootOffset_ = -minY * scale;
}

void Player::UpdateAnimationState(bool isMoving)
{
    // フィニッシャー溜め中は静止ポーズを維持（タイマー管理は Update() 側）
    if (finisherCharging_) {
        return;
    }

    // 攻撃モーション中は再生し切るまで状態遷移しない
    if (attackAnimTimer_ > 0.0f) {
        attackAnimTimer_ -= GameConstants::kFrameDeltaTime;
        if (attackAnimTimer_ > 0.0f) {
            return;
        }
        rig_->object->SetAnimSpeed(1.0f); // 攻撃用の速度倍率を戻す
    }

    AnimState newState = inWater_ ? AnimState::Swim
        : !onGround_              ? AnimState::Jump
        : isMoving                ? AnimState::Run
                                  : AnimState::Idle;
    // 構え系の近接武器、または銃を左手に構えている間は武器持ちバリエーション（IdleHold/RunHold）を使う
    // （銃は常時左手に追従表示されるため、素のIdle/Runのままだと構えていないように見えてしまう）
    const Skeleton& skel = rig_->object->GetSkeleton();
    bool hasGunBone = skel.GetJointMap().find(rig_->gunBoneName) != skel.GetJointMap().end();
    auto* weaponManager = WeaponManager::GetInstance();
    bool hold = (weaponManager->HasEquippedWeapon()
                    && UsesHoldPose(weaponManager->GetCurrent().type))
        || hasGunBone;
    if (newState == animState_ && hold == animHold_) {
        return;
    }
    animState_ = newState;
    animHold_ = hold;

    switch (animState_) {
    case AnimState::Swim:
        rig_->object->SetAnimation(rig_->swimAnim);
        break;
    case AnimState::Run:
        rig_->object->SetAnimation(hold ? rig_->runHoldAnim : rig_->runAnim);
        break;
    case AnimState::Jump:
        rig_->object->SetAnimation(isMoving ? rig_->runningJumpAnim : rig_->jumpAnim);
        break;
    default:
        rig_->object->SetAnimation(hold ? rig_->idleHoldAnim : rig_->idleAnim);
        break;
    }
}

void Player::PlayStealStab()
{
    PlayAttackAnim(rig_->slashAnim, kStealStabAnimSpeed);
}

int Player::GetComboMax() const
{
    if (!WeaponManager::GetInstance()->HasEquippedWeapon()) {
        return 0;
    }
    // 現在の武器の地上コンボ段数を基準にする（HUDのx段目/最大表示用）
    const MeleeComboSet& set = GetMeleeComboSet(WeaponManager::GetInstance()->GetCurrent().type);
    return set.ground.count + skillMods_.comboMaxBonus;
}

void Player::PlayAttackAnim(const Animation& anim, float speed)
{
    rig_->object->SetAnimation(anim);
    rig_->object->SetAnimSpeed(speed);
    animState_ = AnimState::Attack;
    attackAnimTimer_ = anim.duration / speed;
}

// ══════════════════════════════════════════════════════
// フレーム更新
// ══════════════════════════════════════════════════════

void Player::Update(Input* input, const Vector3& enemyPos)
{
    ResetFrameFlags();

    if (invincibleTimer_ > 0.0f) {
        invincibleTimer_ -= GameConstants::kFrameDeltaTime;
    }

    HandleStyleSwitch(input);

    GetPhysicsState(inWater_).Update(*this, input);
    pos_.x = std::clamp(pos_.x, minX_, maxX_);

    HandleRangedCombat(input);
    HandleMeleeCombat(input, enemyPos);
    HandleFinisherSlash(input);
    HandleWeaponSkill(input);
    UpdateRampagePhysics(enemyPos);
    UpdateAwakenState(input);
    ResolveEnemyOverlap(enemyPos);
    UpdateWaterState();
    UpdateVisualState(input);
    AttachActiveWeapons();
}

void Player::RefreshVisualTransforms()
{
    // アニメ状態は一切変えず、現在のpos_を使って見た目のトランスフォームだけ再計算する
    // （StageEditorのギズモ等、Update()を通さずpos_だけ直接書き換えられた場合に追従させるため）
    Vector3 modelPos = { pos_.x, pos_.y + rig_->modelOffsetY + groundVisualCorrection_, pos_.z };
    rig_->object->SetPosition(modelPos);

    // SkinnedObject3d::Update()はアニメ時刻も進めてしまうため、一時的に速度0にして完全静止させる
    float savedSpeed = rig_->object->GetAnimSpeed();
    rig_->object->SetAnimSpeed(0.0f);
    rig_->object->Update();
    rig_->object->SetAnimSpeed(savedSpeed);

    // 武器/銃は本体のボーンに追従しているため、本体の位置更新後に付け直さないと古い位置に取り残される
    AttachActiveWeapons();

    for (auto& slot : heldWeapons_) {
        slot.object->Update();
    }
    for (auto& slot : guns_) {
        slot.object->Update();
    }
}

void Player::ResolveBlockCollision(const std::vector<AABB>& blocks)
{
    groundVisualCorrection_ = 0.0f;
    if (blocks.empty()) {
        if (pos_.y > kGroundY_ && velocityY_ <= 0.0f) {
            onGround_ = false;
        }
        return;
    }

    // プレイヤーの当たり判定はダミー等と同じpos_を中心とした1x1x1規約に合わせる
    constexpr float kHalf = 0.5f;

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
    const float prevFeetY = pos_.y - velocityY_ - kHalf;
    auto landedOnTop = [&](const AABB& b) {
        return velocityY_ <= 0.0f && prevFeetY >= b.max.y - 0.01f;
    };

    // 水平方向  側面から重なっているブロックがあれば侵入量が小さい側へ押し出す
    for (const auto& b : blocks) {
        if (landedOnTop(b)) {
            continue;
        }
        bool overlapY = (pos_.y + kHalf) > b.min.y + 0.05f && (pos_.y - kHalf) < b.max.y - 0.05f;
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
        float pushBack = b.min.z - (pos_.z + kHalf); // 負値＝手前へ押し出す量
        float pushFront = b.max.z - (pos_.z - kHalf); // 正値＝奥へ押し出す量

        float pushX = (std::abs(pushLeft) < std::abs(pushRight)) ? pushLeft : pushRight;
        float pushZ = (std::abs(pushBack) < std::abs(pushFront)) ? pushBack : pushFront;

        // 侵入量がより小さい軸だけを押し出す（角にめり込んだ場合に誤った軸へ押し出さないため）
        if (std::abs(pushX) < std::abs(pushZ)) {
            pos_.x += pushX;
        } else {
            pos_.z += pushZ;
        }
    }

    // 垂直方向  足元付近に上面があるブロックのうち一番高いものへ着地させる
    float feetY = pos_.y - kHalf;
    float bestTop = kGroundY_; // 何も無ければ通常の地面が最終フォールバック
    float visualFloorTop = (std::numeric_limits<float>::lowest)();
    constexpr float kMaxStepHeight = 0.12f;
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
        const bool reachable = b.max.y <= feetY + kMaxStepHeight || landedOnTop(b);
        if (reachable && (b.max.y + kHalf) > bestTop && hasClearanceAbove(b)) {
            bestTop = b.max.y + kHalf;
        }
        if (reachable && b.max.y > visualFloorTop && hasClearanceAbove(b)) {
            visualFloorTop = b.max.y;
        }
    }
    if (onGround_ && velocityY_ <= 0.0f
        && visualFloorTop != (std::numeric_limits<float>::lowest)()) {
        groundVisualCorrection_ = visualFloorTop - (pos_.y - kHalf);
    }
    if (velocityY_ <= 0.0f && pos_.y <= bestTop + 0.05f) {
        pos_.y = bestTop;
        velocityY_ = 0.0f;
        onGround_ = true;
    } else if (pos_.y > kGroundY_ && velocityY_ <= 0.0f) {
        onGround_ = false;
    }

    pos_.x = std::clamp(pos_.x, minX_, maxX_);
}


// ══════════════════════════════════════════════════════
// 表示更新と描画
// ══════════════════════════════════════════════════════

void Player::UpdateVisualState(Input* input)
{
    float yaw = (lastDirX_ >= 0.0f) ? GameConstants::kHalfPi : -GameConstants::kHalfPi;

    // ── 覚醒残像スポーン＆フェード ──
    Vector3 modelPos = { pos_.x, pos_.y + rig_->modelOffsetY + groundVisualCorrection_, pos_.z };
    bool isRampage = (rampagePhase_ != RampagePhase::Inactive);
    afterImageRenderer_.Update(isRampage, isRampage, modelPos, yaw, spinAngle_);

    // ── アニメーション状態（接地中の左右移動入力で Idle/Run、空中で Jump）──
    bool isMovingHoriz = input->PushAction(Input::Action::MoveLeft)
        || input->PushAction(Input::Action::MoveRight);
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

    // 溜めが深まるほどリムライトを強めて集中が高まる感じを出す（解放の瞬間が一番明るい）
    float rimIntensity = finisherCharging_
        ? 1.2f + (1.0f - finisherChargeTimer_ / kFinisherChargeDuration) * 3.0f
        : 1.2f;
    rig_->object->SetRimIntensity(rimIntensity);
    for (auto& slot : heldWeapons_) {
        slot.object->SetRimIntensity(rimIntensity);
    }
    for (auto& slot : guns_) {
        slot.object->SetRimIntensity(rimIntensity);
    }

    // 攻撃段ごとの体の傾き（前後=X/左右ロール=Z）。Ball旋回のspinAngle_とはZ軸を共有するが
    // 両者は排他（旋回はBall選択中のみ、攻撃レンはコンボ中のみ）なので単純加算でよい
    Vector3 bodyLean = meleeCombo_.GetBodyLeanOffset();
    rig_->object->SetPosition(modelPos);
    rig_->object->SetRotation({ bodyLean.x, yaw, spinAngle_ * GameConstants::kDegToRad + bodyLean.z });
    rig_->object->Update();
}

void Player::AttachActiveWeapons()
{
    auto* wm = WeaponManager::GetInstance();

    // ── 現在のスタイルに対応する武器を右手ボーンに追従 ──────────────
    // 攻撃中は段ごとのスイング回転（振りかぶり→振り抜き）をグリップ回転へ加算し、
    // 共通の腕モーションでも武器ごとに違う軌道に見せる
    activeHeldIndex_ = -1;
    if (wm->HasEquippedWeapon()) {
        WeaponType meleeType = wm->GetCurrent().type;
        for (int i = 0; i < static_cast<int>(heldWeapons_.size()); ++i) {
            if (heldWeapons_[i].type == meleeType) {
                activeHeldIndex_ = i;
                break;
            }
        }
    }
    if (activeHeldIndex_ >= 0 && !(heldWeapons_[activeHeldIndex_].type == WeaponType::Greatsword && greatswordThrowActive_)) {
        auto& slot = heldWeapons_[activeHeldIndex_];
        Vector3 rot = slot.gripRotate + meleeCombo_.GetSwingOffset();
        AttachHeldWeapon(slot.object.get(), rig_->meleeBoneName, slot.gripScale, rot, slot.gripTranslate);
    }

    // 投げ回転斬りの最中は、途中で他の武器に持ち替えてもactiveHeldIndex_とは別に
    // 大剣モデルを飛行/渦の位置へ動かし続ける（持ち替え直後に消えて見えないようにする）
    thrownGreatswordIndex_ = -1;
    if (greatswordThrowActive_) {
        for (int i = 0; i < static_cast<int>(heldWeapons_.size()); ++i) {
            if (heldWeapons_[i].type == WeaponType::Greatsword) {
                thrownGreatswordIndex_ = i;
                UpdateThrownGreatswordVisual(heldWeapons_[i]);
                break;
            }
        }
    }

    // ── 選択中の銃を左手ボーンに追従（Gキーで切り替えた1丁だけ表示）──────
    // 射撃コンボ中は段ごとの構え→リコイルの回転オフセットをグリップ回転へ加算し、
    // 同じ左手ボーンでも銃ごとに違う撃ち方に見せる
    {
        GunType gunType = wm->GetRanged().type;
        activeGunIndex_ = -1;
        for (int i = 0; i < static_cast<int>(guns_.size()); ++i) {
            if (guns_[i].type == gunType) {
                activeGunIndex_ = i;
                break;
            }
        }
        const Skeleton& skel = rig_->object->GetSkeleton();
        gunVisible_ = (skel.GetJointMap().find(rig_->gunBoneName) != skel.GetJointMap().end());
        if (gunVisible_ && activeGunIndex_ >= 0) {
            auto& slot = guns_[activeGunIndex_];
            Vector3 rot = slot.gripRotate + gunCombo_.GetPoseOffset();
            AttachHeldWeapon(slot.object.get(), rig_->gunBoneName, slot.gripScale, rot, slot.gripTranslate);
        }
    }
}

void Player::BeginDash(DashMotion& dash, float worldDeltaX)
{
    dash.active = true;
    dash.timer = 0.0f;
    dash.startX = pos_.x;
    dash.targetX = std::clamp(pos_.x + worldDeltaX, minX_, maxX_);
}

bool Player::AdvanceDash(DashMotion& dash)
{
    if (!dash.active) {
        return false;
    }
    dash.timer += GameConstants::kFrameDeltaTime;
    const float t = std::clamp(dash.timer / kDashDuration_, 0.0f, 1.0f);
    pos_.x = dash.startX + (dash.targetX - dash.startX) * Easing::EaseOutQuad(t);
    if (t >= 1.0f) {
        dash.active = false;
        return true;
    }
    return false;
}

void Player::UpdateThrownGreatswordVisual(HeldWeaponSlot& slot)
{
    // 目にも留まらぬ速さで回転させ続ける（飛行中→静止後の渦で途切れず連続した見た目にする）
    constexpr float kThrowSpinSpeed = 22.0f; // ラジアン/秒
    constexpr float kSpinBobAmplitude = 0.06f; // 静止後、渦の間だけ小さく上下に揺らす
    constexpr float kSpinBobSpeed = 10.0f;

    Vector3 pos;
    if (greatswordThrowTimer_ < kGreatswordThrowTravelTime_) {
        // 飛行中: 手元から静止地点へ補間しながら飛んでいく
        const float t = Easing::EaseOutQuad(greatswordThrowTimer_ / kGreatswordThrowTravelTime_);
        pos = {
            greatswordThrowStartPos_.x + (greatswordThrowPos_.x - greatswordThrowStartPos_.x) * t,
            greatswordThrowStartPos_.y + (greatswordThrowPos_.y - greatswordThrowStartPos_.y) * t,
            greatswordThrowStartPos_.z + (greatswordThrowPos_.z - greatswordThrowStartPos_.z) * t,
        };
    } else {
        const float spinElapsed = greatswordThrowTimer_ - kGreatswordThrowTravelTime_;
        if (spinElapsed < kGreatswordVortexMaxDuration_) {
            // 渦の最中: その場に留まり、渦らしく小さく上下に揺れる
            pos = greatswordThrowPos_;
            pos.y += std::sin(spinElapsed * kSpinBobSpeed) * kSpinBobAmplitude;
        } else {
            // 帰還中: 渦の中心から手元へ飛んで戻る（瞬間移動に見えないよう補間する）
            const float returnElapsed = spinElapsed - kGreatswordVortexMaxDuration_;
            const float t = Easing::EaseInQuad((std::min)(returnElapsed / kGreatswordReturnTime_, 1.0f));
            pos = {
                greatswordThrowPos_.x + (greatswordReturnTargetPos_.x - greatswordThrowPos_.x) * t,
                greatswordThrowPos_.y + (greatswordReturnTargetPos_.y - greatswordThrowPos_.y) * t,
                greatswordThrowPos_.z + (greatswordReturnTargetPos_.z - greatswordThrowPos_.z) * t,
            };
        }
    }

    slot.object->ClearLocalMatrix(); // 手のボーン追従（SetLocalMatrix）を解除し、直接のTransform制御に戻す
    slot.object->SetPosition(pos);
    slot.object->SetRotation({ slot.gripRotate.x, slot.gripRotate.y, greatswordThrowTimer_ * kThrowSpinSpeed });
    slot.object->SetScale(slot.gripScale);
    slot.object->Update();
}

void Player::AttachHeldWeapon(Object3d* obj, const char* boneName,
    const Vector3& gripScale, const Vector3& gripRotate, const Vector3& gripTranslate)
{
    AttachToBone(obj, rig_->object->GetSkeleton(), rig_->object->GetWorldMatrix(),
        boneName, gripScale, gripRotate, gripTranslate);
}

void Player::Draw()
{
    if (staticOverrideObject_) {
        staticOverrideObject_->SetPosition({ pos_.x, pos_.y - 0.5f + groundVisualCorrection_ + staticOverrideFootOffset_, pos_.z });
        staticOverrideObject_->SetRotation({ 0.0f, lastDirX_ >= 0.0f ? GameConstants::kHalfPi : -GameConstants::kHalfPi, 0.0f });
        staticOverrideObject_->Update();
        staticOverrideObject_->Draw();
        return;
    }
    // 残像（プレイヤーより先に描画して後ろに見えるようにする）
    afterImageRenderer_.Draw();

    // 黒縁アウトライン（本体・武器とも先に描いてから通常描画で上書きする）
    auto* outline = OutlineEffect::GetInstance();
    outline->SetColor(kOutlineColor);
    outline->SetWidth(kOutlineWidth);
    {
        // スコープを抜けた瞬間に必ず通常描画用のPSOへ復帰する（早期returnや将来のコード追加があっても復帰忘れが起きない）
        PipelineStateGuard restoreGuard([this] {
            if (modelCommon_) {
                modelCommon_->CommonDrawSettings();
                // ルートシグネチャの切り替えでライト/シャドウマップの束縛が失われているため再バインドする
                Object3d::RebindCommonLighting(modelCommon_->GetDxCommon()->GetCommandList());
            }
        });
        outline->BeginOutlinePass();
        if (weaponsVisible_ && activeHeldIndex_ >= 0) {
            heldWeapons_[activeHeldIndex_].object->DrawOutline(outline);
        }
        if (weaponsVisible_ && thrownGreatswordIndex_ >= 0 && thrownGreatswordIndex_ != activeHeldIndex_) {
            heldWeapons_[thrownGreatswordIndex_].object->DrawOutline(outline);
        }
        if (weaponsVisible_ && gunVisible_ && activeGunIndex_ >= 0) {
            guns_[activeGunIndex_].object->DrawOutline(outline);
        }
        rig_->object->DrawOutline(outline);
    }

    // 通常描画
    if (weaponsVisible_ && activeHeldIndex_ >= 0) {
        heldWeapons_[activeHeldIndex_].object->Draw();
    }
    if (weaponsVisible_ && thrownGreatswordIndex_ >= 0 && thrownGreatswordIndex_ != activeHeldIndex_) {
        heldWeapons_[thrownGreatswordIndex_].object->Draw();
    }
    if (weaponsVisible_ && gunVisible_ && activeGunIndex_ >= 0) {
        guns_[activeGunIndex_].object->Draw();
    }
    rig_->object->Draw();
}
