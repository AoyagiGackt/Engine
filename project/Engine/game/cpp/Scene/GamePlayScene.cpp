/**
 * @file GamePlayScene.cpp
 * @brief メインステージ（GamePlayScene）の初期化と基本更新フローの実装
 */
#include "GamePlayScene.h"
#include "AudioBridge.h"
#include "GameConstants.h"
#include "GamePlaySceneInitializer.h"
#include "GrayscaleEffect.h"
#include "HsvFilter.h"
#include "ImGuiControl.h"
#include "ImageFilter.h"
#include "ParticleManager.h"
#include "PipelineStateGuard.h"
#include "PlayerBridge.h"
#include "PostEffectRenderTarget.h"
#include "RunData.h"
#include "SaveData.h"
#include "SceneManager.h"
#include "ScoreManager.h"
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
    InitializeStageActorsAndScore();
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
    scoreManager_ = ScoreManager::GetInstance();
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
    camera_->SetTranslate({ 19.0f, 6.0f, GameConstants::kCameraDistanceZ });
    Object3d::SetCommonCamera(camera_.get());

    modelSkydome_ = std::make_unique<Model>();
    modelSkydome_->Initialize(modelCommon_.get(),
        "Resources/SkyDome/SkyDome.obj",
        "Resources/SkyDome/skySphere.png");

    skydome_ = std::make_unique<Skydome>();
    skydome_->Initialize(modelCommon_.get(), modelSkydome_.get());
}

void GamePlayScene::InitializeStageActorsAndScore()
{
    GamePlaySceneInitializer::InitializeStageActors(*this);

    scoreManager_->LoadScores();
    scoreManager_->ResetCurrentScore();

    gameTime_.Initialize();
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
    if (RunData::GetInstance()->GetFloor() == 3) {
        player_->SetWaterLevel(WaterPool::GetSurfaceY());
    }

    fontRenderer_.Initialize(spriteCommon_.get());
    SlashMark::GetInstance()->Initialize(spriteCommon_.get());

    awakenGaugeBg_ = std::make_unique<Sprite>();
    awakenGaugeBg_->Initialize(spriteCommon_.get(), "Resources/white.png");
    awakenGaugeBg_->SetColor({ 0.04f, 0.06f, 0.10f, 0.85f });
    awakenGaugeFg_ = std::make_unique<Sprite>();
    awakenGaugeFg_->Initialize(spriteCommon_.get(), "Resources/white.png");
    InitializeWeaponSlotHud();
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
    finisherShatter_.SetDuration(0.9f);

    ImGuiControlPanel::RegisterGlassShatterTrigger([this]() { TriggerGlassShatterTest(); });
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
    if (skydome_) {
        skydome_->Update(camera_.get());
    }
    for (auto& core : energyCores_) {
        if (core.object && !core.collected) {
            core.object->Update();
        }
    }
    if (ghostObject_ && !ghostTrail_.empty()) {
        ghostObject_->Update();
    }
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

    ctx.gameHour = gameTime_.GetHour();
    ctx.gameMinute = gameTime_.GetMinute();

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

    UpdateWeaponExchange();
    if (WeaponManager::GetInstance()->HasPendingWeapon()) {
        UpdateCamera();
        UpdateStyleAndUI(0.0f);
        return;
    }

    auto* tm = TimeManager::GetInstance();
    const float dt = tm->GetDeltaTime(); // ヒットストップ中 = 0、スロー時は比例値
    gameTime_.Update(1.0f);

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
        if (cw > 0.0001f) {
            spaceWarp_.SetCenterUV(cx / cw * 0.5f + 0.5f, 0.5f - cy / cw * 0.5f);
        }
    }
    spaceWarp_.Update(dt);

    // 解放時の世界割れ（ヒットストップ中は凍結し、時が動き出すと砕け散る）
    finisherShatter_.Update(dt);
    if (finisherShatter_.IsFinished()) {
        finisherShatter_.Reset();
    }

    // 切断演出が飛散に移ったら、生き残っている敵本体を再表示する
    if (!enemy_->IsDefeated() && !enemy_->IsVisible()
        && (enemySlice_.IsBursting() || !enemySlice_.IsActive())) {
        enemy_->SetVisible(true);
    }

    SlashMark::GetInstance()->Update(dt);
    CheckClearCondition();
}

