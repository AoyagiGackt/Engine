/**
 * @file BattleTestScene.cpp
 * @brief BattleTestSceneのゲームシーンの初期化、更新、描画、遷移に関する具体的な処理を実装するファイル
 */
#include "BattleTestScene.h"
#include "BattleTestSceneRenderer.h"
#include "AudioBridge.h"
#include "Collision.h"
#include "DiagnosticsDraw.h"
#include "GameConstants.h"
#include "GrayscaleEffect.h"
#include "HsvFilter.h"
#include "ImGuiControl.h"
#include "PipelineStateGuard.h"
#include "PlayerBridge.h"
#include "PostEffectRenderTarget.h"
#include "SceneManager.h"
#include "ScreenFlash.h"
#include "SlashMark.h"
#include "StageEditor.h"
#include "TimeManager.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
using namespace engine;
using namespace engine::graphics;
using namespace engine::game;

static constexpr float kWarpRetX = 3.0f;
static constexpr float kReturnProx = 3.0f;
static constexpr float kDummyMaxHp = 100.0f;

// 固有技（スペースキー）のダミー用ヒット定義。ApplyMeleeHitToDummy が参照するのは
// id/damageMult/knockX/knockY/launcher/hitStop のみなので、コンボ制御用フィールドは0で埋める
static constexpr MeleeAttackDef kSwordDashSkill = { "swd_dash", 0.9f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.30f, 0.05f, false, 5, 0.0f, false, { }, { }, { }, { } };
static constexpr MeleeAttackDef kSpearRetreatSkill = { "spr_retreat", 0.7f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.10f, 0.02f, false, 4, 0.0f, false, { }, { }, { }, { } };
static constexpr MeleeAttackDef kGreatswordSlamSkill = { "gs_slam", 1.6f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.40f, 0.30f, true, 10, 0.0f, false, { }, { }, { }, { } };
static constexpr MeleeAttackDef kAxeChargeSkill = { "axe_charge", 1.1f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.35f, 0.08f, false, 6, 0.0f, false, { }, { }, { }, { } };

// 近接攻撃・固有技の判定ボックス関連
static constexpr float kSkillRearReachMult = 0.4f; ///< 指向性判定の背面リーチ（前方リーチ比）
static constexpr float kLockAssistReachMult = 1.6f; ///< ロックオン中に前方リーチへ掛ける補正
static constexpr float kLockAssistRearMult = 0.6f; ///< ロックオン中の背面リーチ（前方リーチ比）
static constexpr float kSlamRangeBelowY = 1.0f; ///< 設置型AoEの足元方向の厚み
static constexpr float kSlamRangeAboveY = 1.5f; ///< 設置型AoEの頭上方向の厚み
static constexpr float kStageHalfDepth = 0.5f; ///< 2.5Dステージの奥行き半分
static constexpr Vector4 kSlamFlashColor = { 1.0f, 0.6f, 0.3f, 0.30f };
static constexpr float kSlamFlashDuration = 0.10f;

// 射撃コンボのマズルフラッシュ演出
static constexpr float kMuzzleBaseSpeed = 8.0f; ///< 扇状パーティクルの基本速度
static constexpr float kMuzzleSpeedStep = 1.0f; ///< 1粒ごとの速度加算
static constexpr float kMuzzleLifeTime = 0.35f; ///< パーティクル寿命（秒）
static constexpr float kMuzzleScale = 0.14f; ///< パーティクルの大きさ

/**
 * @brief 固有技（スペースキー）1件ぶんの発生条件と判定パラメータ
 * @note 新しい武器固有技はこの表に1行足すだけで追加できる
 */
struct WeaponSkillEntry {
    bool (Player::*justTriggered)() const; ///< 発生フレームを返すPlayerのゲッター
    const MeleeAttackDef* def; ///< ダメージ・ノックバック定義
    float reachMult; ///< weapon.range に掛ける射程係数
    bool symmetricAoE; ///< true=前後対称の設置型AoE / false=前方指向性
    bool screenImpact; ///< true=ヒットストップ＋画面フラッシュの大技演出つき
};
static constexpr WeaponSkillEntry kWeaponSkills[] = {
    { &Player::JustSwordDash, &kSwordDashSkill, 0.80f, false, false },
    { &Player::JustSpearRetreat, &kSpearRetreatSkill, 0.90f, false, false },
    { &Player::JustGreatswordSlam, &kGreatswordSlamSkill, 1.00f, true, true },
    { &Player::JustAxeCharge, &kAxeChargeSkill, 0.85f, false, false },
};

// ══════════════════════════════════════════════════════
// シーン初期化
// ══════════════════════════════════════════════════════

void BattleTestScene::Initialize(DirectXCommon* dxCommon, Input* input, Audio* audio)
{
    spriteCommon_ = InitializeCommonResources(dxCommon, input, audio, dxCommon_, input_, audio_);

    InitializeCoreSystems();
    InitializeStageModels();
    InitializeDummyEnemies();
    InitializePlayerAndBullets();
    InitializeHud();
    InitializeEffects();
}

void BattleTestScene::InitializeCoreSystems()
{
    srvManager_ = SrvManager::GetInstance();
    weaponManager_ = WeaponManager::GetInstance();
    pm_ = ParticleManager::GetInstance();

    // テストシーンでは全武器のコンボを試せるように最初から全解放する
    weaponManager_->UnlockAll();

    grayscaleEffect_ = GrayscaleEffect::GetInstance();
    imageFilter_ = ImageFilter::GetInstance();
    hsvFilter_ = HsvFilter::GetInstance();

    modelCommon_ = std::make_unique<ModelCommon>();
    modelCommon_->Initialize(dxCommon_);

    objectCommon_ = std::make_unique<Object3dCommon>();
    objectCommon_->Initialize(dxCommon_);

    shadowManager_ = std::make_unique<ShadowManager>();
    shadowManager_->Initialize(dxCommon_, srvManager_);

    // OutlineEffect等でルートシグネチャを切り替えた後にライト/シャドウマップを再バインドできるようにする
    Object3d::SetCommonObjectCommon(objectCommon_.get());
    Object3d::SetCommonShadowManager(shadowManager_.get());
    SkinnedObject3d::SetCommonObjectCommon(objectCommon_.get());
    SkinnedObject3d::SetCommonShadowManager(shadowManager_.get());

    camera_ = std::make_unique<Camera>();
    camera_->SetTranslate({ 19.0f, 6.0f, -24.0f });
    Object3d::SetCommonCamera(camera_.get());
}

void BattleTestScene::InitializeStageModels()
{
    modelBlock_ = std::make_unique<Model>();
    modelBlock_->Initialize(modelCommon_.get(),
        "Resources/block/block.obj",
        "Resources/block/block.png");

    cityBackgroundModel_ = std::make_unique<Model>();
    cityBackgroundModel_->Initialize(modelCommon_.get(),
        "Resources/DowntownCityMegaKit[Standard]/Exports/glTF (Godot)/Building_Small_1.gltf",
        "Resources/DowntownCityMegaKit[Standard]/Textures/T_RedBrick_BaseColor.png");
    for (float x : { 5.0f, 16.0f, 27.0f }) {
        auto city = std::make_unique<Object3d>();
        city->Initialize(modelCommon_.get());
        city->SetModel(cityBackgroundModel_.get());
        city->SetPosition({ x, -0.6f, 6.0f });
        city->SetScale({ 0.42f, 0.42f, 0.42f });
        city->Update();
        cityBackgroundObjects_.push_back(std::move(city));
    }

    // 境界ブロック・トリガーは本番ステージと同じ level01.json から読み込む
    // （GetEditorLevelPath()経由でBaseScene::Init()が自動でOpen()する。F2でその場編集も可能）

    for (int i = 0; i < 5; ++i) {
        auto p = std::make_unique<Object3d>();
        p->Initialize(modelCommon_.get());
        p->SetModel(modelBlock_.get());
        p->SetEnableLighting(false);
        p->SetPosition({ kWarpRetX, 0.4f + static_cast<float>(i) * 1.0f, 0.0f });
        p->SetColor({ 1.0f, 0.5f, 0.1f, 0.9f });
        p->Update();
        warpPortalBlocks_.push_back(std::move(p));
    }

    modelDummy_ = std::make_unique<Model>();
    modelDummy_->Initialize(modelCommon_.get(),
        "Resources/block/block.obj",
        "Resources/monsterBall.png");

    SceneShared::CreateParticleGroupsFromJson(pm_, "Resources/particles/battletest.json");
}

void BattleTestScene::InitializeDummyEnemies()
{
    knight_ = std::make_unique<KnightEnemy>();
    knight_->Initialize(modelCommon_.get(), { 20.0f, 0.4f, 0.0f });
    GetStageEditor().RegisterExternalEntity("Knight", &knight_->GetPositionRef());

    Dummy d;
    d.pos = { 15.0f, 0.4f, 0.0f };
    d.homePos = d.pos;
    d.hp = kDummyMaxHp;
    d.maxHp = kDummyMaxHp;
    d.hitFlash = 0.0f;

    d.object = std::make_unique<Object3d>();
    d.object->Initialize(modelCommon_.get());
    d.object->SetModel(modelDummy_.get());
    d.object->SetEnableLighting(false);
    d.object->SetPosition(d.pos);
    d.object->Update();

    d.hpBarBg = std::make_unique<Sprite>();
    d.hpBarBg->Initialize(spriteCommon_.get(), "Resources/white.png");
    d.hpBarBg->SetColor({ 0.2f, 0.2f, 0.2f, 0.8f });

    d.hpBarFg = std::make_unique<Sprite>();
    d.hpBarFg->Initialize(spriteCommon_.get(), "Resources/white.png");
    d.hpBarFg->SetColor({ 0.2f, 0.9f, 0.2f, 0.9f });

    dummies_.push_back(std::move(d));
}

