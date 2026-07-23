/**
 * @file TrainingScene.cpp
 * @brief TrainingSceneのゲームシーンの初期化、更新、描画、遷移に関する具体的な処理を実装するファイル
 */
#include "TrainingScene.h"
#include "AudioBridge.h"
#include "BorderBlockBuilder.h"
#include "FrameProfiler.h"
#include "GameConstants.h"
#include "PlayerBridge.h"
#include "SSAOEffect.h"
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
    spriteCommon_ = InitializeCommonResources(dxCommon, input, audio, dxCommon_, input_, audio_);

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
    camera_->SetTranslate({ 14.5f, 6.0f, -24.0f });
    Object3d::SetCommonCamera(camera_.get());

    modelBlock_ = std::make_unique<Model>();
    modelBlock_->Initialize(modelCommon_.get(),
        "Resources/block/block.obj",
        "Resources/block/block.png");

    BuildBorderBlocks(modelCommon_.get(), modelBlock_.get(), borderBlocks_);

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
        GetStageEditor().RegisterExternalObject(
            "Background Building " + std::to_string(cityBackgroundObjects_.size() + 1), city.get());
        cityBackgroundObjects_.push_back(std::move(city));
    }

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
    player_->SetHorizontalBounds(2.5f, 27.5f);
    // Open()/RegisterExternalEntity("Player")はGetEditorLevelPath()等のフック経由でBaseScene::Init()が自動で行う
    // （未作成のtraining.jsonなら空のまま起動し、F2エディタの+で配置してSaveで作成できる）
    PlayerBridge::GetInstance()->SetPlayer(player_.get());
    AudioBridge::GetInstance()->SetAudio(audio_);

    bulletPool_.Initialize(modelCommon_.get(), modelBlock_.get());

    struct PickupAsset {
        WeaponType type;
        const char* modelPath;
        const char* texturePath;
        float scale;
    };
    static constexpr PickupAsset kPickupAssets[] = {
        { WeaponType::Sword, "Resources/Knight/OBJ/Sword.obj", "Resources/Knight/OBJ/SwordPalette.png", 0.28f },
        { WeaponType::Spear, "Resources/MedievalWeaponsPack/OBJ/Spear.obj", "Resources/MedievalWeaponsPack/OBJ/SpearPalette.png", 0.13f },
        { WeaponType::Hammer, "Resources/MedievalWeaponsPack/OBJ/Hammer_Small.obj", "Resources/MedievalWeaponsPack/OBJ/Hammer_SmallPalette.png", 0.27f },
        { WeaponType::Dagger, "Resources/MedievalWeaponsPack/OBJ/Dagger.obj", "Resources/MedievalWeaponsPack/OBJ/DaggerPalette.png", 0.42f },
        { WeaponType::Greatsword, "Resources/MedievalWeaponsPack/OBJ/Claymore.obj", "Resources/MedievalWeaponsPack/OBJ/ClaymorePalette.png", 0.20f },
        { WeaponType::Scythe, "Resources/MedievalWeaponsPack/OBJ/Scythe.obj", "Resources/MedievalWeaponsPack/OBJ/ScythePalette.png", 0.22f },
        { WeaponType::Axe, "Resources/MedievalWeaponsPack/OBJ/Axe_Double.obj", "Resources/MedievalWeaponsPack/OBJ/Axe_DoublePalette.png", 0.22f },
    };
    for (int i = 0; i < static_cast<int>(weaponPickups_.size()); ++i) {
        const PickupAsset& asset = kPickupAssets[i];
        WeaponPickup& pickup = weaponPickups_[i];
        pickup.type = asset.type;
        pickup.position = { 4.5f + static_cast<float>(i) * 3.0f, 3.0f, 0.0f };
        pickup.model = std::make_unique<Model>();
        pickup.model->Initialize(modelCommon_.get(), asset.modelPath, asset.texturePath);
        pickup.object = std::make_unique<Object3d>();
        pickup.object->Initialize(modelCommon_.get());
        pickup.object->SetModel(pickup.model.get());
        pickup.object->SetPosition(pickup.position);
        pickup.object->SetScale({ asset.scale, asset.scale, asset.scale });
        pickup.object->SetEnableLighting(true);
        pickup.object->Update();
    }

    awakenGaugeBg_ = std::make_unique<Sprite>();
    awakenGaugeBg_->Initialize(spriteCommon_.get(), "Resources/white.png");
    awakenGaugeBg_->SetColor({ 0.05f, 0.05f, 0.15f, 0.75f });

    awakenGaugeFg_ = std::make_unique<Sprite>();
    awakenGaugeFg_->Initialize(spriteCommon_.get(), "Resources/white.png");

    fontRenderer_.Initialize(spriteCommon_.get());
    SlashMark::GetInstance()->Initialize(spriteCommon_.get());

    SSAOEffect::GetInstance()->Initialize(dxCommon_, srvManager_);

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
    fontRenderer_.Reset();

    if (input_->TriggerKey(DIK_BACK)) {
        SceneManager::GetInstance()->ChangeScene("TITLE", 0.4f, 0.4f);
        return;
    }

    // ステージエディタ表示中の一時停止（GetStageEditor().IsVisible()）分岐はBaseScene::Tick()が面倒を見る
    // （表示中はこのUpdate()自体が呼ばれずRefreshVisualTransformsForEditor()が代わりに呼ばれる）

    SceneShared::UpdateWeaponCycle(input_, weaponManager_, weaponCycleTimer_);
    UpdatePlayerAndBullets();
    UpdateWeaponPickups();
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
    for (auto& city : cityBackgroundObjects_) {
        city->Update();
    }
    for (auto& portal : warpPortalBlocks_) {
        portal->Update();
    }
    for (auto& pickup : weaponPickups_) {
        pickup.object->Update();
    }
}

