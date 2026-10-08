/**
 * @file GamePlayScene.cpp
 * @brief メインステージ（GamePlayScene）の初期化と基本更新フローの実装
 */
#include "GamePlayScene.h"
#include "AudioBridge.h"
#include "GameConstants.h"
#include "GamePlaySceneInitializer.h"
#include "GameRules.h"
#include "SceneEffectBridge.h"
#include "SceneFlow.h"
#include "GrayscaleEffect.h"
#include "HsvFilter.h"
#include "ImGuiControl.h"
#include "ImageFilter.h"
#include "ModelManager.h"
#include "ParticleManager.h"
#include "PipelineStateGuard.h"
#include "PlayerBridge.h"
#include "PlaytestLog.h"
#include "PostEffectRenderTarget.h"
#include "RunData.h"
#include "SaveData.h"
#include "SceneManager.h"
#include "ScreenFlash.h"
#include "SlashMark.h"
#include "StageEditor.h"
#include "StringUtility.h"
#include "TextureManager.h"
#include "WeaponManager.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <random>
using namespace engine;
using namespace engine::graphics;
using namespace engine::game;

namespace {
constexpr const char* kSceneName = "GAMEPLAY"; // scene_flow.jsonのキー（SceneFactoryの登録名と同じ）
constexpr const char* kDefaultLevelPath = "Resources/Levels/level01.json";

constexpr Vector3 kInitialCameraPosition = { 19.0f, 6.0f, 0.0f }; // zはGameConstants::kCameraDistanceZを使う
constexpr Vector4 kHpBarBackgroundColor = { 0.2f, 0.2f, 0.2f, 0.8f };
constexpr float kFinisherShatterSeconds = 0.9f;
constexpr float kMinClipW = 0.0001f; // これ以下のw成分ではスクリーン座標へ変換しない（カメラの背後）

// 乱舞 打ち上げヒット
constexpr float kLaunchRingSpeed = 5.5f;
constexpr Vector4 kLaunchRingColor = { 1.0f, 0.55f, 0.1f, 1.0f };
constexpr int kLaunchRingCount = 20;
constexpr float kLaunchRingLifetime = 0.45f;
constexpr float kLaunchRingSize = 0.28f;
constexpr float kLaunchSparkSpreadX = 4.0f;
constexpr float kLaunchSparkRiseMin = 3.0f;
constexpr float kLaunchSparkRiseMax = 7.0f;
constexpr int kLaunchSparkCount = 10;
constexpr Vector4 kLaunchSparkColor = { 1.0f, 0.65f, 0.15f, 1.0f };
constexpr float kLaunchSparkLifetime = 0.8f;
constexpr float kLaunchSparkSize = 0.18f;

// 乱舞 ジャグルスラッシュ（回数を重ねるほど大きく、色を暖色へ寄せる）
constexpr float kJuggleSlashRadiusBase = 1.2f;
constexpr float kJuggleSlashRadiusStep = 0.12f;
constexpr float kJuggleSlashGreenFade = 0.06f;
constexpr float kJuggleSlashBlueFade = 0.09f;
constexpr float kJuggleRingSpeedBase = 2.5f;
constexpr float kJuggleRingSpeedStep = 0.2f;
constexpr Vector4 kJuggleRingColor = { 1.0f, 0.9f, 0.5f, 0.8f };
constexpr int kJuggleRingCount = 8;
constexpr float kJuggleRingLifetime = 0.25f;
constexpr float kJuggleRingSize = 0.15f;
constexpr float kJuggleShakeBase = 0.12f;
constexpr float kJuggleShakeStep = 0.01f;
constexpr float kJuggleShakeSeconds = 0.10f;

// 乱舞 フィニッシュ
constexpr float kFinishOuterRingSpeed = 8.0f;
constexpr Vector4 kFinishOuterRingColor = { 1.0f, 0.3f, 0.3f, 1.0f };
constexpr int kFinishOuterRingCount = 24;
constexpr float kFinishOuterRingLifetime = 0.5f;
constexpr float kFinishOuterRingSize = 0.35f;
constexpr float kFinishInnerRingSpeed = 5.0f;
constexpr Vector4 kFinishInnerRingColor = { 1.0f, 1.0f, 0.5f, 1.0f };
constexpr int kFinishInnerRingCount = 16;
constexpr float kFinishInnerRingLifetime = 0.45f;
constexpr float kFinishInnerRingSize = 0.30f;
constexpr float kFinishSparkSpreadX = 6.0f;
constexpr float kFinishSparkRiseMin = 4.0f;
constexpr float kFinishSparkRiseMax = 10.0f;
constexpr int kFinishSparkCount = 16;
constexpr float kFinishSparkGreenBase = 0.4f;
constexpr float kFinishSparkGreenStep = 0.04f;
constexpr float kFinishSparkBlue = 0.1f;
constexpr float kFinishSparkLifetime = 1.0f;
constexpr float kFinishSparkSize = 0.20f;

// フィニッシャー発動
constexpr float kFinisherLaunchRatio = 0.7f; // 通常の打ち上げ速度に対する比率
constexpr float kFinisherStartShakeAmount = 0.20f;
constexpr float kFinisherStartShakeSeconds = 0.15f;
constexpr float kFinisherStartWarpImpulse = 0.4f;
}