void BattleTestScene::InitializePlayerAndBullets()
{
    player_ = std::make_unique<Player>();
    player_->Initialize(modelCommon_.get());
    // "Player"のRegisterExternalEntity()はGetEditorPlayerPositionRef()経由でBaseScene::Init()が自動で行う
    PlayerBridge::GetInstance()->SetPlayer(player_.get());
    AudioBridge::GetInstance()->SetAudio(audio_);

    bulletPool_.Initialize(modelCommon_.get(), modelBlock_.get());
}

void BattleTestScene::InitializeHud()
{
    awakenGaugeBg_ = std::make_unique<Sprite>();
    awakenGaugeBg_->Initialize(spriteCommon_.get(), "Resources/white.png");
    awakenGaugeBg_->SetColor({ 0.05f, 0.05f, 0.15f, 0.75f });

    awakenGaugeFg_ = std::make_unique<Sprite>();
    awakenGaugeFg_->Initialize(spriteCommon_.get(), "Resources/white.png");

    InitializeWeaponSlotHud();

    styleMeter_.Initialize(spriteCommon_.get());

    fontRenderer_.Initialize(spriteCommon_.get());
    SlashMark::GetInstance()->Initialize(spriteCommon_.get());

    finisherOverlay_ = SceneShared::CreateFinisherOverlay(spriteCommon_.get());

    glassShatterBgSprite_ = std::make_unique<Sprite>();
    glassShatterBgSprite_->Initialize(spriteCommon_.get(), "Resources/white.png");
    glassShatterBgSprite_->SetPosition({ 0.0f, 0.0f });
    glassShatterBgSprite_->SetSize({ static_cast<float>(WinApp::kClientWidth),
        static_cast<float>(WinApp::kClientHeight) });
}

void BattleTestScene::InitializeEffects()
{
    glassShatter_.Initialize(dxCommon_, srvManager_);
    bladeFlash_.Initialize(dxCommon_);
    spaceWarp_.Initialize(dxCommon_, srvManager_);
    dummySlice_.Initialize(dxCommon_);
    finisherShatter_.Initialize(dxCommon_, srvManager_);
    finisherShatter_.SetDuration(0.9f);
    ImGuiControlPanel::RegisterGlassShatterTrigger([this]() { TriggerGlassShatterTest(); });
}

void BattleTestScene::SpawnHitEffect(const Vector3& pos)
{
    pm_->EmitRing("bt_hit_ring", pos, 3.0f, { 1.0f, 0.85f, 0.2f, 1.0f }, 12, 0.25f, 0.18f);
    static std::mt19937 rng { std::random_device { }() };
    std::uniform_real_distribution<float> vx(-3.0f, 3.0f);
    std::uniform_real_distribution<float> vy(2.0f, 5.0f);
    for (int i = 0; i < 6; ++i) {
        pm_->EmitGravity("bt_hit_spark", pos,
            { vx(rng), vy(rng), 0.0f },
            { 1.0f, 0.55f, 0.1f, 1.0f }, 0.6f, 0.13f);
    }
}

void BattleTestScene::UpdateHpBars()
{
    const Vector3& cam = camera_->GetTranslate();
    constexpr float kBarW = 60.0f;
    constexpr float kBarH = 8.0f;
    constexpr float kBarUp = 70.0f;

    for (auto& d : dummies_) {
        float sx, sy;
        SceneShared::WorldToScreen(d.pos.x, d.pos.y, cam.x, cam.y, sx, sy);

        float ratio = d.hpDisplay;
        d.hpBarFg->SetColor({ 1.0f - ratio, ratio * 0.85f + 0.15f, 0.0f, 0.9f });

        d.hpBarBg->SetPosition({ sx - kBarW * 0.5f, sy - kBarUp });
        d.hpBarBg->SetSize({ kBarW, kBarH });
        d.hpBarBg->Update();

        d.hpBarFg->SetPosition({ sx - kBarW * 0.5f, sy - kBarUp });
        d.hpBarFg->SetSize({ kBarW * ratio, kBarH });
        d.hpBarFg->Update();
    }
}

// ══════════════════════════════════════════════════════
// シーン更新
// ══════════════════════════════════════════════════════

void BattleTestScene::Update()
{
    if (glassShatter_.IsActive()) {
        glassShatter_.Update(GameConstants::kFrameDeltaTime);
        return;
    }

    UpdateSceneFlow();
}

void BattleTestScene::UpdateSceneFlow()
{
    fontRenderer_.Reset();

    // StageEditor::Update()（F2トグル・パネル・トリガー判定）と、エディタ表示中の一時停止分岐は
    // BaseScene::Tick()が面倒を見る（表示中はこのUpdate()自体が呼ばれずRefreshVisualTransformsForEditor()が代わりに呼ばれる）

    if (input_->TriggerKey(DIK_F3)) {
        showColliders_ = !showColliders_;
    }
    if (showColliders_) {
        DrawColliderOverlay();
    }

    SceneShared::UpdateWeaponCycle(input_, weaponManager_, weaponCycleTimer_, true);
    UpdateTargetLock();
    UpdatePlayerAndCamera();
    // ステージエディタでsolidにしたブロックとの当たり判定（追加/移動/削除が次フレームからそのまま反映される）
    player_->ResolveBlockCollision(GetStageEditor().GetSolidColliders());
    UpdateEnvironment();

    UpdateCombat();
    UpdateFinisherSlash();
    styleMeter_.Update(GameConstants::kFrameDeltaTime);
    UpdateDummies();
    UpdateKnightEnemy();
    UpdatePlacedKnights();
    UpdateWeaponSlotHud();

    dummySlice_.Update(GameConstants::kFrameDeltaTime, camera_.get());
    bladeFlash_.Update(GameConstants::kFrameDeltaTime, camera_.get());

    // 切断演出が飛散に移ったら、隠していたダミーを再表示する
    if (dummySlice_.IsBursting() || !dummySlice_.IsActive()) {
        for (auto& d : dummies_) {
            d.sliced = false;
        }
    }

    // プレイヤー位置を画面UVへ投影して空間歪みの中心に設定する
    if (spaceWarp_.IsActive() || finisherActive_) {
        const Vector3& pp = player_->GetPosition();
        const Matrix4x4 vp = Multiply(camera_->GetViewMatrix(), camera_->GetProjectionMatrix());
        const float cx = pp.x * vp.m[0][0] + pp.y * vp.m[1][0] + pp.z * vp.m[2][0] + vp.m[3][0];
        const float cy = pp.x * vp.m[0][1] + pp.y * vp.m[1][1] + pp.z * vp.m[2][1] + vp.m[3][1];
        const float cw = pp.x * vp.m[0][3] + pp.y * vp.m[1][3] + pp.z * vp.m[2][3] + vp.m[3][3];
        if (cw > 0.0001f) {
            spaceWarp_.SetCenterUV(cx / cw * 0.5f + 0.5f, 0.5f - cy / cw * 0.5f);
        }
    }
    spaceWarp_.Update(GameConstants::kFrameDeltaTime);

    // 解放時の世界割れ
    finisherShatter_.Update(GameConstants::kFrameDeltaTime);
    if (finisherShatter_.IsFinished()) {
        finisherShatter_.Reset();
    }

    SlashMark::GetInstance()->Update(GameConstants::kFrameDeltaTime);

    bool nearReturn = SceneShared::UpdatePortalTransition(input_, player_->GetPosition(), kWarpRetX, kReturnProx, "TRAINING");
    DrawHud(nearReturn);
}

void BattleTestScene::RefreshVisualTransformsForEditor()
{
    // ステージエディタ表示中はゲームプレイ（敵AI・プレイヤー操作・カメラ追従）を丸ごと止める
    // TimeManagerのタイムスケールだけでは、このシーンの各種Updateが固定dt(GameConstants::kFrameDeltaTime)で
    // 動いてしまい止まらないため、BaseScene::Tick()がUpdate()の代わりにこちらを呼ぶ

    // Object3dのUpdate()はカメラのVP行列込みで定数バッファを書くため、
    // WASDでカメラを動かしても正しい位置に描かれるよう、全モデルの行列だけは毎フレーム再計算する
    // （これを怠ると古いカメラ行列のまま描画され、モデルが画面に張り付いてついてくるように見える）
    player_->RefreshVisualTransforms();
    if (knight_) {
        knight_->RefreshVisualTransforms();
    }
    bulletPool_.RefreshVisualTransforms();
    for (auto& d : dummies_) {
        d.object->Update();
    }
    for (auto& p : warpPortalBlocks_) {
        p->Update();
    }
    for (auto& city : cityBackgroundObjects_) {
        city->Update();
    }
    // 武器スロットの3Dアイコン（カメラ相対配置のHUD）とHPバー（WorldToScreen配置）はカメラ移動に追従させる
    UpdateWeaponSlotHud();
    UpdateHpBars();
}

void BattleTestScene::UpdatePlayerAndCamera()
{
    // 乱舞のターゲット ロック中ならその対象、そうでなければ最も近いダミー
    {
        const Vector3& pp = player_->GetPosition();
        Vector3 rampTarget = { pp.x + player_->GetLastDirX() * 6.0f, pp.y, 0.0f };

        if (lockedKind_ == LockTargetKind::Knight && knight_) {
            rampTarget = knight_->GetPosition();
        } else if (lockedKind_ == LockTargetKind::Dummy && lockedDummyIndex_ < dummies_.size()) {
            rampTarget = dummies_[lockedDummyIndex_].pos;
        } else {
            float minDist = FLT_MAX;
            for (const auto& d : dummies_) {
                float dist = std::abs(d.pos.x - pp.x);
                if (dist < minDist) {
                    minDist = dist;
                    rampTarget = d.pos;
                }
            }
        }
        player_->Update(input_, rampTarget);
    }

    SceneShared::UpdateCameraFollow(camera_.get(), player_->GetPosition());
}

