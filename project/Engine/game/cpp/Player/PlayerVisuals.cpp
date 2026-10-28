/**
 * @file PlayerVisuals.cpp
 * @brief プレイヤーの見た目・アニメーション・武器装着・描画
 */
#include "Player.h"
#include "SceneVisualTuning.h"
#include "CombatTuning.h"
#include "CharacterVisuals.h"
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
using namespace engine::graphics;

namespace engine::game {
namespace {
/** @brief 読み込み済みの調整値を取得する */
const PlayerTuning& Tuning() {
    return PlayerTuning::Get();
}

// 本体・武器の黒縁アウトライン（背景との同化と武器の視認性の低さを補う）
// 幅・濃さともに控えめにし、異常が起きているように見えるほど主張しないようにする

// 差し替えモデルの高さが取れない時の既定スケール
constexpr float kMinModelHeight = 0.001f;

// 状態ごとの本体の色
constexpr Vector4 kWhite = { 1.0f, 1.0f, 1.0f, 1.0f };
constexpr float kHalfWave = 0.5f; // sin を 0〜1 へ寄せる振幅とオフセット

/** @brief 状態ごとの本体の色（条件が成り立つ行のうち先頭を使う） */
struct BodyTint {
    bool active;
    Vector4 color;
};

}

// ══════════════════════════════════════════════════════
// 初期化
// ══════════════════════════════════════════════════════

/** @brief 見た目を差し替える静的モデル（とテクスチャ）を設定する。空のパスなら差し替えを解除する */
void Player::SetStaticVisualModel(const std::string& modelPath, const std::string& texturePath) {
    if (modelPath.empty() || !modelCommon_) {
        staticOverrideObject_.reset();
        staticOverrideModel_ = nullptr;
        staticOverrideFootOffset_ = 0.0f;
        staticOverrideModelPath_.clear();
        staticOverrideTexturePath_.clear();
        return;
    }
    staticOverrideModelPath_ = modelPath;
    staticOverrideTexturePath_ = texturePath.empty() ? "Resources/white.png" : texturePath;
    staticOverrideModel_ = ModelManager::GetInstance()->GetOrLoad(modelCommon_, modelPath, staticOverrideTexturePath_);
    staticOverrideObject_ = std::make_unique<Object3d>();
    staticOverrideObject_->Initialize(modelCommon_);
    staticOverrideObject_->SetModel(staticOverrideModel_);
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
    const float scale = (height > kMinModelHeight) ? (1.0f / height) : SceneVisualTuning::Player().fallbackOverrideScale;
    staticOverrideObject_->SetScale({ scale, scale, scale });
    staticOverrideFootOffset_ = -minY * scale;
}

/** @brief 移動・接地状況から再生すべきアニメーション状態を切り替える */
void Player::UpdateAnimationState(bool isMoving, bool airborne) {
    // フィニッシャー溜め中は静止ポーズを維持（タイマー管理は Update() 側）
    if (finisherCharging_) {
        return;
    }

    // 射撃中は近接アニメを終了して移動系の構えを土台にする
    if (gunCombo_.GetActive() != nullptr) {
        attackAnimTimer_ = float{};
        rig_->object->SetAnimSpeed(Tuning().gunBaseAnimSpeed);
    }

    // 攻撃モーション中は再生し切るまで状態遷移しない
    if (attackAnimTimer_ > 0.0f) {
        attackAnimTimer_ -= gameConstants::kFrameDeltaTime;
        if (attackAnimTimer_ > 0.0f) {
            return;
        }
        rig_->object->SetAnimSpeed(1.0f); // 攻撃用の速度倍率を戻す
    }

    // 上の行ほど優先。どれにも当たらなければ待機
    const struct {
        bool active;
        const IAnimState* state;
    } candidates[] = {
        { inWater_, &SwimAnim() },
        { airborne, &JumpAnim() },
        { isMoving, &RunAnim() },
    };
    const IAnimState* newState = &IdleAnim();
    for (const auto& candidate : candidates) {
        if (candidate.active) {
            newState = candidate.state;
            break;
        }
    }
    // 構え系の近接武器、または銃を左手に構えている間は武器持ちバリエーション（IdleHold/RunHold）を使う
    // （銃は常時左手に追従表示されるため、素のIdle/Runのままだと構えていないように見えてしまう）
    const Skeleton& skel = rig_->object->GetSkeleton();
    bool hasGunBone = skel.HasJoint(rig_->gunBoneName);
    auto* weaponManager = WeaponManager::GetInstance();
    // やられ演出で武器を手放した後は構えない
    bool hold = !weaponsDropped_
        && ((weaponManager->HasEquippedWeapon() && UsesHoldPose(weaponManager->GetCurrent().type)) || hasGunBone);
    if (newState == animState_ && hold == animHold_) {
        return;
    }
    animState_ = newState;
    animHold_ = hold;
    animState_->Enter(*this, hold, isMoving);
}

//  Anim State（移動系アニメーション状態）

/** @brief 装備の有無に応じた待機アニメーションを開始する状態 */
class Player::IdleAnimState : public IAnimState {
public:
    /** @brief この状態へ入る際の初期設定を適用する */
    void Enter(Player& player, bool hold, bool) const override {
        player.rig_->object->SetAnimation(hold ? player.rig_->idleHoldAnim : player.rig_->idleAnim);
    }
};
/** @brief 装備の有無に応じた走行アニメーションを開始する状態 */
class Player::RunAnimState : public IAnimState {
public:
    /** @brief この状態へ入る際の初期設定を適用する */
    void Enter(Player& player, bool hold, bool) const override {
        player.rig_->object->SetAnimation(hold ? player.rig_->runHoldAnim : player.rig_->runAnim);
    }
};
/** @brief 移動入力に応じたジャンプアニメーションを開始する状態 */
class Player::JumpAnimState : public IAnimState {
public:
    /** @brief この状態へ入る際の初期設定を適用する */
    void Enter(Player& player, bool, bool isMoving) const override {
        player.rig_->object->SetAnimation(isMoving ? player.rig_->runningJumpAnim : player.rig_->jumpAnim);
    }
};
/** @brief 水中移動のアニメーションを開始する状態 */
class Player::SwimAnimState : public IAnimState {
public:
    /** @brief この状態へ入る際の初期設定を適用する */
    void Enter(Player& player, bool, bool) const override {
        player.rig_->object->SetAnimation(player.rig_->swimAnim);
    }
};
/** @brief 攻撃制御側が設定したアニメーションを保つ状態 */
class Player::AttackAnimState : public IAnimState {
public:
    /** @brief この状態へ入る際の初期設定を適用する */
    void Enter(Player&, bool, bool) const override { }
};

/** @brief 待機アニメーションの共有インスタンスを返す */
const Player::IAnimState& Player::IdleAnim() {
    static const IdleAnimState kInstance;
    return kInstance;
}

/** @brief 走りアニメーションの共有インスタンスを返す */
const Player::IAnimState& Player::RunAnim() {
    static const RunAnimState kInstance;
    return kInstance;
}

/** @brief ジャンプアニメーションの共有インスタンスを返す */
const Player::IAnimState& Player::JumpAnim() {
    static const JumpAnimState kInstance;
    return kInstance;
}

/** @brief 泳ぎアニメーションの共有インスタンスを返す */
const Player::IAnimState& Player::SwimAnim() {
    static const SwimAnimState kInstance;
    return kInstance;
}

/** @brief 攻撃アニメーションの共有インスタンスを返す */
const Player::IAnimState& Player::AttackAnim() {
    static const AttackAnimState kInstance;
    return kInstance;
}

/** @brief 本体のボーンへ装備を付け直して各モデルの行列を更新する */
void Player::UpdateAttachedWeapons() {
    // 本体の更新を終えてから装備の位置と行列を確定する
    AttachActiveWeapons();

    for (auto& slot : heldWeapons_) {
        slot.object->Update();
    }
    for (auto& slot : guns_) {
        slot.object->Update();
    }
}

/** @brief 見た目のトランスフォーム行列だけを再計算する（ゲームロジックは一切進めない） */
void Player::RefreshVisualTransforms() {
    // アニメ状態は一切変えず、現在のpos_を使って見た目のトランスフォームだけ再計算する
    // （StageEditorのギズモ等、Update()を通さずpos_だけ直接書き換えられた場合に追従させるため）
    Vector3 modelPos = { pos_.x, pos_.y + rig_->modelOffsetY + groundVisualCorrection_, pos_.z };
    rig_->object->SetPosition(modelPos);

    // SkinnedObject3d::Update()はアニメ時刻も進めてしまうため、一時的に速度0にして完全静止させる
    float savedSpeed = rig_->object->GetAnimSpeed();
    rig_->object->SetAnimSpeed(0.0f);
    SyncMotionLayer();
    rig_->object->Update();
    rig_->object->SetAnimSpeed(savedSpeed);

    // 武器/銃は本体のボーンに追従しているため、本体の位置更新後に付け直さないと古い位置に取り残される
    UpdateAttachedWeapons();
}

/** @brief 演出用に、入力と物理を進めずに見た目だけを通常どおり進める（向き・待機/落下のアニメ・武器の付け方） */
void Player::UpdateCinematicPose(bool airborne, const Vector3& bodyRotation, bool moving) {
    // アニメの選び方は通常と同じ（宙なら落下、そうでなければ待機）。
    // 接地状態（物理の状態）は書き換えずに、宙にいるかだけを渡す
    UpdateAnimationState(moving, airborne);

    const float yaw = (lastDirX_ >= 0.0f) ? gameConstants::kHalfPi : -gameConstants::kHalfPi;
    const Vector3 modelPos = { pos_.x, pos_.y + rig_->modelOffsetY + groundVisualCorrection_, pos_.z };
    rig_->object->SetPosition(modelPos);
    rig_->object->SetRotation({ bodyRotation.x, yaw + bodyRotation.y, bodyRotation.z });
    SyncMotionLayer();
    rig_->object->Update(); // アニメの時刻も通常どおり進める

    // 手に持つ武器と銃をボーンへ付け直す（付けないと初期位置のまま本体の中に見える）
    UpdateAttachedWeapons();
}

/** @brief 入口演出用に本体の倍率と本体・装備の透明度を設定する */
void Player::SetPortalAppearance(float scale, float alpha) {
    portalAlpha_ = std::clamp(alpha, 0.0f, 1.0f);
    const float size = rig_->modelScale * scale;
    rig_->object->SetScale({ size, size, size });
    const Vector4 color = { 1.0f, 1.0f, 1.0f, portalAlpha_ };
    rig_->object->SetColor(color);
    for (auto& slot : heldWeapons_) {
        slot.object->SetColor(color);
    }
    for (auto& slot : guns_) {
        slot.object->SetColor(color);
    }
}

/** @brief やられ演出を始める（攻撃モーションを打ち切り、武器を手から放して構えないアニメにする。銃は隠す） */
void Player::BeginDefeatPose() {
    // 攻撃の途中でやられても、次の UpdateAnimationState で構えない待機か落下のアニメへ切り替わるようにする
    weaponsDropped_ = true;
    attackAnimTimer_ = 0.0f;
    rig_->object->SetAnimSpeed(1.0f);
}

/** @brief 手放した近接武器の位置とZ軸回りの回転を決める */
void Player::SetDroppedWeaponPose(const Vector3& position, float spinZ) {
    droppedWeaponPosition_ = position;
    droppedWeaponSpin_ = spinZ;
}

/** @brief 演出中のアニメの再生速度（1=通常、0で止める） */
void Player::SetCinematicAnimationSpeed(float speed) {
    rig_->object->SetAnimSpeed(speed);
}

/** @brief 演出の間だけ、加算モーションの代わりに掛けるポーズレイヤー（nullptr で加算モーションへ戻す） */
void Player::SetCinematicPoseLayer(const IPoseLayer* layer) {
    rig_->object->SetPoseLayer(layer != nullptr ? layer : &motionLayer_);
}

/** @brief 覚醒残像・アニメーション状態・プレイヤー色などUpdate()末尾の表示関連処理をまとめて更新する */
void Player::UpdateVisualState(Input* input) {
    float yaw = (lastDirX_ >= 0.0f) ? gameConstants::kHalfPi : -gameConstants::kHalfPi;

    // ── 覚醒残像スポーン＆フェード ──
    Vector3 modelPos = { pos_.x, pos_.y + rig_->modelOffsetY + groundVisualCorrection_, pos_.z };
    // バックステップは後ろへ反りながら小さく跳ねる弧で見せる（当たり判定の位置は動かさない）
    float backDodgeLean = 0.0f;
    if (dodgeActive_ && dodgeBackward_) {
        const float arc = std::sin(std::clamp(dodgeTimer_ / Tuning().dodgeDuration, 0.0f, 1.0f) * gameConstants::kPi);
        backDodgeLean = Tuning().backDodgeLean * arc;
        modelPos.y += Tuning().backDodgeHopHeight * arc;
    }
    bool isRampage = IsRampaging();
    // 回避中も薄い残像を出して、瞬間的に位置が飛んだのではなく素早く動いたことを見せる
    // 固有技の連撃中も残像を引き、回転の軌跡を見せる
    afterImageRenderer_.Update(isRampage || dodgeActive_ || meleeCombo_.IsSequenceActive(), isRampage, modelPos,
        yaw + (gunCombo_.GetActive() != nullptr ? float{} : meleeCombo_.GetBodyTurnOffset()), spinAngle_);

    // ── アニメーション状態（接地中の左右移動入力で Idle/Run、空中で Jump）──
    bool isMovingHoriz = input->PushAction(Input::Action::kMoveLeft)
        || input->PushAction(Input::Action::kMoveRight);
    UpdateAnimationState(isMovingHoriz, !onGround_);

    // ── プレイヤー色（上の行ほど優先。どれにも当たらなければ白）──
    const float rampageBlink = std::sin(juggleSlashCount_ * SceneVisualTuning::Player().rampageBlinkSpeed) * kHalfWave + kHalfWave;
    const float awakenPulse = std::sin(awakenTimer_ * SceneVisualTuning::Player().awakenPulseSpeed) * SceneVisualTuning::Player().awakenPulseAmplitude + SceneVisualTuning::Player().awakenPulseBase;
    const BodyTint tints[] = {
        { finisherCharging_, kWhite }, // 色自体は白のまま、リムの発光だけで魅せる
        { IsRampaging(), { 1.0f, 1.0f - rampageBlink * SceneVisualTuning::Player().rampageGreenFade, 1.0f - rampageBlink * SceneVisualTuning::Player().rampageBlueFade, 1.0f } }, // 白→青白点滅
        { isAwakened_, { SceneVisualTuning::Player().awakenRed * awakenPulse, SceneVisualTuning::Player().awakenGreen * awakenPulse, 1.0f, 1.0f } }, // 青くパルス
    };
    Vector4 bodyColor = kWhite;
    for (const BodyTint& tint : tints) {
        if (tint.active) {
            bodyColor = tint.color;
            break;
        }
    }
    rig_->object->SetColor(bodyColor);

    // 溜めが深まるほどリムライトを強めて集中が高まる感じを出す（解放の瞬間が一番明るい）
    float rimIntensity = finisherCharging_
        ? kRimIntensity + (1.0f - finisherChargeTimer_ / CombatTune().FinisherChargeDuration()) * kFinisherRimBoost
        : kRimIntensity;
    rig_->object->SetRimIntensity(rimIntensity);
    for (auto& slot : heldWeapons_) {
        slot.object->SetRimIntensity(rimIntensity);
    }
    for (auto& slot : guns_) {
        slot.object->SetRimIntensity(rimIntensity);
    }

    // 攻撃段ごとの体の傾き（前後=X/左右ロール=Z）。Ball旋回のspinAngle_とはZ軸を共有するが
    // 両者は排他（旋回はBall選択中のみ、攻撃レンはコンボ中のみ）なので単純加算でよい
    // 射撃の骨姿勢に近接の全身回転や傾きを重ねない
    Vector3 bodyLean = gunCombo_.GetActive() != nullptr ? Vector3{} : meleeCombo_.GetBodyLeanOffset();
    rig_->object->SetPosition(modelPos);
    // 回転斬りの段では体ごとその場で回る（向きに足すので、武器もボーン追従でいっしょに回る）
    rig_->object->SetRotation({ bodyLean.x + backDodgeLean, yaw + (gunCombo_.GetActive() != nullptr ? float{} : meleeCombo_.GetBodyTurnOffset()), spinAngle_ * gameConstants::kDegToRad + bodyLean.z });
    SyncMotionLayer();
    rig_->object->Update();
}

/** @brief 今の段と経過割合を加算モーションのレイヤーへ渡す（リグを評価する直前に呼ぶ） */
void Player::SyncMotionLayer() {
    // コンボの段タイマーを唯一の時計にして、加算キーを段の経過割合で評価する
    // 同時入力では銃のクリップだけを選び、同じ骨への二重加算を防ぐ
    const GunShotDef* shot = gunCombo_.GetActive();
    const ComboMotionLibrary* gunLibrary = ComboMotionLibrary::GetInstance();
    if (shot != nullptr) {
        motionLayer_.SetClip(gunLibrary->IsEnabled() ? gunLibrary->Find(rig_->motionRigId, shot->id) : nullptr,
            gunCombo_.GetMotionProgress());
        return;
    }
    const MeleeAttackDef* attack = meleeCombo_.GetActive();
    const ComboMotionLibrary* library = ComboMotionLibrary::GetInstance();
    const MotionClip* clip = (attack != nullptr && library->IsEnabled())
        ? library->Find(rig_->motionRigId, attack->id)
        : nullptr;
    motionLayer_.SetClip(clip, meleeCombo_.GetProgress());
}

/** @brief 近接の段を指定時刻の姿勢で見せる（モーション編集のプレビュー用） */
void Player::PreviewMeleePose(const MeleeAttackDef* attack, float normalizedTime) {
    if (attack == nullptr) {
        EndMeleePreview();
        return;
    }
    meleeCombo_.SetPreviewPose(attack, normalizedTime);

    // ベースアニメもゲーム中と同じ対応（段の経過秒 × 再生速度）で同じ時刻に合わせる
    const Animation& anim = attack->slashAnim ? rig_->slashAnim : rig_->punchAnim;
    if (previewAnim_ != &anim) {
        rig_->object->SetAnimation(anim);
        previewAnim_ = &anim;
    }
    float animTime = std::clamp(normalizedTime, 0.0f, 1.0f) * attack->duration * attack->animSpeed;
    if (anim.duration > 0.0f) {
        animTime = std::fmod(animTime, anim.duration);
    }
    rig_->object->SetAnimTime(animTime);

    // 体の傾き・その場回転も同じ段時刻で評価する
    const float yaw = (lastDirX_ >= 0.0f) ? gameConstants::kHalfPi : -gameConstants::kHalfPi;
    const Vector3 bodyLean = meleeCombo_.GetBodyLeanOffset();
    rig_->object->SetRotation({ bodyLean.x, yaw + (gunCombo_.GetActive() != nullptr ? float{} : meleeCombo_.GetBodyTurnOffset()), bodyLean.z });
    RefreshVisualTransforms();
}

/** @brief プレビューを終えて攻撃していない状態へ戻す */
void Player::EndMeleePreview() {
    meleeCombo_.Reset();
    if (previewAnim_ != nullptr) {
        // 攻撃の残り時間と速度を戻し、今の接地・構えの条件から移動系アニメへ入り直させる
        // （状態を攻撃扱いにしておくと、UpdateAnimationState()が同じ状態とみなして入り直しを省かない）
        attackAnimTimer_ = 0.0f;
        rig_->object->SetAnimSpeed(1.0f);
        animState_ = &AttackAnim();
        UpdateAnimationState(false, !onGround_);
        previewAnim_ = nullptr;
    }
    const float yaw = (lastDirX_ >= 0.0f) ? gameConstants::kHalfPi : -gameConstants::kHalfPi;
    rig_->object->SetRotation({ 0.0f, yaw, 0.0f });
    RefreshVisualTransforms();
}

/** @brief 現在のスタイルに対応する近接武器と銃を、それぞれの手ボーンへ追従させる */
void Player::AttachActiveWeapons() {
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
    // やられ演出で手放した後は、近接武器を演出が決めた位置へ置き、銃は隠す
    if (weaponsDropped_) {
        if (activeHeldIndex_ >= 0) {
            PlaceThrownWeapon(heldWeapons_[activeHeldIndex_], droppedWeaponPosition_, droppedWeaponSpin_);
        }
        thrownWeaponIndex_ = -1;
        gunVisible_ = false;
        return;
    }
    if (activeHeldIndex_ >= 0 && !BehaviorFor(heldWeapons_[activeHeldIndex_].type).IsDisarmed()) {
        auto& slot = heldWeapons_[activeHeldIndex_];
        Vector3 rot = slot.gripRotate + meleeCombo_.GetSwingOffset();
        AttachHeldWeapon(slot.object.get(), rig_->meleeBoneName, slot.gripScale, rot, slot.gripTranslate);
    }

    // 投げた武器は、途中で他の武器に持ち替えてもactiveHeldIndex_とは別に
    // 飛行/渦の位置へ動かし続ける（持ち替え直後に消えて見えないようにする）
    thrownWeaponIndex_ = -1;
    for (int i = 0; i < static_cast<int>(heldWeapons_.size()); ++i) {
        Vector3 thrownPos;
        float thrownSpinZ = 0.0f;
        if (BehaviorFor(heldWeapons_[i].type).GetThrownPose(thrownPos, thrownSpinZ)) {
            thrownWeaponIndex_ = i;
            PlaceThrownWeapon(heldWeapons_[i], thrownPos, thrownSpinZ);
            break;
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
        gunVisible_ = (skel.HasJoint(rig_->gunBoneName));
        if (gunVisible_ && activeGunIndex_ >= 0) {
            auto& slot = guns_[activeGunIndex_];
            Vector3 rot = slot.gripRotate + gunCombo_.GetPoseOffset();
            AttachHeldWeapon(slot.object.get(), rig_->gunBoneName, slot.gripScale, rot, slot.gripTranslate);
        }
    }
}

/** @brief 手元を離れて飛んでいる武器を、手のボーンから外して位置・回転を直接動かす */
void Player::PlaceThrownWeapon(HeldWeaponSlot& slot, const Vector3& position, float spinZ) {
    slot.object->ClearLocalMatrix(); // 手のボーン追従（SetLocalMatrix）を解除し、直接のTransform制御に戻す
    slot.object->SetPosition(position);
    slot.object->SetRotation({ slot.gripRotate.x, slot.gripRotate.y, spinZ });
    // 握りの大きさは手のボーンとキャラの縮尺に掛ける前提の値なので、手を離した時も同じ縮尺を掛けて持っていた時の大きさに揃える
    const float handScale = BoneWorldScale(rig_->object->GetSkeleton(), rig_->object->GetWorldMatrix(), rig_->meleeBoneName);
    slot.object->SetScale({ slot.gripScale.x * handScale, slot.gripScale.y * handScale, slot.gripScale.z * handScale });
    slot.object->Update();
}

/** @brief 右手ボーンに武器オブジェクトを追従させる（握りローカル行列は呼び出し側で指定） */
void Player::AttachHeldWeapon(Object3d* obj, const char* boneName,
    const Vector3& gripScale, const Vector3& gripRotate, const Vector3& gripTranslate) {
    AttachToBone(obj, rig_->object->GetSkeleton(), rig_->object->GetWorldMatrix(),
        boneName, gripScale, gripRotate, gripTranslate);
}

/** @brief 現在表示中の近接武器の手元ワールド座標を返す（武器トレイル演出用） */
Vector3 Player::GetActiveWeaponWorldPosition() const {
    if (activeHeldIndex_ >= 0) {
        return heldWeapons_[activeHeldIndex_].object->GetWorldPosition();
    }
    return { pos_.x, pos_.y + rig_->modelOffsetY, pos_.z };
}

/** @brief 手に持っている近接武器の種類と、武器モデル空間→ワールドの行列を返す（刃の軌跡演出用） */
bool Player::GetHeldWeaponTransform(WeaponType& outType, Matrix4x4& outMatrix) const {
    if (activeHeldIndex_ < 0 || activeHeldIndex_ == thrownWeaponIndex_) {
        return false;
    }
    const HeldWeaponSlot& slot = heldWeapons_[activeHeldIndex_];
    // 手のボーン追従中はSetLocalMatrix()の行列がそのままワールド行列になっている
    if (!slot.object->IsUsingLocalMatrix()) {
        return false;
    }
    outType = slot.type;
    outMatrix = slot.object->GetLocalMatrix();
    return true;
}

/** @brief プレイヤーモデルを描画する */
void Player::Draw() {
    if (staticOverrideObject_) {
        staticOverrideObject_->SetPosition({ pos_.x, pos_.y - kBodyHalfExtent + groundVisualCorrection_ + staticOverrideFootOffset_, pos_.z });
        staticOverrideObject_->SetRotation({ 0.0f, lastDirX_ >= 0.0f ? gameConstants::kHalfPi : -gameConstants::kHalfPi, 0.0f });
        staticOverrideObject_->Update();
        staticOverrideObject_->Draw();
        return;
    }
    // 残像（プレイヤーより先に描画して後ろに見えるようにする）
    afterImageRenderer_.Draw();

    // 黒縁アウトライン（本体・武器とも先に描いてから通常描画で上書きする）
    auto* outline = OutlineEffect::GetInstance();
    Vector4 outlineColor = SceneVisualTuning::Player().outlineColor;
    outlineColor.w *= portalAlpha_;
    outline->SetColor(outlineColor);
    outline->SetWidth(SceneVisualTuning::Player().outlineWidth);
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
        if (activeHeldIndex_ >= 0) {
            heldWeapons_[activeHeldIndex_].object->DrawOutline(outline);
        }
        if (thrownWeaponIndex_ >= 0 && thrownWeaponIndex_ != activeHeldIndex_) {
            heldWeapons_[thrownWeaponIndex_].object->DrawOutline(outline);
        }
        if (gunVisible_ && activeGunIndex_ >= 0) {
            guns_[activeGunIndex_].object->DrawOutline(outline);
        }
        rig_->object->DrawOutline(outline);
    }

    // 通常描画
    if (activeHeldIndex_ >= 0) {
        heldWeapons_[activeHeldIndex_].object->Draw();
    }
    if (thrownWeaponIndex_ >= 0 && thrownWeaponIndex_ != activeHeldIndex_) {
        heldWeapons_[thrownWeaponIndex_].object->Draw();
    }
    if (gunVisible_ && activeGunIndex_ >= 0) {
        guns_[activeGunIndex_].object->Draw();
    }
    rig_->object->Draw();
}

} // namespace engine::game