std::string GamePlayScene::GetEditorLevelPath() const
{
    return GameRules::GetInstance()->LevelPathForFloor(RunData::GetInstance()->GetFloor(), kDefaultLevelPath);
}

bool GamePlayScene::IsWaterFloor() const
{
    const int waterFloor = GameRules::GetInstance()->Get().waterFloor;
    return waterFloor >= 0 && RunData::GetInstance()->GetFloor() == waterFloor;
}

// 初期化

// ══════════════════════════════════════════════════════
// シーン初期化
// ══════════════════════════════════════════════════════

void GamePlayScene::Initialize(DirectXCommon* dxCommon, Input* input, Audio* audio)
{
    // 外部から受け取る共通依存だけを入口で保持し、所有資源の構築は下請けへ委譲する
    spriteCommon_ = InitializeCommonResources(dxCommon, input, audio, dxCommon_, input_, audio_);
    InitializeCoreSystems();
}

void GamePlayScene::InitializeCoreSystems()
{
    InitializeRenderFoundation();
    GamePlaySceneInitializer::InitializeStageActors(*this);
    InitializeRenderTargetsAndOverlays();
    InitializeParticlesWaterAndHud();
    InitializeGhostEditorAndEffects();
}

void GamePlayScene::InitializeRenderFoundation()
{
    // 3D描画基盤を先に構築し、後続のゲーム実体が安全にモデルを生成できる状態にする
    modelCommon_ = std::make_unique<ModelCommon>();
    modelCommon_->Initialize(dxCommon_);

    objectCommon_ = std::make_unique<Object3dCommon>();
    objectCommon_->Initialize(dxCommon_);

    srvManager_ = SrvManager::GetInstance();
    grayscaleEffect_ = GrayscaleEffect::GetInstance();
    imageFilter_ = ImageFilter::GetInstance();
    hsvFilter_ = HsvFilter::GetInstance();

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

    modelSkydome_ = ModelManager::GetInstance()->GetOrLoad(modelCommon_.get(),
        "Resources/SkyDome/SkyDome.obj",
        "Resources/SkyDome/skySphere.png");

    skydome_ = std::make_unique<Skydome>();
    skydome_->Initialize(modelCommon_.get(), modelSkydome_);
}