void BattleTestScene::UpdateEnvironment()
{
    shadowManager_->Update(objectCommon_->GetLightDirection());
    Object3d::SetLightViewProjection(shadowManager_->GetLightViewProjection());
    // GetStageEditor().UpdateObjects()はBaseScene::Tick()がUpdate()の後に一括して呼ぶ

    warpPulseTimer_ += GameConstants::kFrameDeltaTime;
    float pulse = 0.6f + 0.4f * std::sin(warpPulseTimer_ * 4.0f);
    for (auto& p : warpPortalBlocks_) {
        p->SetColor({ 1.0f * pulse, 0.5f * pulse, 0.1f * pulse, 0.9f });
        p->Update();
    }
    for (auto& city : cityBackgroundObjects_) {
        city->Update();
    }
}

AABB BattleTestScene::DummyBounds(const Dummy& d)
{
    // ダミーは 1×1×1 の正方形として扱う
    return { { d.pos.x - 0.5f, d.pos.y - 0.5f, -0.5f },
        { d.pos.x + 0.5f, d.pos.y + 0.5f, 0.5f } };
}

float BattleTestScene::ComputeAttackMult() const
{
    return (player_->IsAwakened() ? 1.5f : 1.0f) * player_->GetAxeRageMult();
}

// ══════════════════════════════════════════════════════
// 戦闘判定
// ══════════════════════════════════════════════════════

bool BattleTestScene::UpdateMeleeComboHit()
{
    // 格闘コンボ（L キー）
    // ヒットはボタン押下の瞬間ではなく、モーション中の hitTime で発生する（MeleeComboController 管理）。
    // 連打間隔・段ごとの威力/リーチ/打ち上げは全て武器タイプ別の MeleeAttackDef が持つ
    if (!player_->JustComboHit()) {
        return false;
    }

    const WeaponData& weapon = weaponManager_->GetCurrent();
    const Vector3& pp = player_->GetPosition();
    const float atkMult = ComputeAttackMult();

    bool hitConfirmed = false;
    const MeleeAttackDef* atk = player_->GetActiveMeleeAttack();
    const float rangeMult = (atk != nullptr) ? atk->rangeMult : 1.0f;
    const float meleeReach = weapon.range * rangeMult;
    const float dirX = player_->GetLastDirX();
    // 前方に厚く、背後は振り抜きぶんだけ（左右対称だと背後の遠い敵にまで当たってしまう）
    AABB meleeRange = SceneShared::MakeDirectionalRange(pp, dirX, meleeReach, meleeReach * kSkillRearReachMult);
    // ロック中は判定を広げてロックしたのに届かないを減らす（距離無制限ヒットはやめる）
    AABB assistRange = SceneShared::MakeDirectionalRange(pp, dirX, meleeReach * kLockAssistReachMult, meleeReach * kLockAssistRearMult);
    for (size_t di = 0; di < dummies_.size(); ++di) {
        auto& d = dummies_[di];
        if (d.hp <= 0.0f) {
            continue;
        }
        bool isLocked = (lockedKind_ == LockTargetKind::Dummy && lockedDummyIndex_ == di);
        bool hit = Collision::CheckCollision(isLocked ? assistRange : meleeRange, DummyBounds(d));
        if (hit && atk != nullptr) {
            hitConfirmed = true;
            ApplyMeleeHitToDummy(d, atk, atkMult);
        }
    }
    if (hitConfirmed) {
        player_->ChargeAwakenGauge(0.08f);
    }
    return hitConfirmed;
}

bool BattleTestScene::UpdateWeaponSkillHits()
{
    // SPACE固有技の攻撃判定と演出を武器ごとの定義から適用する
    // 発生条件・射程係数・判定形状は kWeaponSkills テーブルが持つ
    const WeaponData& weapon = weaponManager_->GetCurrent();
    const Vector3& pp = player_->GetPosition();
    const float atkMult = ComputeAttackMult();

    bool hitConfirmed = false;
    for (const auto& skill : kWeaponSkills) {
        if (!((*player_).*skill.justTriggered)()) {
            continue;
        }
        const float reach = weapon.range * skill.reachMult;
        // 通常は前方に厚い指向性判定、設置型AoEのみ前後対称に叩きつける
        const AABB skillRange = skill.symmetricAoE
            ? AABB { { pp.x - reach, pp.y - kSlamRangeBelowY, -kStageHalfDepth },
                  { pp.x + reach, pp.y + kSlamRangeAboveY, kStageHalfDepth } }
            : SceneShared::MakeDirectionalRange(pp, player_->GetLastDirX(), reach, reach * kSkillRearReachMult);
        for (auto& d : dummies_) {
            if (d.hp <= 0.0f) {
                continue;
            }
            if (Collision::CheckCollision(skillRange, DummyBounds(d))) {
                hitConfirmed = true;
                ApplyMeleeHitToDummy(d, skill.def, atkMult);
            }
        }
        if (skill.screenImpact) {
            TimeManager::GetInstance()->RequestHitStop(GameConstants::kHitStopLaunch);
            ScreenFlash::GetInstance()->Request(kSlamFlashColor, kSlamFlashDuration);
        }
    }
    return hitConfirmed;
}

bool BattleTestScene::UpdateGunShotHit()
{
    // 射撃コンボ（K キー）
    // 発砲はボタン押下の瞬間ではなく、段の shotTime で発生する（GunComboController 管理）。
    // 弾数・射程倍率・ノックバック・打ち上げ・ヒットストップは全て銃種別の GunShotDef が持つ
    if (!player_->JustFired()) {
        return false;
    }

    auto* tm = TimeManager::GetInstance();
    const Vector3& pp = player_->GetPosition();
    const float atkMult = ComputeAttackMult();

    bool hitConfirmed = false;
    const GunShotDef* shot = player_->GetActiveGunShot();
    const RangedWeaponData& gun = weaponManager_->GetRanged();
    const float rangeX = gun.range * ((shot != nullptr) ? shot->rangeMult : 1.0f);
    // 銃口の向きにだけ飛ぶ（背後は銃身ぶんの余裕のみ）
    AABB shotRange = SceneShared::MakeDirectionalRange(pp, player_->GetLastDirX(), rangeX, 0.8f);
    for (auto& d : dummies_) {
        if (d.hp <= 0.0f) {
            continue;
        }
        if (shot != nullptr && Collision::CheckCollision(shotRange, DummyBounds(d))) {
            hitConfirmed = true;
            d.hp = d.maxHp;
            d.hitFlash = 0.10f;
            d.hpDisplay = 0.0f;
            d.returnTimer = 1.5f;
            d.knockVelX += player_->GetLastDirX() * shot->knockX * atkMult;
            d.knockVelY += shot->knockY * atkMult;
            SpawnHitEffect({ d.pos.x, d.pos.y + 0.5f, 0.0f });
            tm->RequestHitStop(shot->launcher ? GameConstants::kHitStopLaunch : shot->hitStop);
            styleMeter_.RegisterHit(shot->id, gun.damage * shot->damageMult * atkMult);
        }
    }
    if (hitConfirmed) {
        player_->ChargeAwakenGauge(0.04f);
    }
    // マズルフラッシュ: 段の弾数ぶん扇状にばらまく（ダメージは上のヒットスキャンが担当。
    // BulletPool の弾はダミーに当たると二重ヒットになるため、射撃コンボの弾道は視覚専用のパーティクルにする）
    if (shot != nullptr) {
        const float dir = player_->GetLastDirX();
        const Vector3 firePos = { pp.x, pp.y, 0.0f }; // 銃口高さ＝手の高さ付近（頭から出ているように見えないよう低めに）
        const Vector4 col = { gun.color[0], gun.color[1], gun.color[2], gun.color[3] };
        const int n = (std::max)(shot->bullets, 2);
        for (int i = 0; i < n; ++i) {
            float t = (n > 1) ? (i / (n - 1.0f) - 0.5f) : 0.0f; // -0.5〜+0.5
            float speed = kMuzzleBaseSpeed + i * kMuzzleSpeedStep;
            pm_->EmitWithColor("bt_gun_shot", firePos,
                { dir * speed, speed * shot->spreadDeg * GameConstants::kDegToRad * t, 0.0f },
                col, kMuzzleLifeTime, kMuzzleScale);
        }
    }
    return hitConfirmed;
}

bool BattleTestScene::UpdateRampageHit()
{
    // ── 覚醒乱舞ヒット ───────────────────────────────────────────────
    if (!player_->JustRampageHit()) {
        return false;
    }

    auto* tm = TimeManager::GetInstance();
    const WeaponData& weapon = weaponManager_->GetCurrent();
    const Vector3& pp = player_->GetPosition();
    const float atkMult = ComputeAttackMult();

    bool hitConfirmed = false;
    const bool isFinisher = player_->JustRampageFinish();
    AABB rushRange = {
        { pp.x - 2.5f, pp.y - 1.5f, -0.5f },
        { pp.x + 2.5f, pp.y + 1.5f, 0.5f }
    };
    for (auto& d : dummies_) {
        if (Collision::CheckCollision(rushRange, DummyBounds(d))) {
            hitConfirmed = true;
            d.hp = d.maxHp;
            d.hitFlash = isFinisher ? 0.20f : 0.08f;
            d.hpDisplay = 0.0f;
            d.returnTimer = 1.5f;
            float kb = (isFinisher ? 0.45f : 0.12f) * weapon.knockbackMult;
            d.knockVelX += player_->GetLastDirX() * kb * atkMult;
            d.knockVelY += (isFinisher ? 0.18f : 0.03f) * atkMult * weapon.knockbackMult;
            SpawnHitEffect({ d.pos.x, d.pos.y + 0.5f, 0.0f });
            tm->RequestHitStop(isFinisher ? 6 : 2);
            styleMeter_.RegisterHit("rampage", isFinisher ? 60.0f : 20.0f);
        }
    }
    return hitConfirmed;
}

