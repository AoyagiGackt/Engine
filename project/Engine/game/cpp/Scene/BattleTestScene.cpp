/**
 * @file BattleTestScene.cpp
 * @brief 訓練用バトルシーン（BattleTestScene）の初期化と基本更新フローの実装
 */
#include "BattleTestScene.h"
#include "AudioBridge.h"
#include "BattleTestSceneRenderer.h"
#include "Collision.h"
#include "DiagnosticsDraw.h"
#include "GameConstants.h"
#include "GrayscaleEffect.h"
#include "HsvFilter.h"
#include "ImGuiControl.h"
#include "ModelManager.h"
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

static constexpr Vector3 kInitialCameraPosition = { 19.0f, 6.0f, 0.0f }; // zはGameConstants::kCameraDistanceZを使う
static constexpr float kGroundY = 0.4f;

// トレーニングへ戻るポータル
static constexpr int kWarpPortalBlockCount = 5;
static constexpr float kWarpPortalBlockSpacing = 1.0f;
static constexpr Vector4 kWarpPortalColor = { 1.0f, 0.5f, 0.1f, 0.9f };
static constexpr float kWarpPulseBase = 0.6f;
static constexpr float kWarpPulseAmplitude = 0.4f;
static constexpr float kWarpPulseSpeed = 4.0f;
static constexpr float kWarpLabelWorldY = 5.0f;
static constexpr Vector2 kWarpLabelOffset = { 110.0f, 36.0f };

// ダミー
static constexpr float kDummySpawnX = 15.0f;
static constexpr Vector4 kHpBarBackgroundColor = { 0.2f, 0.2f, 0.2f, 0.8f };
static constexpr Vector4 kHpBarInitialColor = { 0.2f, 0.9f, 0.2f, 0.9f };
static constexpr float kHpBarGreenScale = 0.85f;
static constexpr float kHpBarGreenBase = 0.15f;
static constexpr float kHpBarAlpha = 0.9f;
static constexpr float kFinisherShatterSeconds = 0.9f;

// ヒット演出
static constexpr float kHitRingSpeed = 3.0f;
static constexpr Vector4 kHitRingColor = { 1.0f, 0.85f, 0.2f, 1.0f };
static constexpr int kHitRingCount = 12;
static constexpr float kHitRingLifetime = 0.25f;
static constexpr float kHitRingSize = 0.18f;
static constexpr float kHitSparkSpreadX = 3.0f;
static constexpr float kHitSparkRiseMin = 2.0f;
static constexpr float kHitSparkRiseMax = 5.0f;
static constexpr int kHitSparkCount = 6;
static constexpr Vector4 kHitSparkColor = { 1.0f, 0.55f, 0.1f, 1.0f };
static constexpr float kHitSparkLifetime = 0.6f;
static constexpr float kHitSparkSize = 0.13f;

// 水しぶき
static constexpr float kSplashRingSpeed = 2.2f;
static constexpr Vector4 kSplashRingColor = { 0.6f, 0.86f, 1.0f, 0.9f };
static constexpr int kSplashRingCount = 8;
static constexpr float kSplashRingLifetime = 0.4f;
static constexpr float kSplashRingSize = 0.12f;
static constexpr float kSplashDropSpreadX = 2.5f;
static constexpr float kSplashDropRiseMin = 2.0f;
static constexpr float kSplashDropRiseMax = 4.5f;
static constexpr int kSplashDropCount = 5;
static constexpr Vector4 kSplashDropColor = { 0.55f, 0.82f, 1.0f, 0.9f };
static constexpr float kSplashDropLifetime = 0.5f;
static constexpr float kSplashDropSize = 0.1f;

static constexpr float kMinClipW = 0.0001f; // これ以下のw成分ではスクリーン座標へ変換しない（カメラの背後）
static constexpr float kRampageDefaultTargetDistance = 6.0f; // 対象がいない時に乱舞が向かう前方の距離