void GamePlayScene::InitializeRenderTargetsAndOverlays()
{
    renderTexture_ = std::make_unique<RenderTexture>();
    renderTexture_->Initialize(dxCommon_, srvManager_,
        WinApp::kClientWidth, WinApp::kClientHeight);

    renderTextureSprite_ = std::make_unique<Sprite>();
    renderTextureSprite_->Initialize(spriteCommon_.get(), "Resources/white.png");
    renderTextureSprite_->SetExternalTexture(renderTexture_->GetSrvIndex());
    renderTextureSprite_->SetPosition({ 0.0f, 0.0f });
    renderTextureSprite_->SetSize({ static_cast<float>(WinApp::kClientWidth),
        static_cast<float>(WinApp::kClientHeight) });

    clearBgSprite_ = std::make_unique<Sprite>();
    clearBgSprite_->Initialize(spriteCommon_.get(), "Resources/white.png");
    clearBgSprite_->SetPosition({ 0.0f, 0.0f });
    clearBgSprite_->SetSize({ static_cast<float>(WinApp::kClientWidth),
        static_cast<float>(WinApp::kClientHeight) });

    finisherOverlay_ = SceneShared::CreateFinisherOverlay(spriteCommon_.get());
}

void GamePlayScene::InitializeParticlesWaterAndHud()
{
    pm_ = ParticleManager::GetInstance();
    SceneShared::CreateParticleGroupsFromJson(pm_, "Resources/particles/gameplay.json");

    waterPool_ = std::make_unique<WaterPool>();
    waterPool_->Initialize(spriteCommon_.get());
    if (IsWaterFloor()) {
        player_->SetWaterLevel(WaterPool::GetSurfaceY());
    }

    fontRenderer_.Initialize(spriteCommon_.get());
    SlashMark::GetInstance()->Initialize(spriteCommon_.get());


    styleRankHud_.Initialize(spriteCommon_.get());

    // ボス頭上のHPバー（道中の武器敵ぶんはOnEditorLevelLoaded()でレベル読込のたびに生成する）
    bossHpBarBg_ = std::make_unique<Sprite>();
    bossHpBarBg_->Initialize(spriteCommon_.get(), "Resources/white.png");
    bossHpBarBg_->SetColor(kHpBarBackgroundColor);
    bossHpBarFg_ = std::make_unique<Sprite>();
    bossHpBarFg_->Initialize(spriteCommon_.get(), "Resources/white.png");

    // プレイヤーHPゲージ（左下、既存の数値表示のすぐ上に置く）
    playerHpBarBg_ = std::make_unique<Sprite>();
    playerHpBarBg_->Initialize(spriteCommon_.get(), "Resources/white.png");
    playerHpBarBg_->SetColor(kHpBarBackgroundColor);
    playerHpBarFg_ = std::make_unique<Sprite>();
    playerHpBarFg_->Initialize(spriteCommon_.get(), "Resources/white.png");

    // ボスAoEスラムの着弾予告円（円形グロー画像を赤く点滅させて範囲を示す）
    bossSlamWarningSprite_ = std::make_unique<Sprite>();
    bossSlamWarningSprite_->Initialize(spriteCommon_.get(), "Resources/Effects/circle2.png");

    InitializeWeaponSlotHud();
    SetupPauseMenu();
}

void GamePlayScene::InitializeGhostEditorAndEffects()
{
    ghostObject_ = std::make_unique<Object3d>();
    ghostObject_->Initialize(modelCommon_.get());
    ghostObject_->SetModel(player_->GetModel());
    ghostObject_->SetEnableLighting(false);

    sceneEditor_.LoadAll(BuildEditContext());

    // カメラスムージング用の初期目標値を現在のカメラ位置から取る
    cameraTargetPos_ = camera_->GetTranslate();
    cameraTargetRot_ = camera_->GetRotate();

    glassShatter_.Initialize(dxCommon_, srvManager_);
    enemySlice_.Initialize(dxCommon_);
    bladeFlash_.Initialize(dxCommon_);
    spaceWarp_.Initialize(dxCommon_, srvManager_);
    finisherShatter_.Initialize(dxCommon_, srvManager_);
    finisherShatter_.SetDuration(kFinisherShatterSeconds);

    ImGuiControlPanel::RegisterGlassShatterTrigger([this]() { TriggerGlassShatterTest(); });
    // ノードグラフのShakeCameraノードからこのシーンのカメラシェイクを呼べるようにする
    SceneEffectBridge::GetInstance()->SetCameraShakeHandler(
        [this](float amount, float seconds) { cameraShaker_.Request(amount, seconds); });
}

