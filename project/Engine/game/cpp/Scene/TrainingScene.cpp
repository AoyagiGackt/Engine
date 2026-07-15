#include "TrainingScene.h"
#include "AudioBridge.h"
#include "BorderBlockBuilder.h"
#include "DebugProfiler.h"
#include "GameConstants.h"
#include "PlayerBridge.h"
#include "SSAOEffect.h"
#include "SceneManager.h"
#include "ScreenFlash.h"
#include "SlashMark.h"
#include "StageEditor.h"
#include "TimeManager.h"
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

void TrainingScene::Initialize(DirectXCommon* dxCommon, Input* input, Audio* audio)
{
    dxCommon_ = dxCommon;
    input_ = input;
    audio_ = audio;

    srvManager_ = SrvManager::GetInstance();
    weaponManager_ = WeaponManager::GetInstance();

    spriteCommon_ = std::make_unique<SpriteCommon>();
    spriteCommon_->Initialize(dxCommon_);

    modelCommon_ = std::make_unique<ModelCommon>();
    modelCommon_->Initialize(dxCommon_);

    objectCommon_ = std::make_unique<Object3dCommon>();
    objectCommon_->Initialize(dxCommon_);

    shadowManager_ = std::make_unique<ShadowManager>();
    shadowManager_->Initialize(dxCommon_, srvManager_);

    // OutlineEffect等でルートシグネチャを切り替えた後にライト/シャドウマップを再バインドできるようにする
    Object3d::SetCommonObjectCommon(objectCommon_.get());
    Object3d::SetCommonShadowManager(shadowManager_.get());

    camera_ = std::make_unique<Camera>();
    camera_->SetTranslate({ 14.5f, 6.0f, -24.0f });
    Object3d::SetCommonCamera(camera_.get());

    modelBlock_ = std::make_unique<Model>();
    modelBlock_->Initialize(modelCommon_.get(),
        "Resources/block/block.obj",
        "Resources/block/block.png");

    BuildBorderBlocks(modelCommon_.get(), modelBlock_.get(), borderBlocks_);

    for (int i = 0; i < 5; ++i) {
        auto p = std::make_unique<Object3d>();
        p->Initialize(modelCommon_.get());
        p->SetModel(modelBlock_.get());
        p->SetEnableLighting(false);
        p->SetPosition({ kWarpX, 0.4f + static_cast<float>(i) * 1.0f, 0.0f });
        p->SetColor({ 0.1f, 0.9f, 1.0f, 0.9f });
        p->Update();
        warpPortalBlocks_.push_back(std::move(p));
    }

    player_ = std::make_unique<Player>();
    player_->Initialize(modelCommon_.get());
    // Open()/RegisterExternalEntity("Player")はGetEditorLevelPath()等のフック経由でBaseScene::Init()が自動で行う
    // （未作成のtraining.jsonなら空のまま起動し、F2エディタの「+」で配置してSaveで作成できる）
    PlayerBridge::GetInstance()->SetPlayer(player_.get());
    AudioBridge::GetInstance()->SetAudio(audio_);

    bulletPool_.Initialize(modelCommon_.get(), modelBlock_.get());

    awakenGaugeBg_ = std::make_unique<Sprite>();
    awakenGaugeBg_->Initialize(spriteCommon_.get(), "Resources/white.png");
    awakenGaugeBg_->SetColor({ 0.05f, 0.05f, 0.15f, 0.75f });

    awakenGaugeFg_ = std::make_unique<Sprite>();
    awakenGaugeFg_->Initialize(spriteCommon_.get(), "Resources/white.png");

    fontRenderer_.Initialize(spriteCommon_.get());
    SlashMark::GetInstance()->Initialize(spriteCommon_.get());

    SSAOEffect::GetInstance()->Initialize(dxCommon_, srvManager_);

    // PBR デモブロック（3 種類を画面中央付近に配置）
    static constexpr float kPBRX[3] = { 7.0f, 14.0f, 21.0f };
    static constexpr Vector4 kPBRColor[3] = {
        { 0.80f, 0.55f, 0.45f, 1.0f }, // 非金属（テラコッタ調）
        { 0.90f, 0.90f, 1.00f, 1.0f }, // 鏡面金属（シルバー）
        { 0.55f, 0.65f, 0.70f, 1.0f }, // ラフ金属（ガンメタル）
    };
    for (int i = 0; i < 3; ++i) {
        pbrDemoBlocks_[i] = std::make_unique<Object3d>();
        pbrDemoBlocks_[i]->Initialize(modelCommon_.get());
        pbrDemoBlocks_[i]->SetModel(modelBlock_.get());
        pbrDemoBlocks_[i]->SetPosition({ kPBRX[i], 9.0f, 0.0f });
        pbrDemoBlocks_[i]->SetScale({ 1.5f, 1.5f, 1.5f });
        pbrDemoBlocks_[i]->SetColor(kPBRColor[i]);
        pbrDemoBlocks_[i]->SetMetallic(pbrMetallic_[i]);
        pbrDemoBlocks_[i]->SetRoughness(pbrRoughness_[i]);
        pbrDemoBlocks_[i]->SetShadingTypePBR();
        pbrDemoBlocks_[i]->Update();
    }

    GpuProfiler::GetInstance()->Initialize(dxCommon_);

#ifdef _DEBUG
    // ビジュアルスクリプティングVMの動作確認用スモークテスト（エディタUIはまだ無い）
    testGraph_ = GraphIO::Load("Resources/Graphs/test_graph.json");
    EventBus::GetInstance()->Subscribe("low_hp_warning", [] { Logger::LogInfo("[GraphTest] low_hp_warning fired"); });
    EventBus::GetInstance()->Subscribe("hp_checked", [] { Logger::LogInfo("[GraphTest] hp_checked fired"); });
    testGraphRuntime_.Start(&testGraph_);
#endif
}

