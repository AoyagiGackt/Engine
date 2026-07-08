#include "Player.h"
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
constexpr const char* kPlayerModelDir  = "Resources/AlienAnimated/glTF";
constexpr const char* kPlayerModelFile = "Alien.gltf";
// マテリアル単色を焼き込んだパレットテクスチャ（UVは変換時にブロック中心へ書き換え済み）
constexpr const char* kPlayerTexture   = "Resources/AlienAnimated/glTF/AlienPalette.png";
// モデル身長は約2.93。当たり判定(1x1x1 AABB)の高さに合わせる
constexpr float kPlayerModelScale   = 0.34f;
// モデル原点は足元、pos_ は AABB 中心なので半分下げて足元を合わせる
constexpr float kPlayerModelOffsetY = -0.5f;

// 本体・武器の黒縁アウトライン（背景との同化と武器の視認性の低さを補う）
// 幅・濃さともに控えめにし、「異常が起きているように見える」ほど主張しないようにする
constexpr Vector4 kOutlineColor = { 0.0f, 0.0f, 0.0f, 0.55f };
constexpr float   kOutlineWidth = 0.006f;

// 手持ち近接武器の定義（右手 Palm.R にアタッチ、現在のスタイルの1つだけ表示）
// パレットテクスチャは全て変換済み。スケールはモデル実寸差を吸収し、
// 手に持った時の見た目の長さ（約1.5〜1.8）が揃うように決めている
struct HeldWeaponAsset {
    WeaponType  type;
    const char* modelPath;
    const char* texturePath;
    Vector3     gripScale;
    Vector3     gripRotate;    // ラジアン
    Vector3     gripTranslate;
};
constexpr HeldWeaponAsset kHeldWeaponAssets[] = {
    // カタナ: 原点は柄と鍔の境目、刃が +Y（実寸高さ約4.35）
    { WeaponType::Sword,      "Resources/Knight/OBJ/Katana.obj",                    "Resources/Knight/OBJ/KatanaPalette.png",                    { 0.40f, 0.40f, 0.40f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.05f, 0.0f } },
    // ダガー: 柄が根元(-Y側)、刃が +Y（実寸高さ約2.6）
    { WeaponType::Dagger,     "Resources/MedievalWeaponsPack/OBJ/Dagger.obj",       "Resources/MedievalWeaponsPack/OBJ/DaggerPalette.png",       { 0.35f, 0.35f, 0.35f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.05f, 0.0f } },
    // ハンマー: 柄が根元(-Y側)（実寸高さ約4.33）
    { WeaponType::Hammer,     "Resources/MedievalWeaponsPack/OBJ/Hammer_Small.obj", "Resources/MedievalWeaponsPack/OBJ/Hammer_SmallPalette.png", { 0.35f, 0.35f, 0.35f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.05f, 0.0f } },
    // スピア: 実寸高さ約9.7と長いため小さめのスケール
    { WeaponType::Spear,      "Resources/MedievalWeaponsPack/OBJ/Spear.obj",        "Resources/MedievalWeaponsPack/OBJ/SpearPalette.png",        { 0.18f, 0.18f, 0.18f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.05f, 0.0f } },
    // クレイモア: 柄が根元付近(Y=-0.32)、刃が +Y（実寸高さ約6.6）
    { WeaponType::Greatsword, "Resources/MedievalWeaponsPack/OBJ/Claymore.obj",     "Resources/MedievalWeaponsPack/OBJ/ClaymorePalette.png",     { 0.26f, 0.26f, 0.26f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.05f, 0.0f } },
    // 鎌: 柄の中程が原点、刃が +X 側へ張り出す（実寸高さ約5.6）
    { WeaponType::Scythe,     "Resources/MedievalWeaponsPack/OBJ/Scythe.obj",       "Resources/MedievalWeaponsPack/OBJ/ScythePalette.png",       { 0.22f, 0.22f, 0.22f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.05f, 0.0f } },
    // 両刃斧: 柄の中央が原点（Y対称、実寸高さ約6.35）
    { WeaponType::Axe,        "Resources/MedievalWeaponsPack/OBJ/Axe_Double.obj",   "Resources/MedievalWeaponsPack/OBJ/Axe_DoublePalette.png",   { 0.24f, 0.24f, 0.24f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.05f, 0.0f } },
};