void GamePlayScene::RefreshVisualTransformsForEditor()
{
    if (player_) {
        player_->RefreshVisualTransforms();
    }
    if (enemy_) {
        enemy_->RefreshVisualTransforms();
    }
    for (auto& entry : weaponEnemies_) {
        if (entry.enemy) {
            entry.enemy->RefreshVisualTransforms();
        }
    }

    // ゲーム更新停止中も、編集カメラで描画する全3D実体のWVPだけは更新する。
    // （収集物・壊せる物はStageEditor所有の配置物なのでStageEditor::UpdateObjects()側が追従させる）
    if (skydome_) {
        skydome_->Update(camera_.get());
    }
    if (ghostObject_ && !ghostTrail_.empty()) {
        ghostObject_->Update();
    }

    // HUDの文字はUpdate()内で組み立てているため、エディタでUIを動かした結果を止まった画面にも反映する
    UpdateWeaponSlotHud();
    DrawStyleUI();
}

SceneEditor::EditContext GamePlayScene::BuildEditContext()
{
    SceneEditor::EditContext ctx;

    ctx.camera = camera_.get();
    ctx.skydome = skydome_.get();
    ctx.spriteCommon = spriteCommon_.get();

    ctx.cameraTargetPos = &cameraTargetPos_;
    ctx.cameraTargetRot = &cameraTargetRot_;
    ctx.cameraSmoothFrames = &cameraSmoothFrames_;
    ctx.cameraPosHistory = &cameraPosHistory_;
    ctx.cameraRotHistory = &cameraRotHistory_;

    ctx.skyColor = &skyColor_;
    ctx.skyRotOffsetY = &skyRotOffsetY_;


    ctx.requestClear = &requestClear_;
    return ctx;
}

void GamePlayScene::UpdateCameraSmoothing()
{
    cameraPosHistory_.push_back(cameraTargetPos_);
    cameraRotHistory_.push_back(cameraTargetRot_);

    while ((int)cameraPosHistory_.size() > cameraSmoothFrames_) {
        cameraPosHistory_.pop_front();
        cameraRotHistory_.pop_front();
    }

    Vector3 avgPos = { };
    Vector3 avgRot = { };
    for (const auto& p : cameraPosHistory_) {
        avgPos.x += p.x;
        avgPos.y += p.y;
        avgPos.z += p.z;
    }
    for (const auto& r : cameraRotHistory_) {
        avgRot.x += r.x;
        avgRot.y += r.y;
        avgRot.z += r.z;
    }
    float n = static_cast<float>(cameraPosHistory_.size());

    camera_->SetTranslate({ avgPos.x / n, cameraTargetPos_.y, avgPos.z / n });
    camera_->SetRotate({ avgRot.x / n, avgRot.y / n, avgRot.z / n });
}

// ══════════════════════════════════════════════════════
// シーン更新
// ══════════════════════════════════════════════════════