// ══════════════════════════════════════════════════════
// フィニッシャー演出
// ══════════════════════════════════════════════════════

void BattleTestScene::TriggerFinisherSlash()
{
    // ── フィニッシャースラッシュ 発動の合図（斬撃線の表示は UpdateFinisherSlash に委譲）──
    if (!player_->JustFinisherSlash()) {
        return;
    }

    auto* tm = TimeManager::GetInstance();
    const Vector3& pp = player_->GetPosition();

    finisherActive_ = true;
    finisherLineIdx_ = 0;
    finisherBeatTimer_ = GameConstants::kFinisherChargeDelay;
    tm->RequestHitStop(GameConstants::kHitStopJuggle);
    ScreenFlash::GetInstance()->Request({ 0.75f, 0.95f, 1.0f, 0.35f }, 0.10f);
    SpawnHitEffect({ pp.x, pp.y + 0.5f, 0.0f });
    SceneShared::EmitFinisherCharge(pm_, "bt_hit_ring", "bt_hit_spark",
        { pp.x, pp.y + 0.5f, 0.0f });
    spaceWarp_.AddImpulse(0.4f);
}

void BattleTestScene::UpdateSpinShotFire()
{
    // ── スペースキー スピン連射 ──────────────────────────────────────
    if (!player_->JustSpinShot()) {
        return;
    }

    auto* tm = TimeManager::GetInstance();
    const Vector3& pp = player_->GetPosition();
    constexpr float kBulletSpeed = 0.30f;
    Vector3 firePos = { pp.x, pp.y, 0.0f };

    if (player_->IsUpsideDown()) {
        // 逆さ: 下方向中心に 5 方向ばらまき
        constexpr float kBaseAngle = 270.0f * (3.14159265f / 180.0f); // 真下
        constexpr float kSpread = 30.0f * (3.14159265f / 180.0f); // 30°間隔
        for (int i = -2; i <= 2; ++i) {
            float angle = kBaseAngle + i * kSpread;
            bulletPool_.Spawn(firePos, { std::cos(angle) * kBulletSpeed, std::sin(angle) * kBulletSpeed, 0.0f });
        }
        tm->RequestHitStop(3);
        ScreenFlash::GetInstance()->Request({ 1.0f, 0.7f, 0.1f, 0.55f }, 0.10f);
    } else {
        // 通常: 向いている方向に 1 発
        bulletPool_.Spawn(firePos, { player_->GetLastDirX() * kBulletSpeed, 0.0f, 0.0f });
    }
}

bool BattleTestScene::UpdateBulletHits()
{
    // ── 弾丸の移動・衝突判定 ────────────────────────────────────────
    auto* tm = TimeManager::GetInstance();
    const WeaponData& weapon = weaponManager_->GetCurrent();

    bool hitConfirmed = false;
    bulletPool_.Update();
    for (int bi = 0; bi < BulletPool::kMaxBullets; ++bi) {
        if (!bulletPool_.IsActive(bi)) {
            continue;
        }
        const Vector3& bpos = bulletPool_.GetPos(bi);
        const Vector3& bvel = bulletPool_.GetVel(bi);
        AABB bulletAABB = { { bpos.x - 0.12f, bpos.y - 0.12f, -0.5f },
            { bpos.x + 0.12f, bpos.y + 0.12f, 0.5f } };
        for (auto& d : dummies_) {
            if (d.hp <= 0.0f) {
                continue;
            }
            if (Collision::CheckCollision(bulletAABB, DummyBounds(d))) {
                hitConfirmed = true;
                d.hp = d.maxHp;
                d.hitFlash = 0.08f;
                d.hpDisplay = 0.0f;
                d.returnTimer = 1.5f;
                float bspd = std::sqrt(bvel.x * bvel.x + bvel.y * bvel.y);
                if (bspd > 0.001f) {
                    d.knockVelX += bvel.x / bspd * 0.09f * weapon.knockbackMult;
                    d.knockVelY += bvel.y / bspd * 0.04f * weapon.knockbackMult;
                }
                SpawnHitEffect({ d.pos.x, d.pos.y + 0.5f, 0.0f });
                tm->RequestHitStop(2);
                styleMeter_.RegisterHit("spin_bullet", 3.0f);
                player_->ChargeAwakenGauge(0.02f);
                bulletPool_.Kill(bi);
                break;
            }
        }
    }
    return hitConfirmed;
}

bool BattleTestScene::UpdateCombat()
{
    bool hitConfirmed = false;
    hitConfirmed |= UpdateMeleeComboHit();
    hitConfirmed |= UpdateWeaponSkillHits();
    hitConfirmed |= UpdateGunShotHit();
    hitConfirmed |= UpdateRampageHit();
    TriggerFinisherSlash();
    UpdateSpinShotFire();
    hitConfirmed |= UpdateBulletHits();
    return hitConfirmed;
}

bool BattleTestScene::UpdateFinisherSlash()
{
    if (!finisherActive_) {
        return false;
    }

    finisherBeatTimer_ -= GameConstants::kFrameDeltaTime;
    if (finisherBeatTimer_ > 0.0f) {
        return false;
    }

    if (finisherLineIdx_ < GameConstants::kFinisherSlashLines) {
        UpdateFinisherSlashLine();
        return true;
    }

    // 解放 溜めた斬撃が一斉に炸裂し、距離を問わず全マネキンに命中
    finisherActive_ = false;
    ApplyFinisherReleaseHits();
    PlayFinisherReleaseEffects();
    StartFinisherShatterImpact();
    return true;
}

void BattleTestScene::UpdateFinisherSlashLine()
{
    auto* tm = TimeManager::GetInstance();
    const Vector3& pp = player_->GetPosition();

    // カメラ視界全体にランダムな位置を高速で斬り刻む
    const Vector3& cam = camera_->GetTranslate();
    static std::mt19937 rng { std::random_device { }() };
    std::uniform_real_distribution<float> angleDist(0.0f, GameConstants::kTwoPi);
    std::uniform_real_distribution<float> offXDist(-GameConstants::kCameraHalfW, GameConstants::kCameraHalfW);
    std::uniform_real_distribution<float> offYDist(-GameConstants::kCameraHalfH, GameConstants::kCameraHalfH);
    std::uniform_real_distribution<float> lenDist(4.0f, 9.0f);
    std::uniform_real_distribution<float> thickDist(3.0f, 7.0f);
    const float ang = angleDist(rng);
    const Vector2 dir = { std::cos(ang), std::sin(ang) };
    const Vector2 center = { cam.x + offXDist(rng), cam.y + offYDist(rng) };
    const float len = lenDist(rng);

    // 解放の瞬間まで全ての斬撃線を画面に残す
    const float duration = (GameConstants::kFinisherSlashLines - 1 - finisherLineIdx_) * GameConstants::kFinisherLineInterval
        + GameConstants::kFinisherImpactDelay + 0.25f;
    SceneShared::SpawnSlashMarkWorld(
        { center.x - dir.x * len, center.y - dir.y * len },
        { center.x + dir.x * len, center.y + dir.y * len },
        cam.x, cam.y, { 0.75f, 0.95f, 1.0f, 1.0f }, thickDist(rng), duration);

    SceneShared::EmitFinisherSlashLine(pm_, "bt_sword_slash", "bt_hit_spark",
        { center.x, center.y, 0.0f }, ang, len);

    // 空間にガラス質の刃を明滅させ、歪みを脈動させる
    bladeFlash_.Emit({ center.x, center.y, 0.0f }, 3, 4.0f, 1.2f, 2.8f);
    spaceWarp_.AddImpulse(0.12f);

    tm->RequestHitStop(GameConstants::kHitStopFinisherBeat);

    // 斬撃線が出るたびに実際にヒットさせ、マネキンを浮かせ続ける
    for (auto& d : dummies_) {
        d.hp = d.maxHp;
        d.hitFlash = 0.10f;
        d.hpDisplay = 0.0f;
        d.returnTimer = 1.5f;
        d.knockVelX += ((d.pos.x >= pp.x) ? 1.0f : -1.0f) * 0.06f;
        d.knockVelY += 0.06f;
        SpawnHitEffect({ d.pos.x, d.pos.y + 0.5f, 0.0f });
    }

    styleMeter_.RegisterHit("finisher_line", 6.0f);

    finisherLineIdx_++;
    finisherBeatTimer_ = (finisherLineIdx_ < GameConstants::kFinisherSlashLines)
        ? GameConstants::kFinisherLineInterval
        : GameConstants::kFinisherImpactDelay;
}

void BattleTestScene::ApplyFinisherReleaseHits()
{
    const Vector3& pp = player_->GetPosition();
    for (auto& d : dummies_) {
        d.hp = d.maxHp;
        d.hitFlash = 0.22f;
        d.hpDisplay = 0.0f;
        d.returnTimer = 1.5f;
        d.knockVelX += ((d.pos.x >= pp.x) ? 1.0f : -1.0f) * 0.5f;
        d.knockVelY += 0.20f;
        SpawnHitEffect({ d.pos.x, d.pos.y + 0.5f, 0.0f });
    }
}