// 近接武器共通のアタッチ先ボーン
constexpr const char* kMeleeWeaponBoneName = "Palm.R";

/** @brief 武器を構えた待機/走りモーション（IdleHold/RunHold）を使うタイプか */
bool UsesHoldPose(WeaponType type) {
    return type == WeaponType::Sword  || type == WeaponType::Greatsword
        || type == WeaponType::Scythe || type == WeaponType::Axe
        || type == WeaponType::Spear;
}

// 拳銃装備（左手 Palm.L にアタッチ、スタイルによらず常時表示。K キー射撃と対応）
constexpr const char* kPistolModelPath = "Resources/AnimatedFPSGuns/OBJ/Pistol.obj";
constexpr const char* kPistolTexture   = "Resources/AnimatedFPSGuns/OBJ/PistolPalette.png";
constexpr const char* kPistolBoneName  = "Palm.L";
// Palm.L ローカル空間での握り調整（モデル原点はグリップ中心、銃口が +X 方向）
constexpr Vector3 kPistolGripScale     = { 0.035f, 0.035f, 0.035f };
constexpr Vector3 kPistolGripRotate    = { 0.0f, 0.0f, 0.0f }; // ラジアン
constexpr Vector3 kPistolGripTranslate = { 0.0f, 0.0f, 0.0f };

// 発砲時の跳ね上がり（反動）演出
constexpr float kPistolRecoilDuration = 0.08f;
constexpr float kPistolRecoilAngle    = 0.35f; // ラジアン

// 斬撃モーションの再生速度倍率（コンボのテンポに合わせて少し速める）
constexpr float kAttackAnimSpeed = 1.5f;

// フィニッシャー：静止集中の長さ（GamePlayScene/BattleTestScene の斬撃線バラマキ演出と一致させる：
// kFinisherChargeDelay → 斬撃線を kFinisherSlashLines 本 kFinisherLineInterval 間隔で出す → kFinisherImpactDelay で解放）
// 素材にバージルの次元斬のような納刀ポーズは無いため、Idle/IdleHold を静止させて代用する
constexpr float kFinisherChargeDuration   = GameConstants::kFinisherChargeDelay
                                           + GameConstants::kFinisherSlashLines * GameConstants::kFinisherLineInterval
                                           + GameConstants::kFinisherImpactDelay;
constexpr float kFinisherReleaseAnimSpeed = 3.0f; // 解放の一閃は目にも留まらぬ速さで

// 武器奪取の刺突（バージル的:ぶっ刺す→奪う演出の仮モーション、専用素材が無いため斬撃を流用）
constexpr float kStealStabAnimSpeed = 1.2f;
}