void GamePlayScene::Update()
{
    if (UpdateClearState()) {
        return;
    }

    const bool pauseToggle = input_->TriggerAction(Input::Action::Pause)
        || (pauseController_.IsPaused() && input_->TriggerMenuCancel());
    const auto updateMode = pauseController_.Advance(pauseToggle
        && !WeaponManager::GetInstance()->HasPendingWeapon());
    if (updateMode == GamePauseController::UpdateMode::SkipFrame) { return; }
    if (updateMode == GamePauseController::UpdateMode::Menu) {
        UpdatePauseMenu();
        return;
    }

    UpdateWeaponExchange();
    if (WeaponManager::GetInstance()->HasPendingWeapon()) {
        UpdateCamera();
        UpdateStyleAndUI(0.0f);
        return;
    }

    auto* tm = TimeManager::GetInstance();
    const float dt = tm->GetDeltaTime(); // ヒットストップ中 = 0、スロー時は比例値
    floorElapsedSeconds_ += GameConstants::kFrameDeltaTime; // プレイログ用。ヒットストップの影響を受けない実時間換算

    UpdateCombat();
    UpdateCamera();
    player_->RefreshVisualTransforms();
    sceneEditor_.Update(BuildEditContext(), input_);
    UpdateStyleAndUI(dt);
    UpdateParticles(dt);
    UpdateFinisherSlash(dt);
    enemySlice_.Update(dt, camera_.get());
    bladeFlash_.Update(dt, camera_.get());

    // 敵位置を画面UVへ投影して空間歪みの中心に設定する
    if (spaceWarp_.IsActive() || finisherActive_) {
        const Vector3& epos = enemy_->GetPosition();
        const Matrix4x4 vp = Multiply(camera_->GetViewMatrix(), camera_->GetProjectionMatrix());
        const float cx = epos.x * vp.m[0][0] + epos.y * vp.m[1][0] + epos.z * vp.m[2][0] + vp.m[3][0];
        const float cy = epos.x * vp.m[0][1] + epos.y * vp.m[1][1] + epos.z * vp.m[2][1] + vp.m[3][1];
        const float cw = epos.x * vp.m[0][3] + epos.y * vp.m[1][3] + epos.z * vp.m[2][3] + vp.m[3][3];
        if (cw > kMinClipW) {
            spaceWarp_.SetCenterUV(cx / cw * 0.5f + 0.5f, 0.5f - cy / cw * 0.5f);
        }
    }
    spaceWarp_.Update(dt);

    // 解放時の世界割れ（ヒットストップ中は凍結し、時が動き出すと砕け散る）
    finisherShatter_.Update(dt);
    if (finisherShatter_.IsFinished()) {
        finisherShatter_.Reset();
    }

    // 切断演出が飛散に移ったら、敵本体を再表示する
    // 撃破済みでもJの武器吸収で本体を消費するまでは戻す（フィニッシャーで倒した場合に
    // 透明な敵へJを当てる状態にならないように）。吸収完了後はweaponStealTriggered_が立つので戻さない
    if (!enemy_->IsVisible() && !weaponStealTriggered_
        && (enemySlice_.IsBursting() || !enemySlice_.IsActive())) {
        enemy_->SetVisible(true);
    }

    SlashMark::GetInstance()->Update(dt);

    // ラン中にHPが尽きたらゲームオーバー結果へ（クリア演出が始まっていればそちらを優先する）
    auto* runData = RunData::GetInstance();
    if (GameRules::GetInstance()->Get().gameOverOnHpZero
        && runData->IsRunActive() && runData->GetHp() <= 0 && !clearTriggered_) {
        PlaytestLog::GetInstance()->RecordRunResult(false, runData->GetFloor(), floorElapsedSeconds_,
            peakStyle_, styleRankHud_.GetBestChain(), player_->GetPosition());
        SceneFlow::GetInstance()->Transition(kSceneName, "gameover", "GAMEOVER");
        return;
    }

    CheckClearCondition();
}

bool GamePlayScene::UpdateClearState()
{
    if (!clearTriggered_) {
        return false;
    }

    auto* rd = RunData::GetInstance();
    const GameRulesData& rules = GameRules::GetInstance()->Get();
    if (rd->IsRunActive() && !glassShatterDebugTest_) {
        // ローグライト: 結果表示 → 次のフロア（最終フロアならクリア）へ
        if (!showResult_) {
            showResult_ = true;
            resultTimer_ = rules.resultDisplaySeconds;
            lastGold_ = RunData::CalcGold(peakStyle_);
            rd->AddGold(lastGold_);
            lastScore_ = GameRules::GetInstance()->ScoreForRank(RunData::CalcRank(peakStyle_));
            rd->AddScore(lastScore_);
            PlaytestLog::GetInstance()->RecordRunResult(true, rd->GetFloor(), floorElapsedSeconds_,
                peakStyle_, styleRankHud_.GetBestChain(), player_->GetPosition());
            rd->AdvanceFloor();

            // フロアクリア毎に自動セーブ（最終フロア到達時はコンティニュー不要なので破棄）
            if (rd->GetFloor() >= rules.finalFloor) {
                SaveDataManager::GetInstance()->ClearContinue();
            } else {
                SaveDataManager::GetInstance()->SaveContinue(*rd);
            }
        }
        resultTimer_ -= GameConstants::kFrameDeltaTime;
        if (resultTimer_ <= 0.0f) {
            if (rd->GetFloor() >= rules.finalFloor) {
                SceneFlow::GetInstance()->Transition(kSceneName, "clear", "CLEAR");
            } else {
                SceneFlow::GetInstance()->Transition(kSceneName, "next", "MAP");
            }
        }
    } else {
        // サンドボックス: ガラス割れ → クリア結果へ
        glassShatter_.Update(GameConstants::kFrameDeltaTime);
        if (glassShatter_.IsFinished()) {
            SceneFlow::GetInstance()->Transition(kSceneName, "sandbox_clear", "CLEAR");
        }
    }
    return true;
}

