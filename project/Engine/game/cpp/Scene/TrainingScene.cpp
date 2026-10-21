/**
 * @file TrainingScene.cpp
 * @brief アクション操作練習用シーン（TrainingScene）の初期化と基本更新フローの実装
 */
#include "TrainingScene.h"
#include "SceneVisualTuning.h"
#include "HitEffectLibrary.h"
#include "CombatTuning.h"
#include "TextBufferCapacity.h"
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
using namespace engine::graphics;

namespace engine::game {

// バトルテストへのワープポータル
static constexpr Vector2 kWarpLabelOffset = { 110.0f, 36.0f };
static constexpr const char* kLayoutName = "training";

// デバッグ表示
static constexpr Vector2 kDebugTextPosition = { 1140.0f, 4.0f };

/** @brief シーンの初期化プレイヤー・ステージ・UI を構築する */
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
    /** @brief 唯一のEventBusインスタンスを取得する（未生成なら生成する） */
    EventBus::GetInstance()->Subscribe("low_hp_warning", [] {
        Logger::LogInfo("[GraphTest] low_hp_warning fired");
    });
    /** @brief 唯一のEventBusインスタンスを取得する（未生成なら生成する） */
    EventBus::GetInstance()->Subscribe("hp_checked", [] {
        Logger::LogInfo("[GraphTest] hp_checked fired");
    });
    testGraphRuntime_.Start(&testGraph_);
#endif
}

/** @brief Initialize()の下請け マネージャ取得・各種Common初期化・カメラ生成を行う */
void TrainingScene::InitializeCoreSystems()
{
    srvManager_ = SrvManager::GetInstance();
    weaponManager_ = WeaponManager::GetInstance();

    BuildRenderFoundation(dxCommon_, srvManager_, "training", { modelCommon_, objectCommon_, shadowManager_, camera_ });
}

/** @brief Initialize()の下請け 境界ブロック・ワープポータルを初期化する（背景のビルはtraining.jsonに置く） */
void TrainingScene::InitializeStageModels()
{
    modelBlock_ = ModelManager::GetInstance()->GetOrLoad(modelCommon_.get(),
        "Resources/block/block.obj",
        "Resources/block/block.png");

    BuildBorderBlocks(modelCommon_.get(), modelBlock_, borderBlocks_);

    // 背景のビルはレベルJSON（Resources/Levels/training.json）に置き、F2のステージエディタで編集する

    SceneShared::CreateWarpPortalBlocks(modelCommon_.get(), modelBlock_,
        { CombatTune().trainingWarpX, CombatTune().trainingGroundY, 0.0f }, SceneVisualTuning::Training().warpPortalBlockCount,
        SceneVisualTuning::Training().warpPortalBlockSpacing, SceneVisualTuning::Training().warpPortalColor, warpPortalBlocks_);
}

/** @brief Initialize()の下請け プレイヤーと弾丸プールを初期化する */
void TrainingScene::InitializePlayerAndBullets()
{
    // 水平方向の移動範囲は固定値で決め打ちしない壁ブロックの当たり判定（ResolveBlockCollision）が
    // StageEditorでの編集をそのまま反映するので、それ自体が境界として機能する
    // Open()/RegisterExternalEntity("Player")はGetEditorLevelPath()等のフック経由でBaseScene::Init()が自動で行う
    // （未作成のtraining.jsonなら空のまま起動し、F2エディタの+で配置してSaveで作成できる）
    SceneShared::InitializePlayerAndBullets(player_, bulletPool_, modelCommon_.get(), modelBlock_, audio_);
}

/** @brief Initialize()の下請け 配置武器ピックアップ（アセット表に基づく実体生成）を初期化する */
void TrainingScene::InitializeWeaponPickups()
{
    weaponPickups_.Initialize(modelCommon_.get());
}

/** @brief Initialize()の下請け 覚醒ゲージUI・フォント・斬撃線・SSAO・プロファイラを初期化する */
void TrainingScene::InitializeHudAndEffects()
{

    fontRenderer_.Initialize(spriteCommon_.get());
    pauseMenu_.Initialize(spriteCommon_.get(), &fontRenderer_, audio_, kLayoutName,
        {
            "QUIT TO TITLE", [] {
                SceneFlow::GetInstance()->Transition("TRAINING", "title", "TITLE");
            }
        });
    InitializeWeaponSlotHud();
    SlashMark::GetInstance()->Initialize(spriteCommon_.get());

    SSAOEffect::GetInstance()->Initialize(dxCommon_, srvManager_);

    GpuProfiler::GetInstance()->Initialize(dxCommon_);
}