void BattleTestScene::PlayFinisherReleaseEffects()
{
    auto* tm = TimeManager::GetInstance();
    const Vector3& pp = player_->GetPosition();

    // 溜めた斬撃線を一斉に白く光らせてから消し、太く短い閃光の斬撃線を重ねる
    SlashMark::GetInstance()->FlashAll({ 1.0f, 1.0f, 1.0f, 1.0f }, 0.22f);
    static std::mt19937 rngRelease { std::random_device { }() };
    std::uniform_real_distribution<float> angleDist(0.0f, GameConstants::kTwoPi);
    const Vector3& cam = camera_->GetTranslate();
    for (int i = 0; i < 8; ++i) {
        const float ang = angleDist(rngRelease);
        const Vector2 dir = { std::cos(ang), std::sin(ang) };
        SceneShared::SpawnSlashMarkWorld(
            { pp.x - dir.x * GameConstants::kFinisherSlashRadius,
                pp.y - dir.y * GameConstants::kFinisherSlashRadius },
            { pp.x + dir.x * GameConstants::kFinisherSlashRadius,
                pp.y + dir.y * GameConstants::kFinisherSlashRadius },
            cam.x, cam.y, { 1.0f, 1.0f, 1.0f, 1.0f }, 9.0f, 0.15f);
    }

    tm->RequestHitStop(GameConstants::kHitStopFinisherSlash);
    ScreenFlash::GetInstance()->Request({ 0.75f, 0.95f, 1.0f, 0.65f }, GameConstants::kShakeFinisherSlashDur);
    SceneShared::EmitFinisherRelease(pm_, "bt_hit_ring", "bt_hit_spark",
        { pp.x, pp.y + 0.5f, 0.0f });
    styleMeter_.RegisterHit("finisher_release", 120.0f);

    // 解放の瞬間 刃の一斉放出と空間歪みの最大化、最も近いダミーを切断破片に差し替える
    bladeFlash_.Emit({ pp.x, pp.y + 0.5f, 0.0f }, 30, GameConstants::kFinisherSlashRadius, 2.0f, 5.0f);
    spaceWarp_.AddImpulse(1.0f);

    Dummy* nearest = nullptr;
    float minDist = FLT_MAX;
    for (auto& d : dummies_) {
        float dist = std::abs(d.pos.x - pp.x);
        if (dist < minDist) {
            minDist = dist;
            nearest = &d;
        }
    }
    if (nearest != nullptr) {
        static std::mt19937 rngSlice { std::random_device { }() };
        dummySlice_.Start(modelDummy_.get(), nearest->pos, { 1.0f, 1.0f, 1.0f }, rngSlice());
        nearest->sliced = true;
    }
}

void BattleTestScene::StartFinisherShatterImpact()
{
    // 暗転+斬撃線ごと凍った画面をプレイヤー位置から砕き、素の世界を見せる
    const Vector3& pp = player_->GetPosition();
    const Matrix4x4 vp = Multiply(camera_->GetViewMatrix(), camera_->GetProjectionMatrix());
    const float cx = pp.x * vp.m[0][0] + pp.y * vp.m[1][0] + pp.z * vp.m[2][0] + vp.m[3][0];
    const float cy = pp.x * vp.m[0][1] + pp.y * vp.m[1][1] + pp.z * vp.m[2][1] + vp.m[3][1];
    const float cw = pp.x * vp.m[0][3] + pp.y * vp.m[1][3] + pp.z * vp.m[2][3] + vp.m[3][3];
    if (cw > 0.0001f) {
        finisherShatter_.SetImpactUV(cx / cw * 0.5f + 0.5f, 0.5f - cy / cw * 0.5f);
    }

    finisherShatter_.Reset();
    finisherShatter_.Start();
}

void BattleTestScene::ApplyMeleeHitToDummy(Dummy& d, const MeleeAttackDef* atk, float atkMult)
{
    auto* tm = TimeManager::GetInstance();
    const WeaponData& weapon = weaponManager_->GetCurrent();

    d.hp = d.maxHp;
    d.hitFlash = atk->launcher ? 0.20f : 0.14f;
    d.hpDisplay = 0.0f;
    d.returnTimer = 1.5f;

    // ノックバックは段の定義 × 武器の重さ × 覚醒倍率
    const float kb = weapon.knockbackMult * atkMult;
    d.knockVelX += player_->GetLastDirX() * atk->knockX * kb;
    d.knockVelY += atk->knockY * kb;

    const Vector3 hitPosition = { d.pos.x, d.pos.y + 0.5f, 0.0f };
    SpawnHitEffect(hitPosition);

    // 本編と同じ属性プリセットを使い、テストシーンで色と密度を調整できるようにする
    const Vector4 effectColor = { weapon.effectColor[0], weapon.effectColor[1],
        weapon.effectColor[2], weapon.effectColor[3] };
    for (int i = 0; i < weapon.effectBurstCount; ++i) {
        const float side = static_cast<float>((i % 5) - 2) * 0.5f;
        pm_->EmitGravity("bt_hit_spark", hitPosition,
            { player_->GetLastDirX() * (2.0f + i * 0.25f), 2.0f + (i % 4) * 0.7f, side },
            effectColor, 0.5f, 0.16f);
    }
    if (weapon.effectRingRadius > 0.0f) {
        pm_->EmitRing("bt_hit_ring", hitPosition, weapon.effectRingRadius,
            effectColor, 12 + weapon.effectBurstCount, 0.28f, 0.16f);
    }

    if (atk->launcher) {
        // 打ち上げ: 長めのヒットストップ + 画面フラッシュで浮かせた手応えを出す
        tm->RequestHitStop(GameConstants::kHitStopLaunch);
        ScreenFlash::GetInstance()->Request({ 1.0f, 0.95f, 0.7f, 0.25f }, 0.08f);
    } else {
        tm->RequestHitStop(atk->hitStop);
    }

    // スタイル加点はおおよそ与ダメージに比例（同じ技の連発は StyleMeter 側で減衰する）
    styleMeter_.RegisterHit(atk->id, weapon.damage * atk->damageMult * 1.5f * atkMult);
}

// ══════════════════════════════════════════════════════
// 敵とターゲットの更新
// ══════════════════════════════════════════════════════

void BattleTestScene::UpdateDummies()
{
    for (auto& d : dummies_) {
        // ノックバック物理
        d.knockVelY -= 0.012f;
        d.pos.x += d.knockVelX;
        d.pos.y += d.knockVelY;

        if (d.pos.y <= 0.4f) {
            d.pos.y = 0.4f;
            d.knockVelY = 0.0f;
        }
        d.pos.x = std::clamp(d.pos.x, 3.0f, 35.0f);
        if (d.pos.x <= 3.01f || d.pos.x >= 34.99f) {
            d.knockVelX = 0.0f;
        }
        d.knockVelX *= 0.84f;
        d.knockVelY *= 0.88f;

        // HP バー表示値を回復（被弾後 0.8 秒で満タンに戻る）
        d.hpDisplay = (std::min)(d.hpDisplay + GameConstants::kFrameDeltaTime / 0.8f, 1.0f);

        // 帰還タイマー（被弾から 1.5 秒後に中央へ戻る）
        d.returnTimer -= GameConstants::kFrameDeltaTime;
        if (d.returnTimer <= 0.0f) {
            d.returnTimer = 0.0f;
            d.pos.x += (d.homePos.x - d.pos.x) * 0.05f;
            d.pos.y += (d.homePos.y - d.pos.y) * 0.05f;
        }

        d.object->SetPosition(d.pos);

        d.hitFlash -= GameConstants::kFrameDeltaTime;
        if (d.hitFlash > 0) {
            d.object->SetColor({ 1.5f, 1.5f, 1.5f, 1.0f });
        } else {
            d.object->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
        }
        d.object->Update();
    }

    UpdateHpBars();
}

void BattleTestScene::UpdateTargetLock()
{
    // ロック中の対象が無効になっていたら（撃破された等）自動解除
    if (lockedKind_ == LockTargetKind::Knight && (!knight_ || !knight_->IsAlive())) {
        lockedKind_ = LockTargetKind::None;
    }
    if (lockedKind_ == LockTargetKind::Dummy && lockedDummyIndex_ >= dummies_.size()) {
        lockedKind_ = LockTargetKind::None;
    }

    if (!input_->TriggerKey(DIK_LSHIFT)) {
        return;
    }

    // 候補: 生存中のダミー → ナイト（生存時のみ）→ 末尾はロック解除として巡回する
    struct Candidate {
        LockTargetKind kind;
        size_t index;
    };
    std::vector<Candidate> candidates;
    for (size_t i = 0; i < dummies_.size(); ++i) {
        candidates.push_back({ LockTargetKind::Dummy, i });
    }
    if (knight_ && knight_->IsAlive()) {
        candidates.push_back({ LockTargetKind::Knight, 0 });
    }
    if (candidates.empty()) {
        lockedKind_ = LockTargetKind::None;
        return;
    }

    int curIdx = -1;
    for (size_t i = 0; i < candidates.size(); ++i) {
        bool sameKind = (candidates[i].kind == lockedKind_);
        bool sameSlot = (lockedKind_ != LockTargetKind::Dummy) || (candidates[i].index == lockedDummyIndex_);
        if (sameKind && sameSlot) {
            curIdx = static_cast<int>(i);
            break;
        }
    }

    int nextIdx = curIdx + 1; // 未ロック(-1)からは先頭へ、最後まで進んだら解除に戻る
    if (nextIdx >= static_cast<int>(candidates.size())) {
        lockedKind_ = LockTargetKind::None;
    } else {
        lockedKind_ = candidates[nextIdx].kind;
        lockedDummyIndex_ = candidates[nextIdx].index;
    }
}