// HUD
static constexpr Vector2 kBattleControlsAnchor = { 1020.0f, 12.0f };
static constexpr float kLockMarkerHeight = 1.6f;
static constexpr float kLockMarkerHalfWidth = 46.0f;
static constexpr float kLockMarkerScale = 1.3f;
static constexpr Vector4 kLockMarkerColor = { 1.0f, 0.35f, 0.2f, 1.0f };
static constexpr float kHintLineHeight = 24.0f;
static constexpr Vector2 kColliderLabelPosition = { 12.0f, 84.0f };
static constexpr Vector4 kColliderLabelColor = { 1.0f, 0.5f, 0.1f, 1.0f };

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
    camera_->SetTranslate({ kInitialCameraPosition.x, kInitialCameraPosition.y, GameConstants::kCameraDistanceZ });
    Object3d::SetCommonCamera(camera_.get());
}

void BattleTestScene::InitializeStageModels()
{
    modelBlock_ = ModelManager::GetInstance()->GetOrLoad(modelCommon_.get(),
        "Resources/block/block.obj",
        "Resources/block/block.png");

    // 背景の街並みはbattletest.json側のkind="background"配置物として管理する
    // （エディタのHierarchy/Inspectorから通常の配置物と同じく選択・移動・削除・保存ができる）

    // 境界ブロック・トリガーは本番ステージと同じ level01.json から読み込む
    // （GetEditorLevelPath()経由でBaseScene::Init()が自動でOpen()する。F2でその場編集も可能）

    for (int i = 0; i < kWarpPortalBlockCount; ++i) {
        auto p = std::make_unique<Object3d>();
        p->Initialize(modelCommon_.get());
        p->SetModel(modelBlock_);
        p->SetEnableLighting(false);
        p->SetPosition({ kWarpRetX, kGroundY + static_cast<float>(i) * kWarpPortalBlockSpacing, 0.0f });
        p->SetColor(kWarpPortalColor);
        p->Update();
        warpPortalBlocks_.push_back(std::move(p));
    }

    modelDummy_ = ModelManager::GetInstance()->GetOrLoad(modelCommon_.get(),
        "Resources/AnimatedMonsterPackby@Quaternius/OBJ/Slime.obj",
        "Resources/AnimatedMonsterPackby@Quaternius/OBJ/SlimePalette.png");

    SceneShared::CreateParticleGroupsFromJson(pm_, "Resources/particles/battletest.json");
}

void BattleTestScene::InitializeDummyEnemies()
{
    Dummy d;
    d.pos = { kDummySpawnX, kGroundY, 0.0f };
    d.homePos = d.pos;
    d.hp = kDummyMaxHp;
    d.maxHp = kDummyMaxHp;
    d.hitFlash = 0.0f;

    d.object = std::make_unique<Object3d>();
    d.object->Initialize(modelCommon_.get());
    d.object->SetModel(modelDummy_);
    d.object->SetEnableLighting(false);
    d.object->SetScale({ kDummyModelScale, kDummyModelScale, kDummyModelScale });
    d.object->SetPosition({ d.pos.x, d.pos.y + kDummyModelFootOffsetY, d.pos.z });
    d.object->Update();

    d.hpBarBg = std::make_unique<Sprite>();
    d.hpBarBg->Initialize(spriteCommon_.get(), "Resources/white.png");
    d.hpBarBg->SetColor(kHpBarBackgroundColor);

    d.hpBarFg = std::make_unique<Sprite>();
    d.hpBarFg->Initialize(spriteCommon_.get(), "Resources/white.png");
    d.hpBarFg->SetColor(kHpBarInitialColor);

    dummies_.push_back(std::move(d));
}

void BattleTestScene::InitializePlayerAndBullets()
{
    player_ = std::make_unique<Player>();
    player_->Initialize(modelCommon_.get());
    // 水平方向の移動範囲は固定値で決め打ちしない壁ブロックの当たり判定（ResolveBlockCollision）が
    // StageEditorでの編集をそのまま反映するので、それ自体が境界として機能する
    // "Player"のRegisterExternalEntity()はGetEditorPlayerPositionRef()経由でBaseScene::Init()が自動で行う
    PlayerBridge::GetInstance()->SetPlayer(player_.get());
    AudioBridge::GetInstance()->SetAudio(audio_);

    bulletPool_.Initialize(modelCommon_.get(), modelBlock_);
}