// ══════════════════════════════════════════════════════
// 戦闘とカメラの更新
// ══════════════════════════════════════════════════════

void GamePlayScene::UpdateCombatEvents()
{
    auto* tm = TimeManager::GetInstance();

    // 乱舞 打ち上げヒット
    if (player_->JustLaunched()) {
        enemy_->Launch(GameConstants::kLaunchSpeed);
        const Vector3& epos = enemy_->GetPosition();
        tm->RequestHitStop(GameConstants::kHitStopLaunch);
        cameraShaker_.Request(GameConstants::kShakeLaunchAmt, GameConstants::kShakeLaunchDur);
        pm_->EmitRing("hit_ring", epos, kLaunchRingSpeed, kLaunchRingColor, kLaunchRingCount, kLaunchRingLifetime, kLaunchRingSize);
        std::uniform_real_distribution<float> vxL(-kLaunchSparkSpreadX, kLaunchSparkSpreadX);
        std::uniform_real_distribution<float> vyL(kLaunchSparkRiseMin, kLaunchSparkRiseMax);
        for (int i = 0; i < kLaunchSparkCount; ++i) {
            pm_->EmitGravity("hit_spark", epos,
                { vxL(rng_), vyL(rng_), 0.0f },
                kLaunchSparkColor, kLaunchSparkLifetime, kLaunchSparkSize);
        }
    }

    // 乱舞 ジャグルスラッシュ
    if (player_->JustRampageHit()) {
        int cnt = player_->GetJuggleCount();
        float dir = player_->GetLastDirX();
        float slashAng = (dir > 0.0f) ? 0.0f : GameConstants::kPi;
        float rad = kJuggleSlashRadiusBase + cnt * kJuggleSlashRadiusStep;
        const Vector3& epos = enemy_->GetPosition();
        pm_->EmitSlash("sword_slash", epos, slashAng,
            { 1.0f, 1.0f - cnt * kJuggleSlashGreenFade, 1.0f - cnt * kJuggleSlashBlueFade, 1.0f }, rad);
        pm_->EmitRing("hit_ring", epos, kJuggleRingSpeedBase + cnt * kJuggleRingSpeedStep,
            kJuggleRingColor, kJuggleRingCount, kJuggleRingLifetime, kJuggleRingSize);
        tm->RequestHitStop(GameConstants::kHitStopJuggle);
        cameraShaker_.Request(kJuggleShakeBase + cnt * kJuggleShakeStep, kJuggleShakeSeconds);
    }

    // 乱舞 フィニッシュ
    if (player_->JustRampageFinish()) {
        const Vector3& epos = enemy_->GetPosition();
        tm->RequestHitStop(GameConstants::kHitStopFinish);
        cameraShaker_.Request(GameConstants::kShakeFinishAmt, GameConstants::kShakeFinishDur);
        pm_->EmitRing("hit_ring", epos, kFinishOuterRingSpeed, kFinishOuterRingColor,
            kFinishOuterRingCount, kFinishOuterRingLifetime, kFinishOuterRingSize);
        pm_->EmitRing("hit_ring", epos, kFinishInnerRingSpeed, kFinishInnerRingColor,
            kFinishInnerRingCount, kFinishInnerRingLifetime, kFinishInnerRingSize);
        std::uniform_real_distribution<float> vxF(-kFinishSparkSpreadX, kFinishSparkSpreadX);
        std::uniform_real_distribution<float> vyF(kFinishSparkRiseMin, kFinishSparkRiseMax);
        for (int i = 0; i < kFinishSparkCount; ++i) {
            pm_->EmitGravity("hit_spark", epos,
                { vxF(rng_), vyF(rng_), 0.0f },
                { 1.0f, kFinishSparkGreenBase + i * kFinishSparkGreenStep, kFinishSparkBlue, 1.0f },
                kFinishSparkLifetime, kFinishSparkSize);
        }
    }

    // フィニッシャースラッシュ 発動の合図（斬撃線の表示は UpdateFinisherSlash に委譲）
    if (player_->JustFinisherSlash()) {
        const Vector3& epos = enemy_->GetPosition();
        finisherActive_ = true;
        finisherLineIdx_ = 0;
        finisherBeatTimer_ = GameConstants::kFinisherChargeDelay;

        // 敵を打ち上げて空中に拘束し、暗転とともに溜めを作る
        enemy_->Launch(GameConstants::kLaunchSpeed * kFinisherLaunchRatio);
        tm->RequestHitStop(GameConstants::kHitStopJuggle);
        cameraShaker_.Request(kFinisherStartShakeAmount, kFinisherStartShakeSeconds);
        SceneShared::EmitFinisherCharge(pm_, "hit_ring", "hit_spark", epos);
        spaceWarp_.AddImpulse(kFinisherStartWarpImpulse);
    }
}