/** @brief 武器スロットのHUDを用意する */
void TrainingScene::InitializeWeaponSlotHud()
{
    hud_.Add(std::make_unique<TrainingHud>());
    hud_.Add(std::make_unique<AwakenGaugeHud>());
    hud_.Add(std::make_unique<WeaponSlotHud>(true));
    hud_.Initialize({ *spriteCommon_, *modelCommon_, *dxCommon_, *weaponManager_ });
    UpdateWeaponSlotHud();
}

/** @brief 武器スロットのHUDを今の装備に合わせて更新する */
void TrainingScene::UpdateWeaponSlotHud()
{
    HudFrame frame { *camera_, GameConstants::kFrameDeltaTime, player_->GetAwakenGauge(), player_->IsAwakened(), warpPulseTimer_ };
    hud_.Update(frame);
}

/** @brief 武器スロットのHUDを描画する */
void TrainingScene::DrawWeaponSlotHud()
{
    hud_.Draw();
}

/** @brief 入力・物理・パーティクルを毎フレーム更新する */
void TrainingScene::Update()
{
    fontRenderer_.Reset();

    // 一時停止中はゲームを止める（HUDは描画側で隠す）。Backspaceもポーズを閉じるのに使うため先に判定する
    const bool paused = pauseMenu_.Update(input_);
    // 配置物と敵は BaseScene が Update の後に更新するので、ポーズ中はそちらも止める
    GetStageEditor().SetGameplayHeld(paused);
    if (paused)
    {
        return;
    }

    if (input_->TriggerKey(DIK_BACK))
    {
        audio_->PlayMenuSelect();
        SceneFlow::GetInstance()->Transition("TRAINING", "title", "TITLE");
        return;
    }
    if (input_->TriggerKey(DIK_TAB) || input_->TriggerButton(XINPUT_GAMEPAD_BACK))
    {
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

    bool nearWarp = SceneShared::UpdatePortalTransition(input_, player_->GetPosition(), CombatTune().trainingWarpX, CombatTune().trainingWarpProximity, "TRAINING", "battle_test", "BATTLETEST", audio_);
    DrawHud(nearWarp);
}

/** @brief エディタ表示中（ゲームプレイ停止中）にプレイヤーの見た目だけ追従させる */
void TrainingScene::RefreshVisualTransformsForEditor()
{
    fontRenderer_.Reset();
    // ステージエディタ表示中はゲームプレイ（プレイヤー操作・カメラ追従）を丸ごと止める
    // （BattleTestSceneと同じ規約。TimeManagerのタイムスケールだけでは
    // このシーンの各種Updateが固定dtで動いてしまい止まらないため、BaseScene::Tick()がUpdate()の代わりにこちらを呼ぶ）
    player_->RefreshVisualTransforms();
    for (auto& b : borderBlocks_)
    {
        b->Update();
    }
    for (auto& portal : warpPortalBlocks_)
    {
        portal->Update();
    }
    weaponPickups_.RefreshVisuals();
    UpdateWeaponSlotHud();
    DrawHud(false);
}

/** @brief プレイヤーとスピン連射弾を更新する */
void TrainingScene::UpdatePlayerAndBullets()
{
    // 乱舞用ダミーターゲット（正面 8 ユニット先）
    {
        const Vector3& pp = player_->GetPosition();
        const auto stageColliders = GetStageEditor().GetSolidColliders();
        player_->Update(input_, { pp.x + player_->GetLastDirX() * CombatTune().trainingFacingTargetDistance, pp.y, 0.0f }, false, !stageColliders.empty());
        player_->ResolveBlockCollision(stageColliders);
        player_->RefreshVisualTransforms();

        // 境界ブロックは描画専用なので、幅1のプレイヤー判定が
        // 中心座標 x=2 と x=28 の壁の内側に収まるよう補正する
    }

    // ── スペースキー スピン連射 ──────────────────────────────────────
    SceneShared::UpdateSpinShotFire(player_.get(), bulletPool_);
    bulletPool_.Update();

    // ── フィニッシャースラッシュ（ゲージ満タン消費）───────────────────
    if (player_->JustFinisherSlash())
    {
        TimeManager::GetInstance()->RequestHitStop(HitEffectLibrary::GetInstance()->SceneParticles().hitStopFinisherSlash);
        ScreenFlash::GetInstance()->Request(SceneVisualTuning::Training().finisherFlashColor, HitEffectLibrary::GetInstance()->SceneParticles().shakeFinisherSlashDur);

        static std::mt19937 rng { std::random_device { }() };
        std::uniform_real_distribution<float> angleDist(0.0f, GameConstants::kTwoPi);
        std::uniform_real_distribution<float> offXDist(-GameConstants::kCameraHalfW, GameConstants::kCameraHalfW);
        std::uniform_real_distribution<float> offYDist(-GameConstants::kCameraHalfH, GameConstants::kCameraHalfH);
        std::uniform_real_distribution<float> lenDist(SceneVisualTuning::Training().slashLineLengthMin, SceneVisualTuning::Training().slashLineLengthMax);
        const Vector3& cam = camera_->GetTranslate();
        for (int i = 0; i < CombatTune().finisherSlashLines; ++i)
        {
            const float ang = angleDist(rng);
            const Vector2 dir = { std::cos(ang), std::sin(ang) };
            const Vector2 center = { cam.x + offXDist(rng), cam.y + offYDist(rng) };
            const float len = lenDist(rng);
            SceneShared::SpawnSlashMarkWorld(
                { center.x - dir.x * len, center.y - dir.y * len },
                { center.x + dir.x * len, center.y + dir.y * len },
                cam.x, cam.y, SceneVisualTuning::Training().slashLineColor, SceneVisualTuning::Training().slashLineThickness, SceneVisualTuning::Training().slashLineSeconds);
        }
    }
    SlashMark::GetInstance()->Update(GameConstants::kFrameDeltaTime);
}

/** @brief カメラ追従、影、境界ブロック、PBRデモ、ワープポータル演出を更新する */
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
    for (auto& b : borderBlocks_)
    {
        b->Update();
    }

    warpPulseTimer_ += GameConstants::kFrameDeltaTime;
    float pulse = SceneVisualTuning::Training().warpPulseBase + SceneVisualTuning::Training().warpPulseAmplitude * std::sin(warpPulseTimer_ * SceneVisualTuning::Training().warpPulseSpeed);
    for (auto& p : warpPortalBlocks_)
    {
        p->SetColor({ SceneVisualTuning::Training().warpPortalColor.x * pulse, SceneVisualTuning::Training().warpPortalColor.y * pulse, SceneVisualTuning::Training().warpPortalColor.z * pulse, SceneVisualTuning::Training().warpPulseAlpha });
        p->Update();
    }
}

