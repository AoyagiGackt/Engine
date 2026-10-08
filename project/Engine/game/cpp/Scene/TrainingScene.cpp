/**
 * @file TrainingScene.cpp
 * @brief アクション操作練習用シーン（TrainingScene）の初期化と基本更新フローの実装
 */
#include "TrainingScene.h"
#include "WeaponSlotHud.h"
#include "AwakenGaugeHud.h"
#include "TrainingHud.h"
#include "AudioBridge.h"
#include "BorderBlockBuilder.h"
#include "FrameProfiler.h"
#include "GameConstants.h"
#include "ModelManager.h"
#include "PlayerBridge.h"
#include "SSAOEffect.h"
#include "SceneFlow.h"
#include "SceneManager.h"
#include "ScreenFlash.h"
#include "SlashMark.h"
#include "StageEditor.h"
#include "UILayout.h"
#include "TimeManager.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#ifdef _DEBUG
#include "EventBus.h"
#include "Logger.h"
#endif
#ifdef USE_IMGUI
#include <imgui.h>
#endif
using namespace engine;
using namespace engine::graphics;
using namespace engine::game;

static constexpr float kWarpX = 25.5f;
static constexpr float kWarpProximity = 3.0f;

static constexpr Vector3 kInitialCameraPosition = { 14.5f, 6.0f, 0.0f }; // zはGameConstants::kCameraDistanceZを使う
static constexpr float kGroundY = 0.4f;
static constexpr float kPlayerFacingTargetDistance = 8.0f; // 向きを決めるために渡す前方の注視点までの距離

// 背景の街並み

// バトルテストへのワープポータル
static constexpr int kWarpPortalBlockCount = 5;
static constexpr float kWarpPortalBlockSpacing = 1.0f;
static constexpr Vector4 kWarpPortalColor = { 0.1f, 0.9f, 1.0f, 0.9f };
static constexpr float kWarpPulseBase = 0.6f;
static constexpr float kWarpPulseAmplitude = 0.4f;
static constexpr float kWarpPulseSpeed = 4.0f;
static constexpr float kWarpPulseAlpha = 0.85f;
static constexpr float kWarpLabelWorldY = 5.0f;
static constexpr Vector2 kWarpLabelOffset = { 110.0f, 36.0f };
static constexpr float kWarpLabelScale = 1.5f;
static constexpr Vector4 kWarpLabelColor = { 0.2f, 1.0f, 1.0f, 1.0f };
static constexpr const char* kLayoutName = "training";

// フィニッシャー
static constexpr Vector4 kFinisherFlashColor = { 0.75f, 0.95f, 1.0f, 0.65f };
static constexpr float kSlashLineLengthMin = 4.0f;
static constexpr float kSlashLineLengthMax = 9.0f;
static constexpr Vector4 kSlashLineColor = { 0.75f, 0.95f, 1.0f, 1.0f };
static constexpr float kSlashLineThickness = 5.0f;
static constexpr float kSlashLineSeconds = 0.22f;

// 武器を拾った時のフラッシュ
static constexpr Vector4 kPickupFlashColor = { 0.55f, 0.9f, 1.0f, 0.22f };
static constexpr float kPickupFlashSeconds = 0.08f;

// デバッグ表示
static constexpr Vector2 kDebugTextPosition = { 1140.0f, 4.0f };
static constexpr float kDebugTextScale = 1.2f;
static constexpr Vector4 kDebugTextColor = { 0.6f, 1.0f, 0.6f, 0.85f };