void Player::Initialize(ModelCommon* modelCommon)
{
    modelCommon_ = modelCommon;

    // 残像・分身演出用の静的モデル（ボーンなし、ボーン付きモデルと同じ見た目）
    model_ = std::make_unique<Model>();
    model_->Initialize(modelCommon,
        "Resources/AlienAnimated/OBJ/Alien.obj",
        kPlayerTexture);

    // 本体（ボーンアニメーション付き）
    skinCommon_ = std::make_unique<SkinCommon>();
    skinCommon_->Initialize(modelCommon->GetDxCommon());

    std::string modelPath = std::string(kPlayerModelDir) + "/" + kPlayerModelFile;
    skinnedModel_ = std::make_unique<SkinnedModel>();
    skinnedModel_->Initialize(modelCommon->GetDxCommon(), modelPath, kPlayerTexture);

    Skeleton skeleton = Skeleton::Create(LoadNodeHierarchyFromFile(kPlayerModelDir, kPlayerModelFile));

    idleAnim_        = LoadAnimationFile(kPlayerModelDir, kPlayerModelFile, "Alien_Idle");
    runAnim_         = LoadAnimationFile(kPlayerModelDir, kPlayerModelFile, "Alien_Run");
    jumpAnim_        = LoadAnimationFile(kPlayerModelDir, kPlayerModelFile, "Alien_Jump");
    runningJumpAnim_ = LoadAnimationFile(kPlayerModelDir, kPlayerModelFile, "Alien_RunningJump");
    swimAnim_        = LoadAnimationFile(kPlayerModelDir, kPlayerModelFile, "Alien_Swimming");
    idleHoldAnim_    = LoadAnimationFile(kPlayerModelDir, kPlayerModelFile, "Alien_IdleHold");
    runHoldAnim_     = LoadAnimationFile(kPlayerModelDir, kPlayerModelFile, "Alien_RunHold");
    slashAnim_       = LoadAnimationFile(kPlayerModelDir, kPlayerModelFile, "Alien_SwordSlash");
    punchAnim_       = LoadAnimationFile(kPlayerModelDir, kPlayerModelFile, "Alien_Punch");

    SkinnedObject3d::SetCommonModelCommon(modelCommon);
    SkinnedObject3d::SetCommonCamera(Object3d::GetCommonCamera());

    skinnedObject_ = std::make_unique<SkinnedObject3d>();
    skinnedObject_->Initialize(skinCommon_.get());
    skinnedObject_->SetModel(skinnedModel_.get());
    skinnedObject_->SetSkeleton(std::move(skeleton));
    skinnedObject_->SetAnimation(idleAnim_);
    // ライティング有効 + リムライトで背景からシルエットを分離させる
    skinnedObject_->SetEnableLighting(true);
    skinnedObject_->SetRimColor({ 0.4f, 0.9f, 1.0f });
    skinnedObject_->SetRimPower(2.5f);
    skinnedObject_->SetRimIntensity(1.2f);
    skinnedObject_->SetEnableRim(true);
    skinnedObject_->SetScale({ kPlayerModelScale, kPlayerModelScale, kPlayerModelScale });
    skinnedObject_->SetPosition({ pos_.x, pos_.y + kPlayerModelOffsetY, pos_.z });
    skinnedObject_->Update();
    animState_ = AnimState::Idle;

    afterImageRenderer_.Initialize(modelCommon, model_.get(), kPlayerModelScale);

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
    for (const auto& asset : kHeldWeaponAssets) {
        HeldWeaponSlot slot;
        slot.type          = asset.type;
        slot.gripScale     = asset.gripScale;
        slot.gripRotate    = asset.gripRotate;
        slot.gripTranslate = asset.gripTranslate;
        initHeldWeapon(slot.model, slot.object, asset.modelPath, asset.texturePath);
        heldWeapons_.push_back(std::move(slot));
    }

    // 拳銃（左手ボーン追従、スタイルによらず常時表示）
    initHeldWeapon(pistolModel_, pistolObject_, kPistolModelPath, kPistolTexture);
}

void Player::UpdateAnimationState(bool isMoving)
{
    // フィニッシャー溜め中は静止ポーズを維持（タイマー管理は Update() 側）
    if (finisherCharging_) { return; }

    // 攻撃モーション中は再生し切るまで状態遷移しない
    if (attackAnimTimer_ > 0.0f) {
        attackAnimTimer_ -= GameConstants::kFrameDeltaTime;
        if (attackAnimTimer_ > 0.0f) { return; }
        skinnedObject_->SetAnimSpeed(1.0f); // 攻撃用の速度倍率を戻す
    }

    AnimState newState = inWater_    ? AnimState::Swim
                        : !onGround_ ? AnimState::Jump
                        : isMoving   ? AnimState::Run
                                     : AnimState::Idle;
    // 構え系の武器では武器持ちバリエーション（IdleHold/RunHold）を使う
    bool hold = UsesHoldPose(WeaponManager::GetInstance()->GetCurrent().type);
    if (newState == animState_ && hold == animHold_) { return; }
    animState_ = newState;
    animHold_  = hold;

    switch (animState_) {
    case AnimState::Swim: skinnedObject_->SetAnimation(swimAnim_); break;
    case AnimState::Run:  skinnedObject_->SetAnimation(hold ? runHoldAnim_ : runAnim_); break;
    case AnimState::Jump: skinnedObject_->SetAnimation(isMoving ? runningJumpAnim_ : jumpAnim_); break;
    default:              skinnedObject_->SetAnimation(hold ? idleHoldAnim_ : idleAnim_); break;
    }
}