void TrainingScene::Update()
{
    DebugProfiler::GetInstance()->EndFrame();
    DebugProfiler::GetInstance()->BeginFrame();
    fontRenderer_.Reset();

    if (input_->TriggerKey(DIK_BACK)) {
        SceneManager::GetInstance()->ChangeScene("TITLE", 0.4f, 0.4f);
        return;
    }

    // ステージエディタ表示中の一時停止（GetStageEditor().IsVisible()）分岐はBaseScene::Tick()が面倒を見る
    // （表示中はこのUpdate()自体が呼ばれずRefreshVisualTransformsForEditor()が代わりに呼ばれる）

    SceneShared::UpdateWeaponCycle(input_, weaponManager_, weaponCycleTimer_);
    UpdatePlayerAndBullets();
    UpdateCameraAndEnvironment();

#ifdef _DEBUG
    testGraphRuntime_.Update(TimeManager::GetInstance()->GetDeltaTime());
#endif

    bool nearWarp = SceneShared::UpdatePortalTransition(input_, player_->GetPosition(), kWarpX, kWarpProximity, "BATTLETEST");
    DrawHud(nearWarp);
}

void TrainingScene::RefreshVisualTransformsForEditor()
{
    // ステージエディタ表示中はゲームプレイ（プレイヤー操作・カメラ追従）を丸ごと止める
    // （BattleTestSceneと同じ規約。TimeManagerのタイムスケールだけでは
    // このシーンの各種Updateが固定dtで動いてしまい止まらないため、BaseScene::Tick()がUpdate()の代わりにこちらを呼ぶ）
    player_->RefreshVisualTransforms();
    for (auto& b : borderBlocks_) {
        b->Update();
    }
}