void TrainingScene::Initialize(DirectXCommon* dxCommon, Input* input, Audio* audio)
{
    spriteCommon_ = InitializeCommonResources(dxCommon, input, audio, dxCommon_, input_, audio_);

    InitializeCoreSystems();
    InitializeStageModels();
    InitializePlayerAndBullets();
    InitializeWeaponPickups();
    InitializeHudAndEffects();

#ifdef _DEBUG
    // ビジュアルスクリプティングVMの動作確認用スモークテスト（エディタUIはまだ無い）
    testGraph_ = GraphIO::Load("Resources/Graphs/test_graph.json");
    EventBus::GetInstance()->Subscribe("low_hp_warning", [] { Logger::LogInfo("[GraphTest] low_hp_warning fired"); });
    EventBus::GetInstance()->Subscribe("hp_checked", [] { Logger::LogInfo("[GraphTest] hp_checked fired"); });
    testGraphRuntime_.Start(&testGraph_);
#endif
}

void TrainingScene::InitializeCoreSystems()
{
    srvManager_ = SrvManager::GetInstance();
    weaponManager_ = WeaponManager::GetInstance();

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

void TrainingScene::InitializeStageModels()
{
    modelBlock_ = ModelManager::GetInstance()->GetOrLoad(modelCommon_.get(),
        "Resources/block/block.obj",
        "Resources/block/block.png");

    BuildBorderBlocks(modelCommon_.get(), modelBlock_, borderBlocks_);

    // 背景のビルはレベルJSON（Resources/Levels/training.json）に置き、F2のステージエディタで編集する

    for (int i = 0; i < kWarpPortalBlockCount; ++i) {
        auto p = std::make_unique<Object3d>();
        p->Initialize(modelCommon_.get());
        p->SetModel(modelBlock_);
        p->SetEnableLighting(false);
        p->SetPosition({ kWarpX, kGroundY + static_cast<float>(i) * kWarpPortalBlockSpacing, 0.0f });
        p->SetColor(kWarpPortalColor);
        p->Update();
        warpPortalBlocks_.push_back(std::move(p));
    }
}

void TrainingScene::InitializePlayerAndBullets()
{
    player_ = std::make_unique<Player>();
    player_->Initialize(modelCommon_.get());
    // 水平方向の移動範囲は固定値で決め打ちしない壁ブロックの当たり判定（ResolveBlockCollision）が
    // StageEditorでの編集をそのまま反映するので、それ自体が境界として機能する
    // Open()/RegisterExternalEntity("Player")はGetEditorLevelPath()等のフック経由でBaseScene::Init()が自動で行う
    // （未作成のtraining.jsonなら空のまま起動し、F2エディタの+で配置してSaveで作成できる）
    PlayerBridge::GetInstance()->SetPlayer(player_.get());
    AudioBridge::GetInstance()->SetAudio(audio_);

    bulletPool_.Initialize(modelCommon_.get(), modelBlock_);
}

void TrainingScene::InitializeWeaponPickups()
{
    weaponPickups_.Initialize(modelCommon_.get());
}

void TrainingScene::InitializeHudAndEffects()
{


    fontRenderer_.Initialize(spriteCommon_.get());
    InitializeWeaponSlotHud();
    SlashMark::GetInstance()->Initialize(spriteCommon_.get());

    SSAOEffect::GetInstance()->Initialize(dxCommon_, srvManager_);

    GpuProfiler::GetInstance()->Initialize(dxCommon_);
}

void TrainingScene::InitializeWeaponSlotHud()
{
    hud_.Add(std::make_unique<TrainingHud>());
    hud_.Add(std::make_unique<AwakenGaugeHud>());
    hud_.Add(std::make_unique<WeaponSlotHud>(true));
    hud_.Initialize({ *spriteCommon_, *modelCommon_, *dxCommon_, *weaponManager_ });
    UpdateWeaponSlotHud();
}

void TrainingScene::UpdateWeaponSlotHud()
{
    HudFrame frame { *camera_, GameConstants::kFrameDeltaTime, player_->GetAwakenGauge(), player_->IsAwakened(), warpPulseTimer_ };
    hud_.Update(frame);
}

void TrainingScene::DrawWeaponSlotHud()
{
    hud_.Draw();
}

void TrainingScene::Update()
{
    fontRenderer_.Reset();

    if (input_->TriggerKey(DIK_BACK)) {
        audio_->PlayMenuSelect();
        SceneFlow::GetInstance()->Transition("TRAINING", "title", "TITLE");
        return;
    }
    if (input_->TriggerKey(DIK_TAB) || input_->TriggerButton(XINPUT_GAMEPAD_BACK)) {
        audio_->PlayMenuSelect();
        SceneFlow::GetInstance()->Transition("TRAINING", "map", "MAP");
        return;
    }

    // ステージエディタ表示中の一時停止（GetStageEditor().IsVisible()）分岐はBaseScene::Tick()が面倒を見る
    // （表示中はこのUpdate()自体が呼ばれずRefreshVisualTransformsForEditor()が代わりに呼ばれる）

    SceneShared::UpdateWeaponCycle(input_, weaponManager_, weaponCycleTimer_);
    UpdatePlayerAndBullets();
    UpdateWeaponPickups();
    UpdateCameraAndEnvironment();
    UpdateWeaponSlotHud();

#ifdef _DEBUG
    testGraphRuntime_.Update(TimeManager::GetInstance()->GetDeltaTime());
#endif

    bool nearWarp = SceneShared::UpdatePortalTransition(input_, player_->GetPosition(), kWarpX, kWarpProximity, "TRAINING", "battle_test", "BATTLETEST", audio_);
    DrawHud(nearWarp);
}

void TrainingScene::RefreshVisualTransformsForEditor()
{
    fontRenderer_.Reset();
    // ステージエディタ表示中はゲームプレイ（プレイヤー操作・カメラ追従）を丸ごと止める
    // （BattleTestSceneと同じ規約。TimeManagerのタイムスケールだけでは
    // このシーンの各種Updateが固定dtで動いてしまい止まらないため、BaseScene::Tick()がUpdate()の代わりにこちらを呼ぶ）
    player_->RefreshVisualTransforms();
    for (auto& b : borderBlocks_) {
        b->Update();
    }
    for (auto& portal : warpPortalBlocks_) {
        portal->Update();
    }
    weaponPickups_.RefreshVisuals();
    UpdateWeaponSlotHud();
    DrawHud(false);
}

void TrainingScene::UpdatePlayerAndBullets()
{
    // 乱舞用ダミーターゲット（正面 8 ユニット先）
    {
        const Vector3& pp = player_->GetPosition();
        const auto stageColliders = GetStageEditor().GetSolidColliders();
        player_->Update(input_, { pp.x + player_->GetLastDirX() * kPlayerFacingTargetDistance, pp.y, 0.0f }, false, !stageColliders.empty());
        player_->ResolveBlockCollision(stageColliders);
        player_->RefreshVisualTransforms();

        // 境界ブロックは描画専用なので、幅1のプレイヤー判定が
        // 中心座標 x=2 と x=28 の壁の内側に収まるよう補正する
    }

    // ── スペースキー スピン連射 ──────────────────────────────────────
    SceneShared::UpdateSpinShotFire(player_.get(), bulletPool_);
    bulletPool_.Update();

    // ── フィニッシャースラッシュ（ゲージ満タン消費）───────────────────
    if (player_->JustFinisherSlash()) {
        TimeManager::GetInstance()->RequestHitStop(GameConstants::kHitStopFinisherSlash);
        ScreenFlash::GetInstance()->Request(kFinisherFlashColor, GameConstants::kShakeFinisherSlashDur);

        static std::mt19937 rng { std::random_device { }() };
        std::uniform_real_distribution<float> angleDist(0.0f, GameConstants::kTwoPi);
        std::uniform_real_distribution<float> offXDist(-GameConstants::kCameraHalfW, GameConstants::kCameraHalfW);
        std::uniform_real_distribution<float> offYDist(-GameConstants::kCameraHalfH, GameConstants::kCameraHalfH);
        std::uniform_real_distribution<float> lenDist(kSlashLineLengthMin, kSlashLineLengthMax);
        const Vector3& cam = camera_->GetTranslate();
        for (int i = 0; i < GameConstants::kFinisherSlashLines; ++i) {
            const float ang = angleDist(rng);
            const Vector2 dir = { std::cos(ang), std::sin(ang) };
            const Vector2 center = { cam.x + offXDist(rng), cam.y + offYDist(rng) };
            const float len = lenDist(rng);
            SceneShared::SpawnSlashMarkWorld(
                { center.x - dir.x * len, center.y - dir.y * len },
                { center.x + dir.x * len, center.y + dir.y * len },
                cam.x, cam.y, kSlashLineColor, kSlashLineThickness, kSlashLineSeconds);
        }
    }
    SlashMark::GetInstance()->Update(GameConstants::kFrameDeltaTime);
}

void TrainingScene::UpdateCameraAndEnvironment()
{
    SceneShared::UpdateCameraFollow(camera_.get(), player_->GetPosition(), GetStageEditor().GetSolidColliders());
    player_->RefreshVisualTransforms();

#ifdef _DEBUG
    GpuProfiler::GetInstance()->ReadBack();
#endif

    shadowManager_->Update(objectCommon_->GetLightDirection());
    Object3d::SetLightViewProjection(shadowManager_->GetLightViewProjection());
    // GetStageEditor().UpdateObjects()はBaseScene::Tick()がUpdate()の後に一括して呼ぶ
    for (auto& b : borderBlocks_) {
        b->Update();
    }

    warpPulseTimer_ += GameConstants::kFrameDeltaTime;
    float pulse = kWarpPulseBase + kWarpPulseAmplitude * std::sin(warpPulseTimer_ * kWarpPulseSpeed);
    for (auto& p : warpPortalBlocks_) {
        p->SetColor({ kWarpPortalColor.x * pulse, kWarpPortalColor.y * pulse, kWarpPortalColor.z * pulse, kWarpPulseAlpha });
        p->Update();
    }
}

void TrainingScene::UpdateWeaponPickups()
{
    if (weaponPickups_.Update(player_->GetPosition(), *weaponManager_, GameConstants::kFrameDeltaTime)) {
        hud_.Notify(HudEvent::WeaponAcquired);
        ScreenFlash::GetInstance()->Request(kPickupFlashColor, kPickupFlashSeconds);
    }
}

void TrainingScene::DrawHud(bool nearWarpPortal)
{
    hud_.QueueText(fontRenderer_);
    DrawWeaponHud(nearWarpPortal);
    DrawDebugHud();
}

void TrainingScene::DrawWeaponHud(bool nearWarpPortal)
{
    // ワープラベル（ポータルの上）
    if (nearWarpPortal) {
        const Vector3& cam = camera_->GetTranslate();
        float sx, sy;
        SceneShared::WorldToScreen(kWarpX, kWarpLabelWorldY, cam.x, cam.y, sx, sy);
        UILayout& layout = UILayout::Get(kLayoutName);
        fontRenderer_.DrawStringW(input_->ExpandPrompts(L"[ {Interact} ] バトルテストへ"), sx - kWarpLabelOffset.x, sy - kWarpLabelOffset.y,
            layout.Float("portal.label_scale", kWarpLabelScale), layout.Color("portal.label_color", kWarpLabelColor));
    }
}

void TrainingScene::DrawDebugHud()
{
    // ── デバッグ情報（右上隅） ──────────────────────────────────────
#ifdef _DEBUG
    {
        char dbgBuf[64];
        float fps = FrameProfiler::GetInstance()->GetFPS();
        float ms = FrameProfiler::GetInstance()->GetMs();
        std::snprintf(dbgBuf, sizeof(dbgBuf), "%.0f FPS  %.2f ms", fps, ms);
        fontRenderer_.DrawString(dbgBuf, kDebugTextPosition.x, kDebugTextPosition.y, kDebugTextScale, kDebugTextColor);
    }
#endif

#ifdef USE_IMGUI
    // ── プロファイラ ───────────────────────────────────────────────────
    GpuProfiler::GetInstance()->DrawImGui();
#endif
}

void TrainingScene::Draw()
{
    ID3D12GraphicsCommandList* cmd = dxCommon_->GetCommandList();
    auto* gpuProfiler = GpuProfiler::GetInstance();

    // シャドウパス
    gpuProfiler->BeginScope(GpuProfiler::Shadow, cmd);
    shadowManager_->BeginShadowPass(cmd);
    modelCommon_->BeginShadowPass();
    shadowManager_->EndShadowPass(cmd);
    gpuProfiler->EndScope(GpuProfiler::Shadow, cmd);

    // SSAO ノーマルキャプチャパス
    auto* ssao = SSAOEffect::GetInstance();
    gpuProfiler->BeginScope(GpuProfiler::SSAO, cmd);
    if (ssao->IsEnabled()) {
        ssao->BeginNormalCapture(dxCommon_, camera_.get());
        for (auto& b : borderBlocks_) {
            b->DrawForNormalCapture();
        }
        for (auto& p : warpPortalBlocks_) {
            p->DrawForNormalCapture();
        }
        ssao->EndNormalCapture(dxCommon_);
    }
    gpuProfiler->EndScope(GpuProfiler::SSAO, cmd);

    // メイン3D描画
    gpuProfiler->BeginScope(GpuProfiler::Main3D, cmd);
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = dxCommon_->GetCurrentBackBufferHandle();
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = dxCommon_->GetBackBufferDsvHandle();
    cmd->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    D3D12_VIEWPORT vp = dxCommon_->GetCenteredClientViewport();
    D3D12_RECT scissor = dxCommon_->GetCenteredClientScissorRect();
    cmd->RSSetViewports(1, &vp);
    cmd->RSSetScissorRects(1, &scissor);

    modelCommon_->CommonDrawSettings();
    objectCommon_->SetDefaultLight(cmd);
    shadowManager_->SetShadowMap(cmd, srvManager_);

    for (auto& b : borderBlocks_) {
        b->Draw();
    }
    for (auto& p : warpPortalBlocks_) {
        p->Draw();
    }
    weaponPickups_.Draw();
    bulletPool_.Draw();
    player_->Draw();

    // ステージエディタの配置ブロックはここで描く（HUDテキストより前＝ブロックがUIパネルに重ならないように）
    // BaseScene::Render()側の自動呼び出しはWasObjectsDrawnThisFrame()で自動的にスキップされる
    GetStageEditor().DrawObjects();

    // SSAO 計算 → ブラー → 乗算合成
    if (ssao->IsEnabled()) {
        ssao->Compute(dxCommon_, camera_.get());
        ssao->Blur(dxCommon_);
        cmd->OMSetRenderTargets(1, &rtv, FALSE, &dsv); // バックバッファに戻す
        cmd->RSSetViewports(1, &vp);
        cmd->RSSetScissorRects(1, &scissor);
        ssao->Apply(dxCommon_, srvManager_);
    }
    gpuProfiler->EndScope(GpuProfiler::Main3D, cmd);

    // GPU タイムスタンプ解決（PostDraw 後の ReadBack で取得）
    gpuProfiler->Resolve(cmd);

    // 2D スプライト（テキスト UI）
    spriteCommon_->CommonDrawSettings();
    DrawWeaponSlotHud();
    SlashMark::GetInstance()->Draw();
    GetStageEditor().DrawUIText(fontRenderer_);
    fontRenderer_.Draw();
}

void TrainingScene::Finalize()
{
    SlashMark::GetInstance()->Clear();
    GpuProfiler::GetInstance()->Finalize();
}