/** @brief 配置された武器へ触れたときの装備切り替えを更新する */
void TrainingScene::UpdateWeaponPickups()
{
    if (weaponPickups_.Update(player_->GetPosition(), *weaponManager_, GameConstants::kFrameDeltaTime))
    {
        hud_.Notify(HudEvent::WeaponAcquired);
        ScreenFlash::GetInstance()->Request(SceneVisualTuning::Training().pickupFlashColor, SceneVisualTuning::Training().pickupFlashSeconds);
    }
}

/** @brief HUD全体（武器一覧・デバッグ情報・操作説明・覚醒ゲージ）を描画する */
void TrainingScene::DrawHud(bool nearWarpPortal)
{
    hud_.QueueText(fontRenderer_);
    DrawWeaponHud(nearWarpPortal);
    DrawDebugHud();
}

/** @brief 武器一覧とワープポータルのラベルを描画する */
void TrainingScene::DrawWeaponHud(bool nearWarpPortal)
{
    // ワープラベル（ポータルの上）
    if (nearWarpPortal)
    {
        const Vector3& cam = camera_->GetTranslate();
        float sx, sy;
        SceneShared::WorldToScreen(CombatTune().trainingWarpX, SceneVisualTuning::Training().warpLabelWorldY, cam.x, cam.y, sx, sy);
        UILayout& layout = UILayout::Get(kLayoutName);
        fontRenderer_.DrawStringW(input_->ExpandPrompts(L"[ {Interact} ] バトルテストへ"), sx - kWarpLabelOffset.x, sy - kWarpLabelOffset.y,
            layout.Float("portal.label_scale", SceneVisualTuning::Training().warpLabelScale), layout.Color("portal.label_color", SceneVisualTuning::Training().warpLabelColor));
    }
}