void TrainingScene::UpdatePlayerAndBullets()
{
    // 乱舞用ダミーターゲット（正面 8 ユニット先）
    {
        const Vector3& pp = player_->GetPosition();
        player_->Update(input_, { pp.x + player_->GetLastDirX() * 8.0f, pp.y, 0.0f });
        player_->ResolveBlockCollision(GetStageEditor().GetSolidColliders());
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
    for (auto& city : cityBackgroundObjects_) {
        city->Update();
    }

    warpPulseTimer_ += GameConstants::kFrameDeltaTime;
    float pulse = 0.6f + 0.4f * std::sin(warpPulseTimer_ * 4.0f);
    for (auto& p : warpPortalBlocks_) {
        p->SetColor({ 0.1f * pulse, 0.9f * pulse, 1.0f * pulse, 0.85f });
        p->Update();
    }
}

void TrainingScene::UpdateWeaponPickups()
{
    weaponPickupPulse_ += GameConstants::kFrameDeltaTime;
    const Vector3& playerPosition = player_->GetPosition();
    for (int i = 0; i < static_cast<int>(weaponPickups_.size()); ++i) {
        WeaponPickup& pickup = weaponPickups_[i];
        const float dx = playerPosition.x - pickup.position.x;
        const float dy = playerPosition.y - pickup.position.y;
        const bool touching = dx * dx + dy * dy <= 1.0f;
        if (touching && !pickup.wasTouching) {
            weaponManager_->EquipForTraining(pickup.type);
            ScreenFlash::GetInstance()->Request({ 0.55f, 0.9f, 1.0f, 0.22f }, 0.08f);
        }
        pickup.wasTouching = touching;

        const float hover = std::sin(weaponPickupPulse_ * 2.5f + static_cast<float>(i)) * 0.15f;
        pickup.object->SetPosition({ pickup.position.x, pickup.position.y + hover, pickup.position.z });
        pickup.object->SetRotation({ 0.2f, weaponPickupPulse_ * 0.8f, 0.0f });
        pickup.object->SetColor(touching
                ? Vector4 { 1.0f, 1.0f, 1.0f, 1.0f }
                : Vector4 { 0.75f, 0.85f, 1.0f, 1.0f });
        pickup.object->Update();
    }
}

void TrainingScene::DrawHud(bool nearWarpPortal)
{
    DrawWeaponHud(nearWarpPortal);
    DrawDebugHud();
    SceneShared::DrawControlsHud(fontRenderer_, L": バトルテストへ移動");
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
        fontRenderer_.DrawStringW(L"[ ENTER ] バトルテストへ", sx - 110.0f, sy - 36.0f, kScale, kColorWarp);
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
        fontRenderer_.DrawString(dbgBuf, 1140.0f, 4.0f, 1.2f, { 0.6f, 1.0f, 0.6f, 0.85f });
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

    for (auto& b : borderBlocks_) {
        b->Draw();
    }
    for (auto& city : cityBackgroundObjects_) {
        city->Draw();
    }
    for (auto& p : warpPortalBlocks_) {
        p->Draw();
    }
    for (auto& pickup : weaponPickups_) {
        pickup.object->Draw();
    }
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
        ssao->Apply(dxCommon_, srvManager_);
    }
    gpuProfiler->EndScope(GpuProfiler::Main3D, cmd);

    // GPU タイムスタンプ解決（PostDraw 後の ReadBack で取得）
    gpuProfiler->Resolve(cmd);

    // 2D スプライト（テキスト UI）
    spriteCommon_->CommonDrawSettings();
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