void BattleTestScene::UpdateKnightEnemy()
{
    if (!knight_) {
        return;
    }

    auto* tm = TimeManager::GetInstance();
    const WeaponData& weapon = weaponManager_->GetCurrent();
    const Vector3& pp = player_->GetPosition();

    knight_->Update(pm_, pp);
    // ステージエディタでsolidにしたブロックとの当たり判定（追加/移動/削除が次フレームからそのまま反映される）
    knight_->ResolveBlockCollision(GetStageEditor().GetSolidColliders());

    // ── プレイヤーの攻撃判定（Dummy 用と同じ AABB をナイトにも適用） ──
    if (knight_->IsAlive()) {
        if (player_->JustComboHit()) {
            const MeleeAttackDef* atk = player_->GetActiveMeleeAttack();
            const float rangeMult = (atk != nullptr) ? atk->rangeMult : 1.0f;
            const float meleeReach = weapon.range * rangeMult;
            const float dirX = player_->GetLastDirX();
            // ロック中は判定を1.6倍まで広げる（ダミー側と同じ基準、距離無制限ヒットはやめる）
            bool isLocked = (lockedKind_ == LockTargetKind::Knight);
            AABB meleeRange = isLocked
                ? SceneShared::MakeDirectionalRange(pp, dirX, meleeReach * 1.6f, meleeReach * 0.6f)
                : SceneShared::MakeDirectionalRange(pp, dirX, meleeReach, meleeReach * 0.4f);
            bool hit = Collision::CheckCollision(meleeRange, knight_->GetAABB());
            if (hit && atk != nullptr) {
                knight_->TakeDamage(1, player_->GetLastDirX(), atk->knockY);
                SpawnHitEffect({ knight_->GetPosition().x, knight_->GetPosition().y + 0.7f, 0.0f });
                tm->RequestHitStop(atk->launcher ? GameConstants::kHitStopLaunch : atk->hitStop);
                styleMeter_.RegisterHit(atk->id, weapon.damage * atk->damageMult * 1.5f);
                player_->ChargeAwakenGauge(0.08f);
            }
        }
        if (player_->JustFired()) {
            const GunShotDef* shot = player_->GetActiveGunShot();
            const RangedWeaponData& gun = weaponManager_->GetRanged();
            if (shot != nullptr) {
                const float rangeX = gun.range * shot->rangeMult;
                // 銃口の向きにだけ飛ぶ（ロック中でも射程・向きは無視しない）
                AABB shotRange = SceneShared::MakeDirectionalRange(pp, player_->GetLastDirX(), rangeX, 0.8f);
                if (Collision::CheckCollision(shotRange, knight_->GetAABB())) {
                    knight_->TakeDamage(1, player_->GetLastDirX(), shot->knockY);
                    SpawnHitEffect({ knight_->GetPosition().x, knight_->GetPosition().y + 0.7f, 0.0f });
                    tm->RequestHitStop(shot->launcher ? GameConstants::kHitStopLaunch : shot->hitStop);
                    styleMeter_.RegisterHit(shot->id, gun.damage * shot->damageMult);
                    player_->ChargeAwakenGauge(0.04f);
                }
            }
        }
        if (player_->JustRampageHit()) {
            AABB rushRange = {
                { pp.x - 2.5f, pp.y - 1.5f, -0.5f },
                { pp.x + 2.5f, pp.y + 1.5f, 0.5f }
            };
            if (Collision::CheckCollision(rushRange, knight_->GetAABB())) {
                bool isFinisher = player_->JustRampageFinish();
                float knockY = (isFinisher ? 0.18f : 0.03f) * weapon.knockbackMult;
                knight_->TakeDamage(1, player_->GetLastDirX(), knockY);
                SpawnHitEffect({ knight_->GetPosition().x, knight_->GetPosition().y + 0.7f, 0.0f });
                tm->RequestHitStop(isFinisher ? 6 : 2);
                styleMeter_.RegisterHit("rampage", player_->JustRampageFinish() ? 60.0f : 20.0f);
            }
        }
    }

    // ── 撃破後: 近づいて J キーで刺し、武器を奪う ──────────────────
    if (knight_->IsAwaitingSteal()) {
        float dx = knight_->GetPosition().x - pp.x;
        float dy = knight_->GetPosition().y - pp.y;
        constexpr float kStealRange = 2.0f;
        if ((dx * dx + dy * dy) <= kStealRange * kStealRange && input_->TriggerKey(DIK_J)) {
            if (knight_->TryBeginAbsorb()) {
                player_->PlayStealStab();
                tm->RequestHitStop(8);
                ScreenFlash::GetInstance()->Request({ 0.6f, 0.75f, 0.9f, 0.4f }, 0.10f);
            }
        }
    }

    if (knight_->JustAbsorbed()) {
        // ナイトの得物(Sword.obj)に合わせてソードスタイルを解放（重複時は false=強化素材扱い、今は未実装）
        weaponManager_->Unlock(WeaponType::Sword);
        pm_->EmitRing("bt_hit_ring", { pp.x, pp.y + 0.5f, 0.0f }, 0.10f,
            { 0.5f, 0.85f, 1.0f, 1.0f }, 20, 0.35f, 0.4f);
        slotFlashTimer_ = kSlotFlashDuration;
    }
}

void BattleTestScene::UpdatePlacedKnights()
{
    // AI/重力自体はBaseScene::Tick()がUpdate()の後にGetStageEditor().UpdateObjects()で回す
    // （1フレーム遅れで前フレームの位置に対して判定する形になるが60fpsなら誤差程度）
    // ここでは当たり判定・ダメージ処理だけを行う
    std::vector<KnightEnemy*> knights = GetStageEditor().GetKnights();
    if (knights.empty()) {
        return;
    }

    auto* tm = TimeManager::GetInstance();
    const WeaponData& weapon = weaponManager_->GetCurrent();
    const Vector3& pp = player_->GetPosition();
    std::vector<AABB> solids = GetStageEditor().GetSolidColliders();

    for (KnightEnemy* knight : knights) {
        knight->ResolveBlockCollision(solids);
        if (!knight->IsAlive()) {
            continue;
        }

        if (player_->JustComboHit()) {
            const MeleeAttackDef* atk = player_->GetActiveMeleeAttack();
            if (atk != nullptr) {
                const float meleeReach = weapon.range * atk->rangeMult;
                const float dirX = player_->GetLastDirX();
                AABB meleeRange = SceneShared::MakeDirectionalRange(pp, dirX, meleeReach, meleeReach * 0.4f);
                if (Collision::CheckCollision(meleeRange, knight->GetAABB())) {
                    knight->TakeDamage(1, dirX, atk->knockY);
                    SpawnHitEffect({ knight->GetPosition().x, knight->GetPosition().y + 0.7f, 0.0f });
                    tm->RequestHitStop(atk->launcher ? GameConstants::kHitStopLaunch : atk->hitStop);
                    styleMeter_.RegisterHit(atk->id, weapon.damage * atk->damageMult * 1.5f);
                    player_->ChargeAwakenGauge(0.08f);
                }
            }
        }
        if (player_->JustFired()) {
            const GunShotDef* shot = player_->GetActiveGunShot();
            const RangedWeaponData& gun = weaponManager_->GetRanged();
            if (shot != nullptr) {
                const float rangeX = gun.range * shot->rangeMult;
                AABB shotRange = SceneShared::MakeDirectionalRange(pp, player_->GetLastDirX(), rangeX, 0.8f);
                if (Collision::CheckCollision(shotRange, knight->GetAABB())) {
                    knight->TakeDamage(1, player_->GetLastDirX(), shot->knockY);
                    SpawnHitEffect({ knight->GetPosition().x, knight->GetPosition().y + 0.7f, 0.0f });
                    tm->RequestHitStop(shot->launcher ? GameConstants::kHitStopLaunch : shot->hitStop);
                    styleMeter_.RegisterHit(shot->id, gun.damage * shot->damageMult);
                    player_->ChargeAwakenGauge(0.04f);
                }
            }
        }
        if (player_->JustRampageHit()) {
            AABB rushRange = {
                { pp.x - 2.5f, pp.y - 1.5f, -0.5f },
                { pp.x + 2.5f, pp.y + 1.5f, 0.5f }
            };
            if (Collision::CheckCollision(rushRange, knight->GetAABB())) {
                bool isFinisher = player_->JustRampageFinish();
                float knockY = (isFinisher ? 0.18f : 0.03f) * weapon.knockbackMult;
                knight->TakeDamage(1, player_->GetLastDirX(), knockY);
                SpawnHitEffect({ knight->GetPosition().x, knight->GetPosition().y + 0.7f, 0.0f });
                tm->RequestHitStop(isFinisher ? 6 : 2);
                styleMeter_.RegisterHit("rampage", isFinisher ? 60.0f : 20.0f);
            }
        }
    }
}

// ══════════════════════════════════════════════════════
// HUD更新と描画
// ══════════════════════════════════════════════════════