void TrainingScene::UpdatePlayerAndBullets()
{
    // 乱舞用ダミーターゲット（正面 8 ユニット先）
    {
        const Vector3& pp = player_->GetPosition();
        player_->Update(input_, { pp.x + player_->GetLastDirX() * 8.0f, pp.y, 0.0f });
    }

    // ── スペースキー スピン連射 ──────────────────────────────────────
    if (player_->JustSpinShot()) {
        constexpr float kBulletSpeed = 0.30f;
        const Vector3& spawnPos = player_->GetPosition();
        Vector3 firePos = { spawnPos.x, spawnPos.y, 0.0f };

        if (player_->IsUpsideDown()) {
            // 逆さ: 下方向中心に 5 方向ばらまき
            constexpr float kBaseAngle = 270.0f * (3.14159265f / 180.0f); // 真下
            constexpr float kSpread = 30.0f * (3.14159265f / 180.0f); // 30°間隔
            for (int i = -2; i <= 2; ++i) {
                float angle = kBaseAngle + i * kSpread;
                bulletPool_.Spawn(firePos, { std::cos(angle) * kBulletSpeed, std::sin(angle) * kBulletSpeed, 0.0f });
            }
            TimeManager::GetInstance()->RequestHitStop(3);
            ScreenFlash::GetInstance()->Request({ 1.0f, 0.7f, 0.1f, 0.55f }, 0.10f);
        } else {
            // 通常: 向いている方向に 1 発
            bulletPool_.Spawn(firePos, { player_->GetLastDirX() * kBulletSpeed, 0.0f, 0.0f });
        }
    }
    bulletPool_.Update();

    // ── フィニッシャースラッシュ（ゲージ満タン消費）───────────────────
    if (player_->JustFinisherSlash()) {
        TimeManager::GetInstance()->RequestHitStop(GameConstants::kHitStopFinisherSlash);
        ScreenFlash::GetInstance()->Request({ 0.75f, 0.95f, 1.0f, 0.65f }, GameConstants::kShakeFinisherSlashDur);

        static std::mt19937 rng { std::random_device { }() };
        std::uniform_real_distribution<float> angleDist(0.0f, GameConstants::kTwoPi);
        std::uniform_real_distribution<float> offXDist(-GameConstants::kCameraHalfW, GameConstants::kCameraHalfW);
        std::uniform_real_distribution<float> offYDist(-GameConstants::kCameraHalfH, GameConstants::kCameraHalfH);
        std::uniform_real_distribution<float> lenDist(4.0f, 9.0f);
        const Vector3& cam = camera_->GetTranslate();
        for (int i = 0; i < GameConstants::kFinisherSlashLines; ++i) {
            const float ang = angleDist(rng);
            const Vector2 dir = { std::cos(ang), std::sin(ang) };
            const Vector2 center = { cam.x + offXDist(rng), cam.y + offYDist(rng) };
            const float len = lenDist(rng);
            SceneShared::SpawnSlashMarkWorld(
                { center.x - dir.x * len, center.y - dir.y * len },
                { center.x + dir.x * len, center.y + dir.y * len },
                cam.x, cam.y, { 0.75f, 0.95f, 1.0f, 1.0f }, 5.0f, 0.22f);
        }
    }
    SlashMark::GetInstance()->Update(GameConstants::kFrameDeltaTime);
}

void TrainingScene::UpdateCameraAndEnvironment()
{
    SceneShared::UpdateCameraFollow(camera_.get(), player_->GetPosition());

#ifdef _DEBUG
    GpuProfiler::GetInstance()->ReadBack();
#endif

    shadowManager_->Update(objectCommon_->GetLightDirection());
    Object3d::SetLightViewProjection(shadowManager_->GetLightViewProjection());
    // GetStageEditor().UpdateObjects()はBaseScene::Tick()がUpdate()の後に一括して呼ぶ
    for (auto& b : borderBlocks_) {
        b->Update();
    }

    for (int i = 0; i < 3; ++i) {
        pbrDemoBlocks_[i]->Update();
    }

    warpPulseTimer_ += GameConstants::kFrameDeltaTime;
    float pulse = 0.6f + 0.4f * std::sin(warpPulseTimer_ * 4.0f);
    for (auto& p : warpPortalBlocks_) {
        p->SetColor({ 0.1f * pulse, 0.9f * pulse, 1.0f * pulse, 0.85f });
        p->Update();
    }
}

void TrainingScene::DrawHud(bool nearWarpPortal)
{
    DrawWeaponHud(nearWarpPortal);
    DrawDebugHud();
    SceneShared::DrawControlsHud(fontRenderer_, L": Warp (portal)");
    SceneShared::DrawAwakenGaugeHud(fontRenderer_, awakenGaugeBg_.get(), awakenGaugeFg_.get(),
        player_->GetAwakenGauge(), player_->IsAwakened(), warpPulseTimer_);
}

void TrainingScene::DrawWeaponHud(bool nearWarpPortal)
{
    constexpr float kScale = 1.5f;

    SceneShared::DrawWeaponListHud(fontRenderer_, weaponManager_, L"トレーニングルーム");

    // ワープラベル（ポータルの上）
    if (nearWarpPortal) {
        const Vector3& cam = camera_->GetTranslate();
        float sx, sy;
        SceneShared::WorldToScreen(kWarpX, 5.0f, cam.x, cam.y, sx, sy);
        constexpr Vector4 kColorWarp = { 0.2f, 1.0f, 1.0f, 1.0f };
        fontRenderer_.DrawString("[ ENTER ] Warp", sx - 84.0f, sy - 36.0f, kScale, kColorWarp);
    }
}