void Player::PlayStealStab()
{
    PlayAttackAnim(slashAnim_, kStealStabAnimSpeed);
}

int Player::GetComboMax() const
{
    // 現在の武器の地上コンボ段数を基準にする（HUDの「x段目/最大」表示用）
    const MeleeComboSet& set = GetMeleeComboSet(WeaponManager::GetInstance()->GetCurrent().type);
    return set.groundCount + skillMods_.comboMaxBonus;
}

void Player::PlayAttackAnim(const Animation& anim, float speed)
{
    skinnedObject_->SetAnimation(anim);
    skinnedObject_->SetAnimSpeed(speed);
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

    // ── 射撃（K キー、水上のみ）─────────────────────────────────────
    if (!inWater_ && input->TriggerKey(DIK_K)) {
        justFired_        = true;
        pistolRecoilTimer_ = kPistolRecoilDuration;
    }
    if (pistolRecoilTimer_ > 0.0f) {
        pistolRecoilTimer_ -= GameConstants::kFrameDeltaTime;
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
            PlayAttackAnim(atk->slashAnim ? slashAnim_ : punchAnim_, atk->animSpeed);
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
    // バージルの次元斬を意識し、静止して集中 → 溜め切ったら一閃、の2段構成にする
    if (!inWater_ && !isAwakened_ && !finisherCharging_ && input->TriggerKey(DIK_F) && awakenGauge_ >= 1.0f) {
        justFinisherSlash_   = true;
        awakenGauge_         = 0.0f; // ゲージを全消費
        finisherCharging_    = true;
        finisherChargeTimer_ = kFinisherChargeDuration;
        meleeCombo_.Reset(); // 進行中のコンボは打ち切って静止に入る
        skinnedObject_->SetAnimation(UsesHoldPose(wm->GetCurrent().type) ? idleHoldAnim_ : idleAnim_);
        skinnedObject_->SetAnimSpeed(0.0f); // 呼吸すら止めるように完全静止
    }

    // ── フィニッシャー溜めの経過 → 解放（一閃）─────────────────────
    if (finisherCharging_) {
        finisherChargeTimer_ -= GameConstants::kFrameDeltaTime;
        if (finisherChargeTimer_ <= 0.0f) {
            finisherCharging_ = false;
            PlayAttackAnim(slashAnim_, kFinisherReleaseAnimSpeed);
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
        PlayAttackAnim(slashAnim_, kAttackAnimSpeed);
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

    // 入水・出水判定（物理後の位置で確定）
    inWater_          = (pos_.y < waterLevel_);
    justEnteredWater_ = !prevInWater_ && inWater_;
    justExitedWater_  = prevInWater_  && !inWater_;

    // 入水衝撃吸収（落下速度を大きく減衰）
    if (justEnteredWater_) { velocityY_ *= 0.4f; }

    float yaw = (lastDirX_ >= 0.0f) ? GameConstants::kHalfPi : -GameConstants::kHalfPi;

    // ── 覚醒残像スポーン＆フェード ──
    Vector3 modelPos = { pos_.x, pos_.y + kPlayerModelOffsetY, pos_.z };
    bool isRampage = (rampagePhase_ != RampagePhase::Inactive);
    afterImageRenderer_.Update(isAwakened_ || isRampage, isRampage, modelPos, yaw, spinAngle_);

    // ── アニメーション状態（接地中の左右移動入力で Idle/Run、空中で Jump）──
    bool isMovingHoriz = input->PushKey(DIK_A) || input->PushKey(DIK_D)
                       || input->PushKey(DIK_LEFT) || input->PushKey(DIK_RIGHT);
    UpdateAnimationState(isMovingHoriz);

    // ── プレイヤー色 ──
    if (finisherCharging_) {
        skinnedObject_->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f }); // 色自体は白のまま、リムの発光だけで魅せる
    } else if (rampagePhase_ != RampagePhase::Inactive) {
        float t = std::sin(juggleSlashCount_ * 3.0f) * 0.5f + 0.5f;
        skinnedObject_->SetColor({ 1.0f, 1.0f - t * 0.4f, 1.0f - t * 0.6f, 1.0f }); // 白→青白点滅
    } else if (justChargedGauge_) {
        skinnedObject_->SetColor({ 1.0f, 0.85f, 0.0f, 1.0f }); // 黄（ハンマーチャージ）
    } else if (isAwakened_) {
        float t = std::sin(awakenTimer_ * 6.0f) * 0.3f + 0.7f;
        skinnedObject_->SetColor({ 0.15f * t, 0.55f * t, 1.0f, 1.0f }); // 青くパルス
    } else {
        skinnedObject_->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
    }

    // 溜めが深まるほどリムライトを強めて「集中が高まる」感じを出す（解放の瞬間が一番明るい）
    float rimIntensity = finisherCharging_
        ? 1.2f + (1.0f - finisherChargeTimer_ / kFinisherChargeDuration) * 3.0f
        : 1.2f;
    skinnedObject_->SetRimIntensity(rimIntensity);
    for (auto& slot : heldWeapons_) { slot.object->SetRimIntensity(rimIntensity); }
    pistolObject_->SetRimIntensity(rimIntensity);

    skinnedObject_->SetPosition(modelPos);
    skinnedObject_->SetRotation({ 0.0f, yaw, spinAngle_ * GameConstants::kDegToRad });
    skinnedObject_->Update();

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
        AttachHeldWeapon(slot.object.get(), kMeleeWeaponBoneName, slot.gripScale, rot, slot.gripTranslate);
    }

    // ── 拳銃を左手ボーンに追従（スタイルによらず常時表示、発砲時は跳ね上がる）──
    {
        const Skeleton& skel = skinnedObject_->GetSkeleton();
        auto jt = skel.jointMap.find(kPistolBoneName);
        pistolVisible_ = (jt != skel.jointMap.end());
        if (pistolVisible_) {
            // 反動: 発砲直後に上向きへ跳ね、時間経過で構え直しに戻る
            float recoilT = (pistolRecoilTimer_ > 0.0f) ? (pistolRecoilTimer_ / kPistolRecoilDuration) : 0.0f;
            Vector3 recoilRotate = {
                kPistolGripRotate.x - kPistolRecoilAngle * recoilT,
                kPistolGripRotate.y,
                kPistolGripRotate.z
            };
            Matrix4x4 grip = MakeAffineMatrix(kPistolGripScale, recoilRotate, kPistolGripTranslate);
            Matrix4x4 pistolWorld = Multiply(grip,
                Multiply(skel.joints[jt->second].skeletonSpaceMatrix, skinnedObject_->GetWorldMatrix()));
            pistolObject_->SetLocalMatrix(pistolWorld);
            pistolObject_->Update();
        }
    }
}