void BattleTestScene::InitializeWeaponSlotHud()
{
    constexpr float kSlotSize = 56.0f;
    constexpr float kSlotGap = 10.0f;
    constexpr float kMarginX = 24.0f;
    constexpr float kMarginY = 90.0f; // 画面下端からの距離
    const float baseY = static_cast<float>(WinApp::kClientHeight) - kMarginY;

    const auto& list = weaponManager_->GetList();
    for (int i = 0; i < kWeaponSlotCount; ++i) {
        float x = kMarginX + static_cast<float>(i) * (kSlotSize + kSlotGap);
        weaponSlotPos_[i] = { x, baseY };

        auto frame = std::make_unique<Sprite>();
        frame->Initialize(spriteCommon_.get(), "Resources/white.png");
        frame->SetPosition({ x, baseY });
        frame->SetSize({ kSlotSize, kSlotSize });
        frame->SetColor({ 0.08f, 0.08f, 0.1f, 0.85f });

        auto icon = std::make_unique<Sprite>();
        icon->Initialize(spriteCommon_.get(), "Resources/white.png");
        icon->SetPosition({ x + 6.0f, baseY + 6.0f });
        icon->SetSize({ kSlotSize - 12.0f, kSlotSize - 12.0f });
        if (i < static_cast<int>(list.size()) && weaponManager_->IsUnlocked(i)) {
            const float* c = list[i].styleColor;
            icon->SetColor({ c[0], c[1], c[2], 0.9f });
        } else {
            icon->SetColor({ 0.2f, 0.2f, 0.2f, 0.35f }); // 未解放スロット
        }

        weaponSlots_[i].frame = std::move(frame);
        weaponSlots_[i].icon = std::move(icon);
    }

    // 各スタイルに対応する実物3Dモデル（色塗り四角の代わりに表示）
    // ダミーの物理武器がまだ無いスタイルはここに追加すれば自動でモデル表示に切り替わる
    // scale はモデル実寸の高さ差を吸収し、見た目のアイコンサイズ(目標高さ約0.8)を揃えるための倍率
    // baseYaw はモデルの正面がカメラを向くよう回す基準角度（ラジアン）。目視で合わせる必要がある
    struct IconAsset {
        WeaponType type;
        const char* modelPath;
        const char* texturePath;
        float scale;
        float baseYaw;
    };
    static constexpr IconAsset kIconAssets[] = {
        { WeaponType::Sword, "Resources/Knight/OBJ/Sword.obj", "Resources/Knight/OBJ/SwordPalette.png", 0.18f, 0.0f }, // 実寸高さ約4.35
        { WeaponType::Dagger, "Resources/MedievalWeaponsPack/OBJ/Dagger.obj", "Resources/MedievalWeaponsPack/OBJ/DaggerPalette.png", 0.31f, 0.0f }, // 実寸高さ約2.60
        { WeaponType::Hammer, "Resources/MedievalWeaponsPack/OBJ/Hammer_Small.obj", "Resources/MedievalWeaponsPack/OBJ/Hammer_SmallPalette.png", 0.18f, GameConstants::kPi }, // 実寸高さ約4.33（正面が逆だったので180度回転）
        { WeaponType::Spear, "Resources/MedievalWeaponsPack/OBJ/Spear.obj", "Resources/MedievalWeaponsPack/OBJ/SpearPalette.png", 0.08f, 0.0f }, // 実寸高さ約9.72
        { WeaponType::Greatsword, "Resources/MedievalWeaponsPack/OBJ/Claymore.obj", "Resources/MedievalWeaponsPack/OBJ/ClaymorePalette.png", 0.12f, 0.0f }, // 実寸高さ約6.59
        { WeaponType::Scythe, "Resources/MedievalWeaponsPack/OBJ/Scythe.obj", "Resources/MedievalWeaponsPack/OBJ/ScythePalette.png", 0.14f, 0.0f }, // 実寸高さ約5.58
        { WeaponType::Axe, "Resources/MedievalWeaponsPack/OBJ/Axe_Double.obj", "Resources/MedievalWeaponsPack/OBJ/Axe_DoublePalette.png", 0.13f, 0.0f }, // 実寸高さ約6.35
    };

    for (int i = 0; i < kWeaponSlotCount && i < static_cast<int>(list.size()); ++i) {
        for (const auto& asset : kIconAssets) {
            if (list[i].type != asset.type) {
                continue;
            }
            auto& icon3d = weaponIcons3D_[i];
            icon3d.slotIndex = i;
            icon3d.scale = asset.scale;
            icon3d.baseYaw = asset.baseYaw;
            icon3d.model = std::make_unique<Model>();
            icon3d.model->Initialize(modelCommon_.get(), asset.modelPath, asset.texturePath);
            icon3d.object = std::make_unique<Object3d>();
            icon3d.object->Initialize(modelCommon_.get());
            icon3d.object->SetModel(icon3d.model.get());
            icon3d.object->SetEnableLighting(true);
            break;
        }
    }

    // 常時装備の拳銃（4スロットの右に少し離して配置、スタイルスロットとは別枠だと分かるように）
    float gunX = kMarginX + static_cast<float>(kWeaponSlotCount) * (kSlotSize + kSlotGap) + 24.0f;
    gunPos_ = { gunX, baseY };

    gunFrame_ = std::make_unique<Sprite>();
    gunFrame_->Initialize(spriteCommon_.get(), "Resources/white.png");
    gunFrame_->SetPosition({ gunX, baseY });
    gunFrame_->SetSize({ kSlotSize, kSlotSize });
    gunFrame_->SetColor({ 0.08f, 0.08f, 0.1f, 0.85f });

    gunIcon_ = std::make_unique<Sprite>();
    gunIcon_->Initialize(spriteCommon_.get(), "Resources/white.png");
    gunIcon_->SetAnchorPoint({ 0.5f, 0.5f });
    gunIcon_->SetPosition({ gunX + kSlotSize * 0.5f, baseY + kSlotSize * 0.5f });
    gunIcon_->SetSize({ kSlotSize - 20.0f, kSlotSize - 20.0f });
    gunIcon_->SetColor({ 0.6f, 0.85f, 1.0f, 0.9f });
}

void BattleTestScene::UpdateWeaponSlotHud()
{
    slotPulseTimer_ += GameConstants::kFrameDeltaTime;
    gunIconAngle_ += GameConstants::kFrameDeltaTime * 0.6f; // 常時装備の印として、ゆっくり回り続ける
    if (slotFlashTimer_ > 0.0f) {
        slotFlashTimer_ = (std::max)(0.0f, slotFlashTimer_ - GameConstants::kFrameDeltaTime);
    }

    const int activeIndex = weaponManager_->GetSelectedSlot();
    const float pulse = 0.7f + 0.3f * std::sin(slotPulseTimer_ * 6.0f);
    const float flash = slotFlashTimer_ / kSlotFlashDuration;

    const auto& list = weaponManager_->GetList();
    for (int i = 0; i < kWeaponSlotCount; ++i) {
        bool active = (i == activeIndex);
        float frameB = 0.08f + (active ? pulse * 0.35f : 0.0f) + flash * 0.5f;
        weaponSlots_[i].frame->SetColor({ frameB, frameB, frameB + (active ? 0.2f : 0.05f), 0.85f });
        weaponSlots_[i].frame->Update();

        const int weaponIndex = weaponManager_->GetSlotWeaponIndex(i);
        bool unlocked = weaponIndex >= 0 && weaponIndex < static_cast<int>(list.size())
            && weaponManager_->IsUnlocked(weaponIndex);
        bool show3DIcon = unlocked && weaponIndex == i
            && (weaponIcons3D_[i].slotIndex == i) && weaponIcons3D_[i].object;
        if (unlocked) {
            float iconMul = (active ? (0.7f + pulse * 0.3f) : 0.5f) + flash * 0.5f;
            const float* c = list[weaponIndex].styleColor;
            // 3Dモデルで表示するスロットは、下地の色四角を隠して実物モデルだけ見せる
            weaponSlots_[i].icon->SetColor({ c[0] * iconMul, c[1] * iconMul, c[2] * iconMul, show3DIcon ? 0.0f : 0.95f });
        } else {
            float lockFlash = 0.2f + flash * 0.6f; // 新規解放の瞬間はロック中のスロットも一緒に光らせる
            weaponSlots_[i].icon->SetColor({ lockFlash, lockFlash, lockFlash, 0.35f + flash * 0.3f });
        }
        weaponSlots_[i].icon->Update();
    }

    gunFrame_->Update();
    gunIcon_->SetRotation(gunIconAngle_);
    gunIcon_->Update();

    // ── 各スロットの3Dアイコン（画面左下に固定表示、ゆっくり回転） ────────
    // カメラは回転しないので、スロットの画面位置をワールド座標へ逆算して張り付ける
    // 元の逆算(Z=0基準)だと地面の境界ブロック(Y=-0.6付近)に埋もれて隠れてしまうため、
    // カメラのすぐ手前(奥行き6)に置き直す。奥行きが変わった分、オフセットとスケールを
    // WorldToScreen の基準距離(24)に対する比率で縮小して同じ画面位置・見た目サイズを保つ
    constexpr float kSlotSize = 56.0f;
    constexpr float kIconDepth = 6.0f; // カメラからの距離
    constexpr float kIconDepthScale = kIconDepth / 24.0f; // WorldToScreen基準距離(24)との比
    for (int i = 0; i < kWeaponSlotCount; ++i) {
        auto& icon3d = weaponIcons3D_[i];
        if (icon3d.slotIndex != i || !icon3d.object) {
            continue;
        }
        if (weaponManager_->GetSlotWeaponIndex(i) != i || !weaponManager_->IsUnlocked(i)) {
            continue;
        }

        float sx = weaponSlotPos_[i].x + kSlotSize * 0.5f;
        float sy = weaponSlotPos_[i].y + kSlotSize * 0.5f;
        const Vector3& cam = camera_->GetTranslate();
        Vector3 iconPos = {
            cam.x + (sx - 640.0f) / 640.0f * GameConstants::kCameraHalfW * kIconDepthScale,
            cam.y - (sy - 360.0f) / 360.0f * GameConstants::kCameraHalfH * kIconDepthScale,
            cam.z + kIconDepth
        };
        float iconScale = icon3d.scale * kIconDepthScale;
        // フルスピンだと必ず背面がカメラを向く瞬間が来て武器が判別できなくなるため、
        // 正面(baseYaw)を中心に小さく揺らすだけにする
        icon3d.wobbleTime += GameConstants::kFrameDeltaTime;
        float yaw = icon3d.baseYaw + std::sin(icon3d.wobbleTime * 0.8f) * 0.35f;
        icon3d.object->SetPosition(iconPos);
        icon3d.object->SetRotation({ 0.3f, yaw, 0.0f });
        icon3d.object->SetScale({ iconScale, iconScale, iconScale });
        bool active = (i == activeIndex);
        float b = (active ? (0.9f + pulse * 0.1f) : 0.6f) + flash * 0.4f;
        icon3d.object->SetColor({ b, b, b, 1.0f });
        icon3d.object->Update();
    }
}