void GamePlayScene::UpdateCombat()
{
    auto* tm = TimeManager::GetInstance();

    // ヒットストップ中も回避ボタンの押下は記憶しておく（停止明けに回避へつなげ、押した感触が消えないように）
    player_->BufferDodgeInput(input_);

    if (!tm->IsHitStopped()) {
        SyncCombatEnemies(); // spawn_pointが出した敵をこのフレームから戦闘対象に含める
        UpdateTargetLock();
        std::vector<AABB> activeColliders = GetStageEditor().GetSolidColliders();
        player_->Update(input_, enemy_->GetPosition(), !enemy_->IsDefeated() && enemy_->IsVisible(), !activeColliders.empty());

        // 移動後に足場との接触を解決し、補正が入ったフレームは見た目も同期する
        // エディタの現在状態から毎フレーム判定を作り、移動・追加・削除を即時反映する
        player_->ResolveBlockCollision(activeColliders);
        player_->RefreshVisualTransforms();
        UpdateWeaponTrail();

        // ロック中は移動入力に関係なく対象の方を向かせる（コンボ判定より前でないと今フレームに反映されない）
        if (lockedEnemy_ != nullptr) {
            player_->FaceTarget(lockedEnemy_->GetPosition());
        }

        UpdateCombatEvents();
        UpdateWeaponEnemies();
        // 雑魚へのヒットは直前のUpdateWeaponEnemies()、ボスへのヒットは前フレームのUpdateStyleAndUI()で記録済み
        UpdateWeaponFatigue();
        UpdateExplosiveBarrels();

        // enemy_の物理/アニメーション更新自体はStageEditor所有のためGetStageEditor().UpdateObjects()
        // （BaseScene::Tick()がUpdate()の直後に呼ぶ）が担う。ここでは前フレーム分の着地判定だけ読む
        // （1フレーム遅延するが60fps下では実用上無視できる差）
        if (enemy_->JustLanded()) {
            player_->EndRampage(); // 敵が着地したらジャグル強制終了
        }

        skydome_->Update(camera_.get());
    }

    // 水エフェクト更新（ヒットストップに関係なく毎フレーム）
    if (IsWaterFloor()) {
        waterPool_->Update();
        if (player_->JustEnteredWater() || player_->JustExitedWater()) {
            waterPool_->EmitSplash(player_->GetPosition());
        }
    }
}