bool GamePlayScene::UpdateClearState()
{
    if (!clearTriggered_) {
        return false;
    }

    auto* rd = RunData::GetInstance();
    if (rd->IsRunActive() && !glassShatterDebugTest_) {
        // ローグライト: 結果表示 → MAP遷移
        if (!showResult_) {
            showResult_ = true;
            resultTimer_ = 2.5f;
            lastGold_ = RunData::CalcGold(peakStyle_);
            rd->AddGold(lastGold_);
            rd->AdvanceFloor();

            // フロアクリア毎に自動セーブ（ボス撃破時はコンティニュー不要なので破棄）
            if (rd->GetFloor() >= 6) {
                SaveDataManager::GetInstance()->ClearContinue();
            } else {
                SaveDataManager::GetInstance()->SaveContinue(*rd);
            }
        }
        resultTimer_ -= GameConstants::kFrameDeltaTime;
        if (resultTimer_ <= 0.0f) {
            if (rd->GetFloor() >= 6) {
                SceneManager::GetInstance()->ChangeScene("CLEAR");
            } else {
                SceneManager::GetInstance()->ChangeScene("MAP");
            }
        }
    } else {
        // サンドボックス: ガラス割れ → CLEAR
        glassShatter_.Update(GameConstants::kFrameDeltaTime);
        if (glassShatter_.IsFinished()) {
            SceneManager::GetInstance()->ChangeScene("CLEAR");
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
        pm_->EmitRing("hit_ring", epos, 5.5f, { 1.0f, 0.55f, 0.1f, 1.0f }, 20, 0.45f, 0.28f);
        std::uniform_real_distribution<float> vxL(-4.0f, 4.0f);
        std::uniform_real_distribution<float> vyL(3.0f, 7.0f);
        for (int i = 0; i < 10; ++i) {
            pm_->EmitGravity("hit_spark", epos,
                { vxL(rng_), vyL(rng_), 0.0f },
                { 1.0f, 0.65f, 0.15f, 1.0f }, 0.8f, 0.18f);
        }
    }

    // 乱舞 ジャグルスラッシュ
    if (player_->JustRampageHit()) {
        int cnt = player_->GetJuggleCount();
        float dir = player_->GetLastDirX();
        float slashAng = (dir > 0.0f) ? 0.0f : GameConstants::kPi;
        float rad = 1.2f + cnt * 0.12f;
        const Vector3& epos = enemy_->GetPosition();
        pm_->EmitSlash("sword_slash", epos, slashAng,
            { 1.0f, 1.0f - cnt * 0.06f, 1.0f - cnt * 0.09f, 1.0f }, rad);
        pm_->EmitRing("hit_ring", epos, 2.5f + cnt * 0.2f,
            { 1.0f, 0.9f, 0.5f, 0.8f }, 8, 0.25f, 0.15f);
        tm->RequestHitStop(GameConstants::kHitStopJuggle);
        cameraShaker_.Request(0.12f + cnt * 0.01f, 0.10f);
    }

    // 乱舞 フィニッシュ
    if (player_->JustRampageFinish()) {
        const Vector3& epos = enemy_->GetPosition();
        tm->RequestHitStop(GameConstants::kHitStopFinish);
        cameraShaker_.Request(GameConstants::kShakeFinishAmt, GameConstants::kShakeFinishDur);
        pm_->EmitRing("hit_ring", epos, 8.0f, { 1.0f, 0.3f, 0.3f, 1.0f }, 24, 0.5f, 0.35f);
        pm_->EmitRing("hit_ring", epos, 5.0f, { 1.0f, 1.0f, 0.5f, 1.0f }, 16, 0.45f, 0.30f);
        std::uniform_real_distribution<float> vxF(-6.0f, 6.0f);
        std::uniform_real_distribution<float> vyF(4.0f, 10.0f);
        for (int i = 0; i < 16; ++i) {
            pm_->EmitGravity("hit_spark", epos,
                { vxF(rng_), vyF(rng_), 0.0f },
                { 1.0f, 0.4f + i * 0.04f, 0.1f, 1.0f }, 1.0f, 0.20f);
        }
    }

    // フィニッシャースラッシュ 発動の合図（斬撃線の表示は UpdateFinisherSlash に委譲）
    if (player_->JustFinisherSlash()) {
        const Vector3& epos = enemy_->GetPosition();
        finisherActive_ = true;
        finisherLineIdx_ = 0;
        finisherBeatTimer_ = GameConstants::kFinisherChargeDelay;

        // 敵を打ち上げて空中に拘束し、暗転とともに溜めを作る
        enemy_->Launch(GameConstants::kLaunchSpeed * 0.7f);
        tm->RequestHitStop(GameConstants::kHitStopJuggle);
        cameraShaker_.Request(0.20f, 0.15f);
        SceneShared::EmitFinisherCharge(pm_, "hit_ring", "hit_spark", epos);
        spaceWarp_.AddImpulse(0.4f);
    }
}

void GamePlayScene::UpdateCombat()
{
    auto* tm = TimeManager::GetInstance();

    if (!tm->IsHitStopped()) {
        UpdateTargetLock();
        player_->Update(input_, enemy_->GetPosition());

        // 移動後に足場との接触を解決し、補正が入ったフレームは見た目も同期する
        // エディタの現在状態から毎フレーム判定を作り、移動・追加・削除を即時反映する
        std::vector<AABB> activeColliders = GetStageEditor().GetSolidColliders();
        player_->ResolveBlockCollision(activeColliders);
        player_->RefreshVisualTransforms();

        // ロック中は移動入力に関係なく対象の方を向かせる（コンボ判定より前でないと今フレームに反映されない）
        if (lockedKind_ == LockTargetKind::MainEnemy) {
            player_->FaceTarget(enemy_->GetPosition());
        } else if (lockedKind_ == LockTargetKind::WeaponEnemy && lockedWeaponEnemyIndex_ < weaponEnemies_.size()) {
            player_->FaceTarget(weaponEnemies_[lockedWeaponEnemyIndex_].enemy->GetPosition());
        }

        UpdateCombatEvents();
        UpdateWeaponEnemies();
        UpdateEnergyCores();

        // enemy_の物理/アニメーション更新自体はStageEditor所有のためGetStageEditor().UpdateObjects()
        // （BaseScene::Tick()がUpdate()の直後に呼ぶ）が担う。ここでは前フレーム分の着地判定だけ読む
        // （1フレーム遅延するが60fps下では実用上無視できる差）
        if (enemy_->JustLanded()) {
            player_->EndRampage(); // 敵が着地したらジャグル強制終了
        }

        skydome_->Update(camera_.get());
    }

    // 水エフェクト更新（ヒットストップに関係なく毎フレーム）
    if (RunData::GetInstance()->GetFloor() == 3) {
        waterPool_->Update();
        if (player_->JustEnteredWater() || player_->JustExitedWater()) {
            waterPool_->EmitSplash(player_->GetPosition());
        }
    }
}