void BattleTestScene::DrawWeaponSlotHud()
{
    // 背景の枠を先に描く（3Dモデルがこの手前に来るようにする）
    for (auto& slot : weaponSlots_) {
        slot.frame->Draw();
    }
    gunFrame_->Draw();

    // 枠の中身 実物3Dモデルのスロットは、いったん3D描画パイプラインに切り替えて
    // 枠より手前に描画する（前は3Dワールドと同じパスで描いていたため、後から描かれる
    // 2Dの枠に覆いかぶさられて背面に隠れてしまっていた）
    {
        ID3D12GraphicsCommandList* cmd = dxCommon_->GetCommandList();
        modelCommon_->CommonDrawSettings();
        Object3d::RebindCommonLighting(cmd);
        for (int i = 0; i < kWeaponSlotCount; ++i) {
            auto& icon3d = weaponIcons3D_[i];
            if (weaponManager_->GetSlotWeaponIndex(i) == i
                && icon3d.slotIndex == i && icon3d.object && weaponManager_->IsUnlocked(i)) {
                icon3d.object->Draw();
            }
        }
        spriteCommon_->CommonDrawSettings(); // 以降のスプライト描画のため2Dへ戻す
    }

    // 色四角のアイコン（3Dモデル未対応のスタイル用。3Dモデル表示中のスロットはアルファ0で透明）
    for (auto& slot : weaponSlots_) {
        slot.icon->Draw();
    }
    gunIcon_->Draw();

    // スタイル名の文字ラベルは廃止（枠の中身＝実物の武器モデル/色で見分ける）。
    // 未解放のスロットだけ?を出し、中身が武器モデルで隠れないよう控えめな位置にする
    constexpr float kSlotSize = 56.0f;
    const auto& list = weaponManager_->GetList();
    for (int i = 0; i < kWeaponSlotCount && i < static_cast<int>(list.size()); ++i) {
        if (weaponManager_->IsUnlocked(i)) {
            continue;
        }
        fontRenderer_.DrawStringW(L"?",
            weaponSlotPos_[i].x + kSlotSize * 0.5f - 6.0f,
            weaponSlotPos_[i].y + kSlotSize * 0.5f - 12.0f, 1.6f,
            { 0.6f, 0.6f, 0.6f, 0.9f });
    }
    fontRenderer_.DrawString("GUN", gunPos_.x + 10.0f, gunPos_.y + kSlotSize + 4.0f, 1.0f,
        { 0.6f, 0.85f, 1.0f, 0.9f });
}

void BattleTestScene::DrawHud(bool nearReturnPortal)
{
    DrawWeaponHud(nearReturnPortal);
    SceneShared::DrawControlsHud(fontRenderer_, L": トレーニングへ戻る");
    styleMeter_.UpdateHud(fontRenderer_); // 右上のスタイリッシュランク
    SceneShared::DrawAwakenGaugeHud(fontRenderer_, awakenGaugeBg_.get(), awakenGaugeFg_.get(),
        player_->GetAwakenGauge(), player_->IsAwakened(), warpPulseTimer_);

    // ── ロックオン中の対象にマーカーを出す ────────────────────────
    if (lockedKind_ != LockTargetKind::None) {
        Vector3 tpos { };
        bool valid = true;
        if (lockedKind_ == LockTargetKind::Knight && knight_) {
            tpos = knight_->GetPosition();
        } else if (lockedKind_ == LockTargetKind::Dummy && lockedDummyIndex_ < dummies_.size()) {
            tpos = dummies_[lockedDummyIndex_].pos;
        } else {
            valid = false;
        }
        if (valid) {
            const Vector3& cam = camera_->GetTranslate();
            float sx, sy;
            SceneShared::WorldToScreen(tpos.x, tpos.y + 1.6f, cam.x, cam.y, sx, sy);
            fontRenderer_.DrawString("v LOCK v", sx - 46.0f, sy, 1.3f, { 1.0f, 0.35f, 0.2f, 1.0f });
        }
    }

    // ── 撃破したナイトの頭上に武器奪取のプロンプトを出す ──────────────
    if (knight_ && knight_->IsAwaitingSteal()) {
        const Vector3& pp = player_->GetPosition();
        const Vector3& kp = knight_->GetPosition();
        float dx = kp.x - pp.x;
        float dy = kp.y - pp.y;
        constexpr float kStealRange = 2.0f;
        if ((dx * dx + dy * dy) <= kStealRange * kStealRange) {
            const Vector3& cam = camera_->GetTranslate();
            float sx, sy;
            SceneShared::WorldToScreen(kp.x, kp.y + 2.2f, cam.x, cam.y, sx, sy);
            fontRenderer_.DrawString("[ J ] Steal Weapon", sx - 90.0f, sy, 1.5f,
                { 0.6f, 0.85f, 1.0f, 1.0f });
        }
    }
}

void BattleTestScene::DrawWeaponHud(bool nearReturnPortal)
{
    constexpr float kScale = 1.5f;
    constexpr Vector4 kColorHint = { 0.6f, 0.6f, 0.6f, 1.0f };

    float py = SceneShared::DrawWeaponListHud(fontRenderer_, weaponManager_, L"テストステージ");
    fontRenderer_.DrawStringW(L"[L] コンボ  [S+L] 打ち上げ  [空中L] 空中コンボ", 12.0f, py, kScale, kColorHint);
    fontRenderer_.DrawStringW(L"[K] 射撃  [R] 覚醒  [Shift] ロックオン切替", 12.0f, py + 24.0f, kScale, kColorHint);

    // 戻りポータルのラベル
    if (nearReturnPortal) {
        const Vector3& cam = camera_->GetTranslate();
        float sx, sy;
        SceneShared::WorldToScreen(kWarpRetX, 5.0f, cam.x, cam.y, sx, sy);
        constexpr Vector4 kColorReturn = { 1.0f, 0.6f, 0.1f, 1.0f };
        fontRenderer_.DrawStringW(L"[ ENTER ] トレーニングへ", sx - 110.0f, sy - 36.0f, kScale, kColorReturn);
    }

    if (showColliders_) {
        fontRenderer_.DrawString("[ F3 ] Colliders: ON", 12.0f, 84.0f, kScale, { 1.0f, 0.5f, 0.1f, 1.0f });
    }
}

void BattleTestScene::DrawColliderOverlay()
{
    DiagnosticsDraw::SetCamera(camera_->GetViewProjectionMatrix(),
        static_cast<float>(WinApp::kClientWidth), static_cast<float>(WinApp::kClientHeight));

    // プレイヤー（緑）
    DiagnosticsDraw::DrawCollider(player_->GetCollider(), DiagnosticsDraw::kColorGreen);

    // 訓練マネキン（生存中のみ、シアン）
    for (const auto& d : dummies_) {
        if (d.hp > 0.0f) {
            DiagnosticsDraw::DrawAABB(DummyBounds(d), DiagnosticsDraw::kColorCyan);
        }
    }

    // ナイト敵（生存中のみ、赤）
    if (knight_ && knight_->IsAlive()) {
        DiagnosticsDraw::DrawAABB(knight_->GetAABB(), DiagnosticsDraw::kColorRed);
    }
    // StageEditorで配置したナイト（生存中のみ、赤）
    for (KnightEnemy* placedKnight : GetStageEditor().GetKnights()) {
        if (placedKnight->IsAlive()) {
            DiagnosticsDraw::DrawAABB(placedKnight->GetAABB(), DiagnosticsDraw::kColorRed);
        }
    }

    // ステージエディタでsolidにしたブロック（オレンジ、エディタを開いていなくても見える）
    for (const auto& b : GetStageEditor().GetSolidColliders()) {
        DiagnosticsDraw::DrawAABB(b, DiagnosticsDraw::kColorOrange);
    }
}

// ══════════════════════════════════════════════════════
// シーン描画と終了処理
// ══════════════════════════════════════════════════════

void BattleTestScene::Draw()
{
    BattleTestSceneRenderer::Draw(*this);
}

void BattleTestScene::Finalize()
{
    ImGuiControlPanel::RegisterGlassShatterTrigger(nullptr);
    pm_->ClearAllGroups();
    glassShatter_.Finalize();
    finisherShatter_.Finalize();
    spaceWarp_.Finalize();
    bladeFlash_.Clear();
    SlashMark::GetInstance()->Clear();
}

void BattleTestScene::TriggerGlassShatterTest()
{
    if (glassShatter_.IsActive()) {
        return;
    }
    glassShatter_.Start();
}

D3D12_CPU_DESCRIPTOR_HANDLE BattleTestScene::GetActiveRTVHandle() const
{
    return SceneShared::GetActiveRTVHandle(dxCommon_, { imageFilter_, grayscaleEffect_, hsvFilter_ });
}

void BattleTestScene::SetupMainRenderTarget()
{
    SceneShared::SetupMainRenderTarget(dxCommon_, { imageFilter_, grayscaleEffect_, hsvFilter_ });
}