void Player::AttachHeldWeapon(Object3d* obj, const char* boneName,
    const Vector3& gripScale, const Vector3& gripRotate, const Vector3& gripTranslate)
{
    const Skeleton& skel = skinnedObject_->GetSkeleton();
    auto jt = skel.jointMap.find(boneName);
    if (jt == skel.jointMap.end()) { return; }

    // 握りローカル → ジョイントのスケルトン空間 → プレイヤーワールド の順で合成
    Matrix4x4 grip  = MakeAffineMatrix(gripScale, gripRotate, gripTranslate);
    Matrix4x4 world = Multiply(grip,
        Multiply(skel.joints[jt->second].skeletonSpaceMatrix, skinnedObject_->GetWorldMatrix()));
    obj->SetLocalMatrix(world);
    obj->Update();
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
    if (pistolVisible_ && pistolObject_) { pistolObject_->DrawOutline(outline); }
    skinnedObject_->DrawOutline(outline);
    if (modelCommon_) {
        modelCommon_->CommonDrawSettings();
        // ルートシグネチャの切り替えでライト/シャドウマップの束縛が失われているため再バインドする
        Object3d::RebindCommonLighting(modelCommon_->GetDxCommon()->GetCommandList());
    }

    // 通常描画
    if (activeHeldIndex_ >= 0) { heldWeapons_[activeHeldIndex_].object->Draw(); }
    if (pistolVisible_ && pistolObject_) {
        pistolObject_->Draw();
    }
    skinnedObject_->Draw();
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
