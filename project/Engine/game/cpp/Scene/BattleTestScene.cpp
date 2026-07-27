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

    // 背景の街並みはbattletest.json側のkind="background"配置物として管理する
    // （エディタのHierarchy/Inspectorから通常の配置物と同じく選択・移動・削除・保存ができる）

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
    // 水平方向の移動範囲は固定値で決め打ちしない壁ブロックの当たり判定（ResolveBlockCollision）が
    // StageEditorでの編集をそのまま反映するので、それ自体が境界として機能する
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

    GetStageEditor().SetWaterSplashCallback([this](const Vector3& pos) { SpawnWaterSplashEffect(pos); });
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

void BattleTestScene::SpawnWaterSplashEffect(const Vector3& pos)
{
    pm_->EmitRing("bt_water_splash", pos, 2.2f, { 0.6f, 0.86f, 1.0f, 0.9f }, 8, 0.4f, 0.12f);
    static std::mt19937 rng { std::random_device { }() };
    std::uniform_real_distribution<float> vx(-2.5f, 2.5f);
    std::uniform_real_distribution<float> vy(2.0f, 4.5f);
    for (int i = 0; i < 5; ++i) {
        pm_->EmitGravity("bt_water_splash", pos,
            { vx(rng), vy(rng), 0.0f },
            { 0.55f, 0.82f, 1.0f, 0.9f }, 0.5f, 0.1f);
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
    if (showHud_) {
        DrawHud(nearReturn);
    }
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
}

void BattleTestScene::UpdatePlayerAndCamera()
{
    // 乱舞のターゲット ロック中ならその対象、そうでなければ最も近いダミー
    {
        const Vector3& pp = player_->GetPosition();
        Vector3 rampTarget = { pp.x + player_->GetLastDirX() * 6.0f, pp.y, 0.0f };

        if (lockedKind_ == LockTargetKind::Dummy && lockedDummyIndex_ < dummies_.size()) {
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

    // ステージエディタでsolidにしたブロックとの当たり判定
    // カメラ追従・見た目反映より前に解決しないと、重力計算のブロック非考慮ぶん（1フレーム分の
    // 突き抜け）がそのままカメラとモデルに映り、次フレームで正しい高さへ戻る様子が
    // 「高速で降りて後からカメラが付いてくる」ように見えてしまう
    player_->ResolveBlockCollision(GetStageEditor().GetSolidColliders());

    SceneShared::UpdateCameraFollow(camera_.get(), player_->GetPosition(), GetStageEditor().GetSolidColliders());
    player_->RefreshVisualTransforms();
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
}

void BattleTestScene::UpdateTargetLock()
{
    // ロック中の対象が無効になっていたら（撃破された等）自動解除
    if (lockedKind_ == LockTargetKind::Dummy && lockedDummyIndex_ >= dummies_.size()) {
        lockedKind_ = LockTargetKind::None;
    }

    if (!input_->TriggerKey(DIK_LSHIFT)) {
        return;
    }

    // 候補: 生存中のダミー → 末尾はロック解除として巡回する
    struct Candidate {
        LockTargetKind kind;
        size_t index;
    };
    std::vector<Candidate> candidates;
    for (size_t i = 0; i < dummies_.size(); ++i) {
        candidates.push_back({ LockTargetKind::Dummy, i });
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

// ══════════════════════════════════════════════════════
// HUD更新と描画
// ══════════════════════════════════════════════════════


void BattleTestScene::DrawHud(bool nearReturnPortal)
{
    DrawWeaponHud(nearReturnPortal);
    DrawComboHud();
    SceneShared::DrawControlsHud(fontRenderer_, L": トレーニングへ戻る");
    styleMeter_.UpdateHud(fontRenderer_); // 右上のスタイリッシュランク
    SceneShared::DrawAwakenGaugeHud(fontRenderer_, awakenGaugeBg_.get(), awakenGaugeFg_.get(),
        player_->GetAwakenGauge(), player_->IsAwakened(), warpPulseTimer_);

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
            SceneShared::WorldToScreen(tpos.x, tpos.y + 1.6f, cam.x, cam.y, sx, sy);
            fontRenderer_.DrawString("v LOCK v", sx - 46.0f, sy, 1.3f, { 1.0f, 0.35f, 0.2f, 1.0f });
        }
    }
}

void BattleTestScene::DrawComboHud()
{
    if (!weaponManager_->HasEquippedWeapon()) {
        return;
    }
    const int comboStep = player_->GetComboStep();
    if (comboStep <= 0) {
        return;
    }

    const WeaponData& weapon = weaponManager_->GetCurrent();
    const Vector4 color = { weapon.styleColor[0], weapon.styleColor[1],
        weapon.styleColor[2], weapon.styleColor[3] };
    std::wstring comboText = L"コンボ ";
    const int comboMax = player_->GetComboMax();
    for (int i = 1; i <= comboMax; ++i) {
        comboText += i <= comboStep ? L"[*]" : L"[ ]";
    }
    fontRenderer_.DrawStringW(comboText, 780.0f, 448.0f, 1.5f, color);
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