void BattleTestScene::InitializeHud()
{


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
    finisherShatter_.SetDuration(kFinisherShatterSeconds);
    ImGuiControlPanel::RegisterGlassShatterTrigger([this]() { TriggerGlassShatterTest(); });

    GetStageEditor().SetWaterSplashCallback([this](const Vector3& pos) { SpawnWaterSplashEffect(pos); });
}

void BattleTestScene::SpawnHitEffect(const Vector3& pos)
{
    pm_->EmitRing("bt_hit_ring", pos, kHitRingSpeed, kHitRingColor, kHitRingCount, kHitRingLifetime, kHitRingSize);
    static std::mt19937 rng { std::random_device { }() };
    std::uniform_real_distribution<float> vx(-kHitSparkSpreadX, kHitSparkSpreadX);
    std::uniform_real_distribution<float> vy(kHitSparkRiseMin, kHitSparkRiseMax);
    for (int i = 0; i < kHitSparkCount; ++i) {
        pm_->EmitGravity("bt_hit_spark", pos,
            { vx(rng), vy(rng), 0.0f },
            kHitSparkColor, kHitSparkLifetime, kHitSparkSize);
    }
}

void BattleTestScene::SpawnWaterSplashEffect(const Vector3& pos)
{
    pm_->EmitRing("bt_water_splash", pos, kSplashRingSpeed, kSplashRingColor, kSplashRingCount, kSplashRingLifetime, kSplashRingSize);
    static std::mt19937 rng { std::random_device { }() };
    std::uniform_real_distribution<float> vx(-kSplashDropSpreadX, kSplashDropSpreadX);
    std::uniform_real_distribution<float> vy(kSplashDropRiseMin, kSplashDropRiseMax);
    for (int i = 0; i < kSplashDropCount; ++i) {
        pm_->EmitGravity("bt_water_splash", pos,
            { vx(rng), vy(rng), 0.0f },
            kSplashDropColor, kSplashDropLifetime, kSplashDropSize);
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
        d.hpBarFg->SetColor({ 1.0f - ratio, ratio * kHpBarGreenScale + kHpBarGreenBase, 0.0f, kHpBarAlpha });

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
    if (input_->TriggerKey(DIK_F4)) {
        showHud_ = !showHud_;
    }
    if (showColliders_) {
        DrawColliderOverlay();
    }

    SceneShared::UpdateWeaponCycle(input_, weaponManager_, weaponCycleTimer_, true);
    UpdateTargetLock();
    UpdatePlayerAndCamera();
    UpdateEnvironment();

    UpdateCombat();
    UpdateFinisherSlash();
    styleMeter_.Update(GameConstants::kFrameDeltaTime);
    UpdateDummies();
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
        if (cw > kMinClipW) {
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

    bool nearReturn = SceneShared::UpdatePortalTransition(input_, player_->GetPosition(), kWarpRetX, kReturnProx, "TRAINING", audio_);
    if (showHud_) {
        DrawHud(nearReturn);
    }
}

void BattleTestScene::RefreshVisualTransformsForEditor()
{
    fontRenderer_.Reset();
    // ステージエディタ表示中はゲームプレイ（敵AI・プレイヤー操作・カメラ追従）を丸ごと止める
    // TimeManagerのタイムスケールだけでは、このシーンの各種Updateが固定dt(GameConstants::kFrameDeltaTime)で
    // 動いてしまい止まらないため、BaseScene::Tick()がUpdate()の代わりにこちらを呼ぶ

    // Object3dのUpdate()はカメラのVP行列込みで定数バッファを書くため、
    // WASDでカメラを動かしても正しい位置に描かれるよう、全モデルの行列だけは毎フレーム再計算する
    // （これを怠ると古いカメラ行列のまま描画され、モデルが画面に張り付いてついてくるように見える）
    player_->RefreshVisualTransforms();
    bulletPool_.RefreshVisualTransforms();
    for (auto& d : dummies_) {
        d.object->Update();
    }
    for (auto& p : warpPortalBlocks_) {
        p->Update();
    }
    // 武器スロットの3Dアイコン（カメラ相対配置のHUD）とHPバー（WorldToScreen配置）はカメラ移動に追従させる
    UpdateWeaponSlotHud();
    UpdateHpBars();

    if (showColliders_) {
        DrawColliderOverlay();
    }

    if (showHud_) { DrawHud(false); }
}

void BattleTestScene::UpdatePlayerAndCamera()
{
    const auto stageColliders = GetStageEditor().GetSolidColliders();
    // 乱舞のターゲット ロック中ならその対象、そうでなければ最も近いダミー
    {
        const Vector3& pp = player_->GetPosition();
        Vector3 rampTarget = { pp.x + player_->GetLastDirX() * kRampageDefaultTargetDistance, pp.y, 0.0f };

        bool targetSolid = false;
        if (lockedKind_ == LockTargetKind::Dummy && lockedDummyIndex_ < dummies_.size()
            && dummies_[lockedDummyIndex_].hp > 0.0f) {
            rampTarget = dummies_[lockedDummyIndex_].pos;
            targetSolid = true;
        } else {
            float minDist = FLT_MAX;
            for (const auto& d : dummies_) {
                if (d.hp <= 0.0f) {
                    continue;
                }
                float dist = std::abs(d.pos.x - pp.x);
                if (dist < minDist) {
                    minDist = dist;
                    rampTarget = d.pos;
                    targetSolid = true;
                }
            }
        }
        player_->Update(input_, rampTarget, targetSolid, !stageColliders.empty());
    }

    // ステージエディタでsolidにしたブロックとの当たり判定
    // カメラ追従・見た目反映より前に解決しないと、重力計算のブロック非考慮ぶん（1フレーム分の
    // 突き抜け）がそのままカメラとモデルに映り、次フレームで正しい高さへ戻る様子が
    // 「高速で降りて後からカメラが付いてくる」ように見えてしまう
    player_->ResolveBlockCollision(stageColliders);

    // ロック中は移動入力に関係なく対象の方を向かせ、カメラも少しだけ対象側へ寄せて気付きやすくする
    const Vector3* lockTargetPos = nullptr;
    if (lockedKind_ == LockTargetKind::Dummy && lockedDummyIndex_ < dummies_.size()) {
        lockTargetPos = &dummies_[lockedDummyIndex_].pos;
        player_->FaceTarget(*lockTargetPos);
    }
    SceneShared::UpdateCameraFollow(camera_.get(), player_->GetPosition(), GetStageEditor().GetSolidColliders(), lockTargetPos);
    player_->RefreshVisualTransforms();
}

void BattleTestScene::UpdateEnvironment()
{
    shadowManager_->Update(objectCommon_->GetLightDirection());
    Object3d::SetLightViewProjection(shadowManager_->GetLightViewProjection());
    // GetStageEditor().UpdateObjects()はBaseScene::Tick()がUpdate()の後に一括して呼ぶ

    warpPulseTimer_ += GameConstants::kFrameDeltaTime;
    float pulse = kWarpPulseBase + kWarpPulseAmplitude * std::sin(warpPulseTimer_ * kWarpPulseSpeed);
    for (auto& p : warpPortalBlocks_) {
        p->SetColor({ kWarpPortalColor.x * pulse, kWarpPortalColor.y * pulse, kWarpPortalColor.z * pulse, kWarpPortalColor.w });
        p->Update();
    }
}

void BattleTestScene::UpdateTargetLock()
{
    // Shiftを押している間だけロックオンし、その間は常に一番近いダミーを対象にし続ける
    // （押した瞬間の対象に固定するのではなく、離れたら別の敵が近くなるような場面でも自然に切り替わる）
    if (!input_->PushKey(DIK_LSHIFT)) {
        lockedKind_ = LockTargetKind::None;
        return;
    }

    // 画面外の敵まで拾うとロック対象の方へカメラが大きく寄ってしまい暴れて見えるため、
    // 画面内に映る範囲（カメラ半幅）より遠い敵はロック対象から除外する
    constexpr float kMaxLockRange = GameConstants::kCameraHalfW;

    const Vector3& pp = player_->GetPosition();
    float minDist = FLT_MAX;
    int nearest = -1;
    for (size_t i = 0; i < dummies_.size(); ++i) {
        if (dummies_[i].hp <= 0.0f) {
            continue;
        }
        const float dist = std::abs(dummies_[i].pos.x - pp.x);
        if (dist > kMaxLockRange) {
            continue;
        }
        if (dist < minDist) {
            minDist = dist;
            nearest = static_cast<int>(i);
        }
    }

    if (nearest < 0) {
        lockedKind_ = LockTargetKind::None;
        return;
    }
    lockedKind_ = LockTargetKind::Dummy;
    lockedDummyIndex_ = static_cast<size_t>(nearest);
}

// ══════════════════════════════════════════════════════
// HUD更新と描画
// ══════════════════════════════════════════════════════

void BattleTestScene::DrawHud(bool nearReturnPortal)
{
    DrawWeaponHud(nearReturnPortal);
    SceneShared::DrawControlsHud(fontRenderer_,
        GetStageEditor().GetHudAnchorPosition("hud_anchor_controls", kBattleControlsAnchor), L": トレーニングへ戻る");
    styleMeter_.UpdateHud(fontRenderer_); // 右上のスタイリッシュランク
    hud_.QueueText(fontRenderer_);

    // ── ロックオン中の対象にマーカーを出す ────────────────────────
    if (lockedKind_ != LockTargetKind::None) {
        Vector3 tpos { };
        bool valid = true;
        if (lockedKind_ == LockTargetKind::Dummy && lockedDummyIndex_ < dummies_.size()) {
            tpos = dummies_[lockedDummyIndex_].pos;
        } else {
            valid = false;
        }
        if (valid) {
            const Vector3& cam = camera_->GetTranslate();
            float sx, sy;
            SceneShared::WorldToScreen(tpos.x, tpos.y + kLockMarkerHeight, cam.x, cam.y, sx, sy);
            fontRenderer_.DrawString("v LOCK v", sx - kLockMarkerHalfWidth, sy, kLockMarkerScale, kLockMarkerColor);
        }
    }
}

void BattleTestScene::DrawWeaponHud(bool nearReturnPortal)
{
    constexpr float kScale = 1.5f;
    // 明るいブロックの上でも埋もれないよう暖色＋影付きにする（武器選択パネルと揃える）
    constexpr Vector4 kColorHint = { 0.80f, 0.76f, 0.65f, 1.0f };
    constexpr Vector4 kShadow = { 0.05f, 0.04f, 0.02f, 0.9f };
    constexpr float kShadowOffset = 1.6f;
    auto drawShadowedHint = [&](const std::wstring& text, float x, float y) {
        fontRenderer_.DrawStringW(text, x + kShadowOffset, y + kShadowOffset, kScale, kShadow);
        fontRenderer_.DrawStringW(text, x, y, kScale, kColorHint);
    };

    const Vector2 weaponHudAnchor = GetStageEditor().GetHudAnchorPosition("hud_anchor_weapon_list", kDefaultHudWeaponAnchor);
    float py = SceneShared::DrawWeaponListHud(fontRenderer_, weaponManager_, L"テストステージ", weaponHudAnchor);
    drawShadowedHint(L"[L] コンボ  [S+L] 打ち上げ  [空中L] 空中コンボ", weaponHudAnchor.x, py);
    drawShadowedHint(L"[K] 射撃  [R] 覚醒  [Shift長押し] ロックオン（最寄りの敵）", weaponHudAnchor.x, py + kHintLineHeight);

    // 戻りポータルのラベル
    if (nearReturnPortal) {
        const Vector3& cam = camera_->GetTranslate();
        float sx, sy;
        SceneShared::WorldToScreen(kWarpRetX, kWarpLabelWorldY, cam.x, cam.y, sx, sy);
        constexpr Vector4 kColorReturn = { 1.0f, 0.6f, 0.1f, 1.0f };
        fontRenderer_.DrawStringW(L"[ ENTER ] トレーニングへ", sx - kWarpLabelOffset.x, sy - kWarpLabelOffset.y, kScale, kColorReturn);
    }

    if (showColliders_) {
        fontRenderer_.DrawString("[ F3 ] Colliders: ON", kColliderLabelPosition.x, kColliderLabelPosition.y, kScale, kColliderLabelColor);
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