void TrainingScene::DrawDebugHud()
{
    // ── デバッグ情報（右上隅） ──────────────────────────────────────
#ifdef _DEBUG
    {
        char dbgBuf[64];
        float fps = DebugProfiler::GetInstance()->GetFPS();
        float ms = DebugProfiler::GetInstance()->GetMs();
        std::snprintf(dbgBuf, sizeof(dbgBuf), "%.0f FPS  %.2f ms", fps, ms);
        fontRenderer_.DrawString(dbgBuf, 1140.0f, 4.0f, 1.2f, { 0.6f, 1.0f, 0.6f, 0.85f });
    }
#endif

#ifdef USE_IMGUI
    // ── PBR マテリアルエディタ ────────────────────────────────────────
    ImGui::SetNextWindowSize(ImVec2(260, 165), ImGuiCond_Once);
    ImGui::SetNextWindowPos(ImVec2(10, 400), ImGuiCond_Once);
    if (ImGui::Begin("PBR Material Demo")) {
        static const char* kLabels[3] = { "Plastic (non-metal)", "Mirror Metal", "Rough Metal" };
        for (int i = 0; i < 3; ++i) {
            ImGui::PushID(i);
            if (ImGui::CollapsingHeader(kLabels[i])) {
                ImGui::SliderFloat("Metallic", &pbrMetallic_[i], 0.0f, 1.0f);
                ImGui::SliderFloat("Roughness", &pbrRoughness_[i], 0.0f, 1.0f);
                pbrDemoBlocks_[i]->SetMetallic(pbrMetallic_[i]);
                pbrDemoBlocks_[i]->SetRoughness(pbrRoughness_[i]);
            }
            ImGui::PopID();
        }
    }
    ImGui::End();

    // ── プロファイラ ───────────────────────────────────────────────────
    GpuProfiler::GetInstance()->DrawImGui();
#endif
}

void TrainingScene::Draw()
{
    ID3D12GraphicsCommandList* cmd = dxCommon_->GetCommandList();
    auto* gpuProfiler = GpuProfiler::GetInstance();

    // ---- シャドウパス ----
    gpuProfiler->BeginScope(GpuProfiler::Shadow, cmd);
    shadowManager_->BeginShadowPass(cmd);
    modelCommon_->BeginShadowPass();
    for (int i = 0; i < 3; ++i) {
        pbrDemoBlocks_[i]->DrawShadow();
    }
    shadowManager_->EndShadowPass(cmd);
    gpuProfiler->EndScope(GpuProfiler::Shadow, cmd);

    // ---- SSAO ノーマルキャプチャパス ----
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

    // ---- メイン3D描画 ----
    gpuProfiler->BeginScope(GpuProfiler::Main3D, cmd);
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = dxCommon_->GetCurrentBackBufferHandle();
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = dxCommon_->GetDsvHandle();
    cmd->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    D3D12_VIEWPORT vp = { 0, 0,
        static_cast<float>(WinApp::kClientWidth), static_cast<float>(WinApp::kClientHeight),
        0.0f, 1.0f };
    D3D12_RECT scissor = { 0, 0, WinApp::kClientWidth, WinApp::kClientHeight };
    cmd->RSSetViewports(1, &vp);
    cmd->RSSetScissorRects(1, &scissor);

    modelCommon_->CommonDrawSettings();
    objectCommon_->SetDefaultLight(cmd);
    shadowManager_->SetShadowMap(cmd, srvManager_);

    // GetStageEditor().DrawObjects()はBaseScene::Render()がDraw()の後に自動で呼ぶ
    for (auto& b : borderBlocks_) {
        b->Draw();
    }
    for (auto& p : warpPortalBlocks_) {
        p->Draw();
    }
    for (int i = 0; i < 3; ++i) {
        pbrDemoBlocks_[i]->Draw();
    }
    bulletPool_.Draw();
    player_->Draw();

    // ---- SSAO 計算 → ブラー → 乗算合成 ----
    if (ssao->IsEnabled()) {
        ssao->Compute(dxCommon_, camera_.get());
        ssao->Blur(dxCommon_);
        cmd->OMSetRenderTargets(1, &rtv, FALSE, &dsv); // バックバッファに戻す
        ssao->Apply(dxCommon_, srvManager_);
    }
    gpuProfiler->EndScope(GpuProfiler::Main3D, cmd);

    // ---- GPU タイムスタンプ解決（PostDraw 後の ReadBack で取得） ----
    gpuProfiler->Resolve(cmd);

    // ---- 2D スプライト（テキスト UI） ----
    spriteCommon_->CommonDrawSettings();
    shadowManager_->SetShadowMap(cmd, srvManager_);
    awakenGaugeBg_->Draw();
    if (player_->GetAwakenGauge() > 0.0f) {
        awakenGaugeFg_->Draw();
    }
    SlashMark::GetInstance()->Draw();
    fontRenderer_.Draw();
}

void TrainingScene::Finalize()
{
    SlashMark::GetInstance()->Clear();
    GpuProfiler::GetInstance()->Finalize();
}