/** @brief デバッグ情報（FPS、PBRマテリアルエディタ、プロファイラ）を描画する */
void TrainingScene::DrawDebugHud()
{
    // ── デバッグ情報（右上隅） ──────────────────────────────────────
#ifdef _DEBUG
    {
        char dbgBuf[TextBufferCapacity::kIdentifier];
        float fps = FrameProfiler::GetInstance()->GetFPS();
        float ms = FrameProfiler::GetInstance()->GetMs();
        std::snprintf(dbgBuf, sizeof(dbgBuf), "%.0f FPS  %.2f ms", fps, ms);
        fontRenderer_.DrawString(dbgBuf, kDebugTextPosition.x, kDebugTextPosition.y, SceneVisualTuning::Training().debugTextScale, SceneVisualTuning::Training().debugTextColor);
    }
#endif

#ifdef USE_IMGUI
    // ── プロファイラ ───────────────────────────────────────────────────
    GpuProfiler::GetInstance()->DrawImGui();
#endif
}

/** @brief ステージとプレイヤーを描画する */
void TrainingScene::Draw()
{
    ID3D12GraphicsCommandList* cmd = dxCommon_->GetCommandList();
    auto* gpuProfiler = GpuProfiler::GetInstance();

    // シャドウパス
    gpuProfiler->BeginScope(GpuProfiler::Shadow, cmd);
    shadowManager_->RunShadowPass(cmd, modelCommon_.get());
    gpuProfiler->EndScope(GpuProfiler::Shadow, cmd);

    // SSAO ノーマルキャプチャパス
    auto* ssao = SSAOEffect::GetInstance();
    gpuProfiler->BeginScope(GpuProfiler::SSAO, cmd);
    if (ssao->IsEnabled())
    {
        ssao->BeginNormalCapture(dxCommon_, camera_.get());
        for (auto& b : borderBlocks_)
        {
            b->DrawForNormalCapture();
        }
        for (auto& p : warpPortalBlocks_)
        {
            p->DrawForNormalCapture();
        }
        ssao->EndNormalCapture(dxCommon_);
    }
    gpuProfiler->EndScope(GpuProfiler::SSAO, cmd);

    // メイン3D描画
    gpuProfiler->BeginScope(GpuProfiler::Main3D, cmd);
    dxCommon_->SetBackBufferRenderTarget();

    modelCommon_->CommonDrawSettings();
    objectCommon_->SetDefaultLight(cmd);
    shadowManager_->SetShadowMap(cmd, srvManager_);

    for (auto& b : borderBlocks_)
    {
        b->Draw();
    }
    for (auto& p : warpPortalBlocks_)
    {
        p->Draw();
    }
    weaponPickups_.Draw();
    bulletPool_.Draw();
    player_->Draw();

    // ステージエディタの配置ブロックはここで描く（HUDテキストより前＝ブロックがUIパネルに重ならないように）
    // BaseScene::Render()側の自動呼び出しはWasObjectsDrawnThisFrame()で自動的にスキップされる
    GetStageEditor().DrawObjects();

    // SSAO 計算 → ブラー → 乗算合成
    if (ssao->IsEnabled())
    {
        ssao->Compute(dxCommon_, camera_.get());
        ssao->Blur(dxCommon_);
        dxCommon_->SetBackBufferRenderTarget(); // バックバッファに戻す
        ssao->Apply(dxCommon_, srvManager_);
    }
    gpuProfiler->EndScope(GpuProfiler::Main3D, cmd);

    // GPU タイムスタンプ解決（PostDraw 後の ReadBack で取得）
    gpuProfiler->Resolve(cmd);

    // 2D スプライト（テキスト UI）
    spriteCommon_->CommonDrawSettings();
    if (pauseMenu_.IsPaused())
    {
        // 一時停止中はHUDを隠してポーズメニューだけを見せる
        pauseMenu_.Draw(input_);
    }
    else
    {
        DrawWeaponSlotHud();
        SlashMark::GetInstance()->Draw();
        GetStageEditor().DrawUIText(fontRenderer_);
    }
    fontRenderer_.Draw();
}

/** @brief リソースを解放する */
void TrainingScene::Finalize()
{
    SlashMark::GetInstance()->Clear();
    GpuProfiler::GetInstance()->Finalize();
}

} // namespace engine::game
