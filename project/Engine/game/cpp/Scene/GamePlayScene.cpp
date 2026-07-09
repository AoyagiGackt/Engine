#include "GamePlayScene.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <random>
#include "GameConstants.h"
#include "RunData.h"
#include "SaveData.h"
#include "GrayscaleEffect.h"
#include "HsvFilter.h"
#include "ImageFilter.h"
#include "ImGuiControl.h"
#include "ParticleManager.h"
#include "PostEffectRenderTarget.h"
#include "SceneManager.h"
#include "ScoreManager.h"
#include "ScreenFlash.h"
#include "SlashMark.h"
#include "StringUtility.h"
#include "TextureManager.h"
#include "WeaponManager.h"
using namespace engine;
using namespace engine::graphics;
using namespace engine::game;

// =====================================================
// 初期化
// =====================================================

void GamePlayScene::Initialize(DirectXCommon* dxCommon, Input* input, Audio* audio)
{
    // 引数のポインタをメンバ変数に保存しておく（後で Update/Draw からも使えるように）
    dxCommon_ = dxCommon;
    input_ = input;
    audio_ = audio;

    spriteCommon_ = std::make_unique<SpriteCommon>();
    spriteCommon_->Initialize(dxCommon_);

    modelCommon_ = std::make_unique<ModelCommon>();
    modelCommon_->Initialize(dxCommon_);

    objectCommon_ = std::make_unique<Object3dCommon>();
    objectCommon_->Initialize(dxCommon_);

    srvManager_      = SrvManager::GetInstance();
    scoreManager_    = ScoreManager::GetInstance();
    grayscaleEffect_ = GrayscaleEffect::GetInstance();
    imageFilter_     = ImageFilter::GetInstance();
    hsvFilter_       = HsvFilter::GetInstance();

    shadowManager_ = std::make_unique<ShadowManager>();
    shadowManager_->Initialize(dxCommon_, srvManager_);

    // OutlineEffect等でルートシグネチャを切り替えた後にライト/シャドウマップを再バインドできるようにする
    Object3d::SetCommonObjectCommon(objectCommon_.get());
    Object3d::SetCommonShadowManager(shadowManager_.get());

    camera_ = std::make_unique<Camera>();
    camera_->SetTranslate({ 19.0f, 6.0f, -24.0f });
    Object3d::SetCommonCamera(camera_.get());

    modelSkydome_ = std::make_unique<Model>();
    modelSkydome_->Initialize(modelCommon_.get(),
        "Resources/SkyDome/SkyDome.obj",
        "Resources/SkyDome/skySphere.png");

    skydome_ = std::make_unique<Skydome>();
    skydome_->Initialize(modelCommon_.get(), modelSkydome_.get());

    {
        auto levelData = LevelLoader::Load("Resources/Levels/level01.json");
        levelSpawn_ = LevelLoader::Spawn(levelData, modelCommon_.get());
        for (auto& obj : levelSpawn_.objects) {
            borderBlocks_.push_back(std::move(obj));
        }
        levelSpawn_.objects.clear();

        player_ = std::make_unique<Player>();
        player_->Initialize(modelCommon_.get());
        player_->SetPosition(levelData.playerSpawn);

        {
            auto* rd = RunData::GetInstance();
            if (rd->IsRunActive()) {
                Player::SkillMods mods;
                if (rd->HasSkill(RunData::Skill::BlinkPlus))    { mods.blinkDistMult    = 1.5f; }
                if (rd->HasSkill(RunData::Skill::ComboExtend))  { mods.comboMaxBonus    = 1; }
                if (rd->HasSkill(RunData::Skill::FastFire))     { mods.fireIntervalMult = 0.5f; }
                if (rd->HasSkill(RunData::Skill::AwakenBoost))  { mods.gaugeChargeMult  = 1.5f; }
                if (rd->HasSkill(RunData::Skill::SpeedUp))      { mods.speedMult        = 1.2f; }
                if (rd->HasSkill(RunData::Skill::HighJump))     { mods.jumpMult         = 1.25f; }
                if (rd->HasSkill(RunData::Skill::JuggleExtend)) { mods.juggleMaxBonus   = 4; }
                player_->ApplySkillMods(mods);
            }
        }

        enemy_ = std::make_unique<EnemyEntity>();
        enemy_->Initialize(modelCommon_.get(), levelData.enemySpawn);
        {
            auto* rd = RunData::GetInstance();
            if (rd->IsRunActive()) {
                int hp = 20;
                if (rd->GetCurrentNode() == RunData::NodeType::Elite) { hp = 35; }
                else if (rd->GetCurrentNode() == RunData::NodeType::Boss) { hp = 60; }
                enemy_->SetMaxHp(hp);
            }
        }
    }

    scoreManager_->LoadScores();
    scoreManager_->ResetCurrentScore();

    gameTime_.Initialize();

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

    pm_ = ParticleManager::GetInstance();
    SceneShared::CreateParticleGroupsFromJson(pm_, "Resources/particles/gameplay.json");

    waterPool_ = std::make_unique<WaterPool>();
    waterPool_->Initialize(spriteCommon_.get());
    player_->SetWaterLevel(WaterPool::GetSurfaceY());

    fontRenderer_.Initialize(spriteCommon_.get());
    SlashMark::GetInstance()->Initialize(spriteCommon_.get());

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

SceneEditor::EditContext GamePlayScene::BuildEditContext()
{
    SceneEditor::EditContext ctx;

    ctx.camera       = camera_.get();
    ctx.skydome      = skydome_.get();
    ctx.spriteCommon = spriteCommon_.get();

    ctx.cameraTargetPos    = &cameraTargetPos_;
    ctx.cameraTargetRot    = &cameraTargetRot_;
    ctx.cameraSmoothFrames = &cameraSmoothFrames_;
    ctx.cameraPosHistory   = &cameraPosHistory_;
    ctx.cameraRotHistory   = &cameraRotHistory_;

    ctx.skyColor      = &skyColor_;
    ctx.skyRotOffsetY = &skyRotOffsetY_;

    ctx.gameHour   = gameTime_.GetHour();
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

    Vector3 avgPos = {};
    Vector3 avgRot = {};
    for (const auto& p : cameraPosHistory_) {
        avgPos.x += p.x; avgPos.y += p.y; avgPos.z += p.z;
    }
    for (const auto& r : cameraRotHistory_) {
        avgRot.x += r.x; avgRot.y += r.y; avgRot.z += r.z;
    }
    float n = static_cast<float>(cameraPosHistory_.size());

    camera_->SetTranslate({ avgPos.x / n, avgPos.y / n, avgPos.z / n });
    camera_->SetRotate({ avgRot.x / n, avgRot.y / n, avgRot.z / n });
}

void GamePlayScene::Update()
{
    if (UpdateClearState()) { return; }

    auto* tm       = TimeManager::GetInstance();
    const float dt = tm->GetDeltaTime(); // ヒットストップ中 = 0、スロー時は比例値
    gameTime_.Update(1.0f);

    UpdateCombat();
    UpdateCamera();
    sceneEditor_.Update(BuildEditContext());
    UpdateStyleAndUI(dt);
    UpdateParticles(dt);
    UpdateFinisherSlash(dt);
    enemySlice_.Update(dt, camera_.get());
    bladeFlash_.Update(dt, camera_.get());

    // 敵位置を画面UVへ投影して空間歪みの中心に設定する
    if (spaceWarp_.IsActive() || finisherActive_) {
        const Vector3&  epos = enemy_->GetPosition();
        const Matrix4x4 vp   = Multiply(camera_->GetViewMatrix(), camera_->GetProjectionMatrix());
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
    if (!clearTriggered_) { return false; }

    auto* rd = RunData::GetInstance();
    if (rd->IsRunActive() && !glassShatterDebugTest_) {
        // ローグライト: 結果表示 → MAP遷移
        if (!showResult_) {
            showResult_  = true;
            resultTimer_ = 2.5f;
            lastGold_    = RunData::CalcGold(peakStyle_);
            rd->AddGold(lastGold_);
            rd->AdvanceFloor();

            // フロアクリア毎に自動セーブ（ボス撃破時はコンティニュー不要なので破棄）
            if (rd->GetFloor() >= 4) {
                SaveDataManager::GetInstance()->ClearContinue();
            } else {
                SaveDataManager::GetInstance()->SaveContinue(*rd);
            }
        }
        resultTimer_ -= GameConstants::kFrameDeltaTime;
        if (resultTimer_ <= 0.0f) {
            if (rd->GetFloor() >= 4) {
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

void GamePlayScene::UpdateCombatEvents()
{
    auto* tm = TimeManager::GetInstance();

    // 乱舞：打ち上げヒット
    if (player_->JustLaunched()) {
        enemy_->Launch(GameConstants::kLaunchSpeed);
        const Vector3& epos = enemy_->GetPosition();
        tm->RequestHitStop(GameConstants::kHitStopLaunch);
        cameraShaker_.Request(GameConstants::kShakeLaunchAmt, GameConstants::kShakeLaunchDur);
        pm_->EmitRing("hit_ring",  epos, 5.5f, { 1.0f, 0.55f, 0.1f, 1.0f }, 20, 0.45f, 0.28f);
        std::uniform_real_distribution<float> vxL(-4.0f, 4.0f);
        std::uniform_real_distribution<float> vyL( 3.0f, 7.0f);
        for (int i = 0; i < 10; ++i) {
            pm_->EmitGravity("hit_spark", epos,
                { vxL(rng_), vyL(rng_), 0.0f },
                { 1.0f, 0.65f, 0.15f, 1.0f }, 0.8f, 0.18f);
        }
    }

    // 乱舞：ジャグルスラッシュ
    if (player_->JustRampageHit()) {
        int   cnt      = player_->GetJuggleCount();
        float dir      = player_->GetLastDirX();
        float slashAng = (dir > 0.0f) ? 0.0f : GameConstants::kPi;
        float rad      = 1.2f + cnt * 0.12f;
        const Vector3& epos = enemy_->GetPosition();
        pm_->EmitSlash("sword_slash", epos, slashAng,
            { 1.0f, 1.0f - cnt * 0.06f, 1.0f - cnt * 0.09f, 1.0f }, rad);
        pm_->EmitRing("hit_ring", epos, 2.5f + cnt * 0.2f,
            { 1.0f, 0.9f, 0.5f, 0.8f }, 8, 0.25f, 0.15f);
        tm->RequestHitStop(GameConstants::kHitStopJuggle);
        cameraShaker_.Request(0.12f + cnt * 0.01f, 0.10f);
    }

    // 乱舞：フィニッシュ
    if (player_->JustRampageFinish()) {
        const Vector3& epos = enemy_->GetPosition();
        tm->RequestHitStop(GameConstants::kHitStopFinish);
        cameraShaker_.Request(GameConstants::kShakeFinishAmt, GameConstants::kShakeFinishDur);
        pm_->EmitRing("hit_ring", epos, 8.0f, { 1.0f, 0.3f, 0.3f, 1.0f }, 24, 0.5f, 0.35f);
        pm_->EmitRing("hit_ring", epos, 5.0f, { 1.0f, 1.0f, 0.5f, 1.0f }, 16, 0.45f, 0.30f);
        std::uniform_real_distribution<float> vxF(-6.0f, 6.0f);
        std::uniform_real_distribution<float> vyF( 4.0f, 10.0f);
        for (int i = 0; i < 16; ++i) {
            pm_->EmitGravity("hit_spark", epos,
                { vxF(rng_), vyF(rng_), 0.0f },
                { 1.0f, 0.4f + i * 0.04f, 0.1f, 1.0f }, 1.0f, 0.20f);
        }
    }

    // フィニッシャースラッシュ：発動の合図（斬撃線の表示は UpdateFinisherSlash に委譲）
    if (player_->JustFinisherSlash()) {
        const Vector3& epos = enemy_->GetPosition();
        finisherActive_    = true;
        finisherLineIdx_   = 0;
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
        player_->Update(input_, enemy_->GetPosition());

        UpdateCombatEvents();

        enemy_->Update();
        if (enemy_->JustLanded()) {
            player_->EndRampage(); // 敵が着地したらジャグル強制終了
        }

        for (auto& obj : gameObjects_) { obj->Update(); }
        skydome_->Update(camera_.get());
        for (auto& block : borderBlocks_) { block->Update(); }
    }

    // 水エフェクト更新（ヒットストップに関係なく毎フレーム）
    waterPool_->Update();

    if (player_->JustEnteredWater() || player_->JustExitedWater()) {
        waterPool_->EmitSplash(player_->GetPosition());
    }
}

void GamePlayScene::UpdateCamera()
{
    // カメラをプレイヤーに追従（境界ブロックが画面外に出ないよう clamp）
    constexpr float kBlkR = 0.5f;
    const Vector3& ppos = player_->GetPosition();
    cameraTargetPos_ = {
        std::clamp(ppos.x,        2.0f  - kBlkR + GameConstants::kCameraHalfW,  36.0f + kBlkR - GameConstants::kCameraHalfW),
        std::clamp(ppos.y + 3.0f, -0.6f - kBlkR + GameConstants::kCameraHalfH,  13.0f + kBlkR - GameConstants::kCameraHalfH),
        -24.0f
    };

    UpdateCameraSmoothing();

    // カメラシェイク（スムージングの後に直接カメラ座標へ加算）
    Vector3 shake = cameraShaker_.Update(GameConstants::kFrameDeltaTime);
    if (shake.x != 0.0f || shake.y != 0.0f) {
        Vector3 cam = camera_->GetTranslate();
        camera_->SetTranslate({ cam.x + shake.x, cam.y + shake.y, cam.z });
    }

    shadowManager_->Update(objectCommon_->GetLightDirection());
    Object3d::SetLightViewProjection(shadowManager_->GetLightViewProjection());
}

void GamePlayScene::UpdateStyleAndUI(float dt)
{
    const auto*    wm     = WeaponManager::GetInstance();
    const WeaponData& weapon = wm->GetCurrent();
    const Vector3& ppos   = player_->GetPosition();
    const Vector3& epos   = enemy_->GetPosition();
    AABB enemyAABB = { { epos.x - 0.5f, epos.y - 0.5f, -0.5f },
                       { epos.x + 0.5f, epos.y + 0.5f,  0.5f } };

    if (player_->JustComboHit()) {
        // 前方に厚く、背後は振り抜きぶんだけ（左右対称だと背後の遠い敵にまで当たってしまう）
        AABB meleeRange = SceneShared::MakeDirectionalRange(
            ppos, player_->GetLastDirX(), weapon.range, weapon.range * 0.4f);
        if (Collision::CheckCollision(meleeRange, enemyAABB)) {
            styleMeter_ = std::clamp(styleMeter_ + 0.08f + player_->GetComboStep() * 0.05f, 0.0f, 1.0f);
            enemy_->TakeDamage(1);
        }
    }
    if (player_->JustFired()) {
        const GunShotDef*       shot = player_->GetActiveGunShot();
        const RangedWeaponData& gun  = wm->GetRanged();
        const float rangeX = gun.range * ((shot != nullptr) ? shot->rangeMult : 1.0f);
        // 銃口の向きにだけ飛ぶ（背後は銃身ぶんの余裕のみ）
        AABB shotRange = SceneShared::MakeDirectionalRange(
            ppos, player_->GetLastDirX(), rangeX, 0.8f);
        if (Collision::CheckCollision(shotRange, enemyAABB)) {
            // 段が進むほどスタイルが伸びる（銃コンボを回す動機付け）
            float gain = 0.04f + ((shot != nullptr) ? player_->GetGunComboStep() * 0.01f : 0.0f);
            styleMeter_ = std::clamp(styleMeter_ + gain, 0.0f, 1.0f);
            enemy_->TakeDamage(1);
        }
    }
    if (player_->JustBlinked()) {
        styleMeter_ = std::clamp(styleMeter_ + 0.10f, 0.0f, 1.0f);
    }
    if (player_->JustRampageHit()) {
        AABB rushRange = { { ppos.x - 2.5f, ppos.y - 1.5f, -0.5f },
                           { ppos.x + 2.5f, ppos.y + 1.5f,  0.5f } };
        if (Collision::CheckCollision(rushRange, enemyAABB)) {
            // 乱舞スラッシュ：回数が増えるほど多くゲージが溜まる
            styleMeter_ = std::clamp(
                styleMeter_ + 0.10f + player_->GetJuggleCount() * 0.02f, 0.0f, 1.0f);
            enemy_->TakeDamage(1);
        }
    }
    // フィニッシャースラッシュのダメージは UpdateFinisherSlash の本命ヒットで適用する

    float decayMult = RunData::GetInstance()->HasSkill(RunData::Skill::StylePersist) ? 0.6f : 1.0f;
    styleMeter_ = std::clamp(styleMeter_ - 0.12f * dt * decayMult, 0.0f, 1.0f);
    peakStyle_  = (std::max)(peakStyle_, styleMeter_);

    DrawStyleUI();
}

void GamePlayScene::UpdateParticles(float dt)
{
    auto* tm = TimeManager::GetInstance();
    const Vector3& ppos = player_->GetPosition();

    // 着地ほこり
    if (player_->JustLanded()) {
        std::uniform_real_distribution<float> vxL(-3.5f, -1.2f);
        std::uniform_real_distribution<float> vxR( 1.2f,  3.5f);
        std::uniform_real_distribution<float> vyD( 0.8f,  2.2f);
        for (int i = 0; i < 4; ++i) {
            pm_->EmitGravity("land_dust", ppos, { vxL(rng_), vyD(rng_), 0.0f },
                { 0.85f, 0.78f, 0.65f, 0.7f }, 0.4f, 0.22f);
            pm_->EmitGravity("land_dust", ppos, { vxR(rng_), vyD(rng_), 0.0f },
                { 0.85f, 0.78f, 0.65f, 0.7f }, 0.4f, 0.22f);
        }
    }

    // ジャンプ煙
    if (player_->JustJumped()) {
        pm_->EmitRing("jump_smoke", ppos, 1.8f, { 0.9f, 0.9f, 0.9f, 0.45f }, 7, 0.22f, 0.28f);
    }

    // 残像: 横移動 or 空中 → プレイヤーモデルのゴーストを一定間隔でスポーン
    bool movingX = input_->PushKey(DIK_A) || input_->PushKey(DIK_LEFT)
                || input_->PushKey(DIK_D) || input_->PushKey(DIK_RIGHT);
    if (movingX || !player_->IsOnGround()) {
        ghostSpawnTimer_ -= dt;
        if (ghostSpawnTimer_ <= 0.0f) {
            ghostSpawnTimer_ = 0.05f;
            ghostTrail_.push_back({ ppos, 0.0f });
        }
    } else {
        ghostSpawnTimer_ = 0.0f;
    }
    for (auto& g : ghostTrail_) { g.age += dt; }
    while (!ghostTrail_.empty() && ghostTrail_.front().age >= kGhostLifetime) {
        ghostTrail_.pop_front();
    }

    // 敵との当たり判定
    hitCooldown_ -= dt;
    {
        Collider playerCol = player_->GetCollider();
        const Vector3& epos = enemy_->GetPosition();
        AABB enemyAABB = { { epos.x - 0.5f, epos.y - 0.5f, -0.5f },
                           { epos.x + 0.5f, epos.y + 0.5f,  0.5f } };
        if (Collision::CheckCollision(playerCol.aabb, enemyAABB) && hitCooldown_ <= 0.0f) {
            hitCooldown_ = 0.5f;
            enemy_->TakeDamage(1);
            tm->RequestHitStop(5);
            cameraShaker_.Request(0.18f, 0.15f);

            Vector3 hitPos = { (ppos.x + epos.x) * 0.5f,
                               (ppos.y + epos.y) * 0.5f, 0.0f };
            pm_->EmitRing("hit_ring", hitPos, 4.0f, { 1.0f, 0.85f, 0.2f, 1.0f }, 16, 0.3f, 0.2f);
            std::uniform_real_distribution<float> vxD(-3.0f, 3.0f);
            std::uniform_real_distribution<float> vyD(2.0f, 5.5f);
            for (int i = 0; i < 8; ++i) {
                pm_->EmitGravity("hit_spark", hitPos,
                    { vxD(rng_), vyD(rng_), 0.0f },
                    { 1.0f, 0.55f, 0.1f, 1.0f }, 0.7f, 0.15f);
            }
        }
    }

    // スタイル技エフェクト
    auto* wm = WeaponManager::GetInstance();
    const auto& styles = wm->GetList();

    if (player_->JustComboHit()) {
        int   step = player_->GetComboStep();
        float dir  = player_->GetLastDirX();
        float ang  = (dir > 0.0f) ? 0.0f : GameConstants::kPi;
        const auto& sc = styles[wm->GetIndex()].styleColor;
        Vector4 col = { sc[0], sc[1], sc[2], sc[3] };
        float   rad = 0.8f + (step - 1) * 0.45f;
        pm_->EmitSlash("sword_slash", ppos, ang, col, rad);
        if (step == 3) {
            pm_->EmitRing("sword_slash", ppos, 3.5f, col, 10, 0.3f, 0.22f);
        }

        tm->RequestHitStop(3);
        cameraShaker_.Request(0.10f * step, 0.10f);
    }

    if (player_->JustFired()) {
        // 弾数・拡散・色は選択中の銃と段の定義に従う（散弾は扇状に、単発は直線に飛ぶ）
        const GunShotDef*       shot = player_->GetActiveGunShot();
        const RangedWeaponData& gun  = wm->GetRanged();
        float dir = player_->GetLastDirX();
        Vector4 col = { gun.color[0], gun.color[1], gun.color[2], gun.color[3] };
        int   n      = (shot != nullptr) ? (std::max)(shot->bullets, 2) : 4;
        float spread = (shot != nullptr) ? shot->spreadDeg * GameConstants::kDegToRad : 0.15f;
        for (int i = 0; i < n; ++i) {
            float t     = (n > 1) ? (i / (n - 1.0f) - 0.5f) : 0.0f; // -0.5〜+0.5
            float speed = 7.0f + i * 1.0f;
            pm_->EmitWithColor("gun_shot", ppos,
                { dir * speed, speed * spread * t, 0.0f },
                col, 0.35f, 0.14f);
        }
    }

    if (player_->JustBlinked()) {
        const auto& sc = styles[2].styleColor;
        Vector4 col = { sc[0], sc[1], sc[2], 0.75f };
        pm_->EmitRing("blink_trail", ppos, 2.8f, col, 10, 0.28f, 0.17f);
    }

    if (player_->JustChargedGauge()) {
        pm_->EmitRing("awaken_aura", ppos, 1.6f, { 0.75f, 0.25f, 1.0f, 0.9f }, 8, 0.38f, 0.2f);
    }

    // 覚醒中オーラ（連続）
    if (player_->IsAwakened()) {
        auraTimer_ += dt;
        if (auraTimer_ >= 0.07f) {
            auraTimer_ = 0.0f;
            pm_->EmitWithColor("awaken_aura", ppos,
                { 0.0f, 0.4f, 0.0f },
                { 1.0f, 0.88f, 0.25f, 0.45f }, 0.45f, 0.28f, true);
        }
    } else {
        auraTimer_ = 0.0f;
    }
}

void GamePlayScene::UpdateFinisherSlash(float dt)
{
    if (!finisherActive_) { return; }

    finisherBeatTimer_ -= dt;
    if (finisherBeatTimer_ > 0.0f) { return; }

    auto*          tm   = TimeManager::GetInstance();
    const Vector3& epos = enemy_->GetPosition();

    if (finisherLineIdx_ < GameConstants::kFinisherSlashLines) {
        // カメラ視界全体にランダムな位置を高速で斬り刻む
        const Vector3& cam = camera_->GetTranslate();
        std::uniform_real_distribution<float> angleDist(0.0f, GameConstants::kTwoPi);
        std::uniform_real_distribution<float> offXDist(-GameConstants::kCameraHalfW, GameConstants::kCameraHalfW);
        std::uniform_real_distribution<float> offYDist(-GameConstants::kCameraHalfH, GameConstants::kCameraHalfH);
        std::uniform_real_distribution<float> lenDist(4.0f, 9.0f);
        std::uniform_real_distribution<float> thickDist(3.0f, 7.0f);
        const float   ang    = angleDist(rng_);
        const Vector2 dir    = { std::cos(ang), std::sin(ang) };
        const Vector2 center = { cam.x + offXDist(rng_), cam.y + offYDist(rng_) };
        const float   len    = lenDist(rng_);

        // 解放の瞬間まで全ての斬撃線を画面に残す
        const float duration = (GameConstants::kFinisherSlashLines - 1 - finisherLineIdx_) * GameConstants::kFinisherLineInterval
                             + GameConstants::kFinisherImpactDelay + 0.25f;
        SceneShared::SpawnSlashMarkWorld(
            { center.x - dir.x * len, center.y - dir.y * len },
            { center.x + dir.x * len, center.y + dir.y * len },
            cam.x, cam.y, { 0.75f, 0.95f, 1.0f, 1.0f }, thickDist(rng_), duration);

        // 1本ごとに実際にヒットさせ、敵を空中に拘束し続ける
        enemy_->TakeDamage(GameConstants::kFinisherLineDamage);
        enemy_->Launch(0.10f);
        styleMeter_ = std::clamp(styleMeter_ + 0.02f, 0.0f, 1.0f);

        tm->RequestHitStop(GameConstants::kHitStopFinisherBeat);
        cameraShaker_.Request(0.06f, 0.05f);
        SceneShared::EmitFinisherSlashLine(pm_, "sword_slash", "hit_spark",
            { center.x, center.y, 0.0f }, ang, len);

        // 空間にガラス質の刃を明滅させ、歪みを脈動させる
        bladeFlash_.Emit({ center.x, center.y, epos.z }, 3, 4.0f, 1.2f, 2.8f);
        spaceWarp_.AddImpulse(0.12f);

        finisherLineIdx_++;
        finisherBeatTimer_ = (finisherLineIdx_ < GameConstants::kFinisherSlashLines)
            ? GameConstants::kFinisherLineInterval
            : GameConstants::kFinisherImpactDelay;
        return;
    }

    // 解放：溜めた斬撃が一斉に炸裂する
    finisherActive_ = false;
    tm->RequestHitStop(GameConstants::kHitStopFinisherSlash);
    cameraShaker_.Request(GameConstants::kShakeFinisherSlashAmt, GameConstants::kShakeFinisherSlashDur);
    styleMeter_ = std::clamp(styleMeter_ + 0.35f, 0.0f, 1.0f);
    enemy_->TakeDamage(GameConstants::kFinisherSlashDamage);
    enemy_->Launch(GameConstants::kLaunchSpeed);

    // 敵本体を切断破片に差し替える（演出が飛散に移るまで本体は非表示）
    enemySlice_.Start(enemy_->GetModel(), epos, { 1.0f, 1.0f, 1.0f }, rng_());
    enemy_->SetVisible(false);

    // 解放の瞬間：刃の一斉放出と空間歪みの最大化
    bladeFlash_.Emit(epos, 30, GameConstants::kFinisherSlashRadius, 2.0f, 5.0f);
    spaceWarp_.AddImpulse(1.0f);

    // 白閃光とともに「暗転+斬撃線ごと凍った画面」を敵位置から砕き、素の世界を見せる
    ScreenFlash::GetInstance()->Request({ 0.75f, 0.95f, 1.0f, 0.5f }, 0.15f);
    {
        const Matrix4x4 vp = Multiply(camera_->GetViewMatrix(), camera_->GetProjectionMatrix());
        const float cx = epos.x * vp.m[0][0] + epos.y * vp.m[1][0] + epos.z * vp.m[2][0] + vp.m[3][0];
        const float cy = epos.x * vp.m[0][1] + epos.y * vp.m[1][1] + epos.z * vp.m[2][1] + vp.m[3][1];
        const float cw = epos.x * vp.m[0][3] + epos.y * vp.m[1][3] + epos.z * vp.m[2][3] + vp.m[3][3];
        if (cw > 0.0001f) {
            finisherShatter_.SetImpactUV(cx / cw * 0.5f + 0.5f, 0.5f - cy / cw * 0.5f);
        }
    }
    finisherShatter_.Reset();
    finisherShatter_.Start();

    // 溜めた斬撃線を一斉に白く光らせてから消し、太く短い閃光の斬撃線を重ねる
    SlashMark::GetInstance()->FlashAll({ 1.0f, 1.0f, 1.0f, 1.0f }, 0.22f);
    std::uniform_real_distribution<float> angleDist(0.0f, GameConstants::kTwoPi);
    const Vector3& cam = camera_->GetTranslate();
    for (int i = 0; i < 8; ++i) {
        const float   ang = angleDist(rng_);
        const Vector2 dir = { std::cos(ang), std::sin(ang) };
        SceneShared::SpawnSlashMarkWorld(
            { epos.x - dir.x * GameConstants::kFinisherSlashRadius,
              epos.y - dir.y * GameConstants::kFinisherSlashRadius },
            { epos.x + dir.x * GameConstants::kFinisherSlashRadius,
              epos.y + dir.y * GameConstants::kFinisherSlashRadius },
            cam.x, cam.y, { 1.0f, 1.0f, 1.0f, 1.0f }, 9.0f, 0.15f);
    }

    SceneShared::EmitFinisherRelease(pm_, "hit_ring", "hit_spark", epos);
}

void GamePlayScene::CheckClearCondition()
{
    // ローグライト: 敵撃破でクリア（大技・切断演出は見せ切ってから遷移する）
    if (!clearTriggered_ && enemy_->IsDefeated()
        && !finisherActive_ && !enemySlice_.IsActive()
        && RunData::GetInstance()->IsRunActive()) {
        requestClear_ = true;
    }

    if (requestClear_ || gameTime_.IsCleared()) {
        requestClear_   = false;
        clearTriggered_ = true;
        if (!RunData::GetInstance()->IsRunActive()) {
            glassShatter_.Start();
        }
    }
}

D3D12_CPU_DESCRIPTOR_HANDLE GamePlayScene::GetActiveRTVHandle() const
{
    return SceneShared::GetActiveRTVHandle(dxCommon_, { imageFilter_, grayscaleEffect_, hsvFilter_ });
}

void GamePlayScene::SetupMainRenderTarget()
{
    SceneShared::SetupMainRenderTarget(dxCommon_, { imageFilter_, grayscaleEffect_, hsvFilter_ });
}

void GamePlayScene::SetupModelRenderState()
{
    modelCommon_->CommonDrawSettings();
    objectCommon_->SetDefaultLight(dxCommon_->GetCommandList());
    shadowManager_->SetShadowMap(dxCommon_->GetCommandList(), srvManager_);
}

void GamePlayScene::DrawShadowPass()
{
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();

    shadowManager_->BeginShadowPass(commandList);
    modelCommon_->BeginShadowPass();
    shadowManager_->EndShadowPass(commandList);
}

void GamePlayScene::Draw()
{
    // ---- クリア演出中（かつキャプチャ済み）はシーン描画をスキップ ----
    if (clearTriggered_ && RunData::GetInstance()->IsRunActive() && showResult_) {
        spriteCommon_->CommonDrawSettings();
        clearBgSprite_->SetColor({ 0.0f, 0.0f, 0.0f, 1.0f });
        clearBgSprite_->Update();
        clearBgSprite_->Draw();
        const char* rank = RunData::CalcRank(peakStyle_);
        fontRenderer_.Reset();
        fontRenderer_.DrawString("CLEAR!",     490.0f, 200.0f, 4.0f, { 1.0f, 1.0f, 0.3f, 1.0f });
        fontRenderer_.DrawString("Style:",     420.0f, 310.0f, 3.0f, { 0.8f, 0.8f, 0.8f, 1.0f });
        fontRenderer_.DrawString(rank,         580.0f, 305.0f, 4.0f, { 1.0f, 0.5f, 0.1f, 1.0f });
        char goldBuf[32];
        snprintf(goldBuf, sizeof(goldBuf), "+%dG", lastGold_);
        fontRenderer_.DrawString(goldBuf,      540.0f, 400.0f, 3.0f, { 0.9f, 0.85f, 0.2f, 1.0f });
        fontRenderer_.Draw();
        return;
    }
    if (clearTriggered_ && IsGlassShatterFlow() && !glassShatter_.NeedCapture()) {
        spriteCommon_->CommonDrawSettings();
        clearBgSprite_->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
        clearBgSprite_->Update();
        clearBgSprite_->Draw();
        glassShatter_.Apply();
        return;
    }

    renderTexture_->BeginRendering();
    renderTexture_->EndRendering();

    DrawShadowPass();
    SetupMainRenderTarget();

    spriteCommon_->CommonDrawSettings();
    shadowManager_->SetShadowMap(dxCommon_->GetCommandList(), srvManager_);
    renderTextureSprite_->Update();
    renderTextureSprite_->Draw();

    spriteCommon_->CommonDrawSettings();
    waterPool_->Draw(camera_.get());

    SetupModelRenderState();
    skydome_->Draw();

    SetupModelRenderState();

    for (auto& obj : gameObjects_) { obj->Draw(); }
    for (auto& block : borderBlocks_) { block->Draw(); }

    if (!ghostTrail_.empty()) {
        SetupModelRenderState();
        ghostObject_->SetModel(player_->GetModel()); // 覚醒フォーム切り替えに残像の見た目を追従させる
        for (const auto& g : ghostTrail_) {
            float alpha = (1.0f - g.age / kGhostLifetime) * 0.5f;
            ghostObject_->SetPosition(g.pos);
            ghostObject_->SetColor({ 0.4f, 0.75f, 1.0f, alpha });
            ghostObject_->Update();
            ghostObject_->Draw();
        }
    }

    player_->Draw();
    enemy_->Draw();
    enemySlice_.Draw();

    pm_->Update(camera_.get());
    pm_->Draw(camera_.get());

    bladeFlash_.Draw();

    // 空間歪み（バックバッファ直描き時のみUIより先に画面をキャプチャして歪ませる）
    if (spaceWarp_.IsActive()
        && GetActiveRTVHandle().ptr == dxCommon_->GetCurrentBackBufferHandle().ptr) {
        spaceWarp_.CaptureAndApply();
        SetupMainRenderTarget(); // 歪み描画で変わったレンダーターゲット設定を戻す
    }

    spriteCommon_->CommonDrawSettings();
    shadowManager_->SetShadowMap(dxCommon_->GetCommandList(), srvManager_);

    for (auto& e : sceneEditor_.GetUIElements()) {
        e.sprite->Update();
        e.sprite->Draw();
    }

    // 大技中と解放フレーム（凍結画面のキャプチャ前）だけ暗転を重ねる
    // 解放後の暗さは砕け散る凍結画面が持ち去るので、素の世界には重ねない
    const bool captureFrame = finisherShatter_.IsActive() && finisherShatter_.NeedCapture();
    if (finisherActive_ || captureFrame) {
        finisherOverlay_->SetColor({ 0.0f, 0.0f, 0.05f, GameConstants::kFinisherOverlayAlpha });
        finisherOverlay_->Update();
        finisherOverlay_->Draw();
    }
    SlashMark::GetInstance()->Draw();

    // ---- 解放時の世界割れ（暗転+斬撃線ごと凍った画面を砕き、下から素の世界が現れる）----
    if (finisherShatter_.IsActive()
        && GetActiveRTVHandle().ptr == dxCommon_->GetCurrentBackBufferHandle().ptr) {
        if (finisherShatter_.NeedCapture()) {
            finisherShatter_.CaptureFrame();
        }
        finisherShatter_.Apply();

        // Apply が変えたレンダーターゲットとルートシグネチャを後続のスプライト描画用に戻す
        SetupMainRenderTarget();
        spriteCommon_->CommonDrawSettings();
    }

    // ----- ゲームプレイ UI テキスト -----
    fontRenderer_.Draw();

    // ---- ガラス割れエフェクト（サンドボックスのクリア演出 / デバッグテスト再生時のみ）----
    if (clearTriggered_ && IsGlassShatterFlow()) {
        if (glassShatter_.NeedCapture()) {
            glassShatter_.CaptureFrame();
        }
        glassShatter_.Apply();
    }
}

void GamePlayScene::DrawRogueliteHUD()
{
    auto* rd = RunData::GetInstance();
    if (!rd->IsRunActive()) { return; }

    // 敵HPバー（上部中央）
    int eHp    = enemy_->GetHp();
    int eMaxHp = enemy_->GetMaxHp();
    int filled = (eMaxHp > 0) ? (eHp * 20 / eMaxHp) : 0;
    std::string hpBar = "ENEMY [";
    for (int i = 0; i < 20; ++i) { hpBar += (i < filled ? '#' : ' '); }
    hpBar += "] ";
    hpBar += std::to_string(eHp) + "/" + std::to_string(eMaxHp);
    fontRenderer_.DrawString(hpBar.c_str(), 280.0f, 10.0f, 1.5f, { 1.0f, 0.35f, 0.35f, 1.0f });

    // プレイヤーHP + ゴールド（左上）
    std::string info = "HP:" + std::to_string(rd->GetHp()) + "/" + std::to_string(rd->GetMaxHp())
                     + "  G:" + std::to_string(rd->GetGold());
    fontRenderer_.DrawString(info.c_str(), 10.0f, 10.0f, 1.5f, { 0.3f, 1.0f, 0.4f, 1.0f });
}

void GamePlayScene::DrawStyleUI()
{
    fontRenderer_.Reset();

    DrawRogueliteHUD();
    DrawRankAndAwakenGauge();
    DrawStyleCommands();
}

void GamePlayScene::DrawRankAndAwakenGauge()
{
    // ══════════════════════════════════════════════════════
    // 右上：コンボランク ＋ 覚醒ゲージ
    // ══════════════════════════════════════════════════════
    constexpr float kScale = 1.5f;
    constexpr float kLineH = FontRenderer::kCharH * kScale + 4.0f;

    const float gauge    = player_->GetAwakenGauge();
    const bool  awakened = player_->IsAwakened();

    // ランク算出
    struct RankInfo { const char* label; Vector4 color; };
    RankInfo ri;
    if      (styleMeter_ >= 0.90f) { ri = { "SSS", { 1.0f, 0.15f, 0.15f, 1.0f } }; }
    else if (styleMeter_ >= 0.70f) { ri = { "SS",  { 1.0f, 0.85f, 0.00f, 1.0f } }; }
    else if (styleMeter_ >= 0.50f) { ri = { "S",   { 0.2f, 0.85f, 1.00f, 1.0f } }; }
    else if (styleMeter_ >= 0.30f) { ri = { "A",   { 0.95f,0.55f, 0.15f, 1.0f } }; }
    else if (styleMeter_ >= 0.15f) { ri = { "B",   { 0.85f,0.85f, 0.20f, 1.0f } }; }
    else if (styleMeter_ >= 0.05f) { ri = { "C",   { 0.85f,0.85f, 0.85f, 1.0f } }; }
    else                           { ri = { "D",   { 0.45f,0.45f, 0.45f, 1.0f } }; }

    // ランク文字（大きく右揃え）
    constexpr float kRankScale = 4.0f;
    int   rankLen = static_cast<int>(strlen(ri.label));
    float rankX   = 1260.0f - rankLen * FontRenderer::kCharW * kRankScale;
    fontRenderer_.DrawString(ri.label, rankX, 20.0f, kRankScale, ri.color);

    // 覚醒ゲージ（ランクの下）
    float gy = 20.0f + FontRenderer::kCharH * kRankScale + 6.0f;

    if (awakened) {
        fontRenderer_.DrawStringW(L"★ 覚醒中!", 1030.0f, gy, kScale,
            { 1.0f, 0.88f, 0.15f, 1.0f });
    }
    gy += kLineH;

    {
        bool ready = (gauge >= 0.3f);
        bool maxed = (gauge >= 1.0f);
        Vector4 col = maxed ? Vector4{ 1.0f,0.95f,0.3f,1.0f }
                    : ready ? Vector4{ 0.85f,0.5f,1.0f,1.0f }
                            : Vector4{ 0.45f,0.45f,0.45f,1.0f };
        const wchar_t* label = maxed ? L"覚醒ゲージ 満タン！[F]で発動"
                             : ready ? L"覚醒ゲージ [R]で発動"
                                     : L"覚醒ゲージ";
        fontRenderer_.DrawStringW(label, 1030.0f, gy, kScale, col);
    }
    gy += kLineH;

    {
        int filled = std::clamp(static_cast<int>(gauge * 16.0f), 0, 16);
        std::string bar = "[";
        for (int i = 0; i < 16; ++i) { bar += (i < filled ? '#' : ' '); }
        bar += "] ";
        bar += std::to_string(static_cast<int>(gauge * 100.0f)) + "%";
        Vector4 col = awakened ? Vector4{ 1.0f,0.85f,0.0f,1.0f }
                               : Vector4{ 0.55f,0.15f,0.9f,1.0f };
        fontRenderer_.DrawString(bar, 1030.0f, gy, kScale, col);
    }
}

void GamePlayScene::DrawStyleCommands()
{
    // ══════════════════════════════════════════════════════
    // 右側：スタイルコマンド UI
    // ══════════════════════════════════════════════════════
    constexpr float kScale = 1.5f;
    constexpr float kLineH = FontRenderer::kCharH * kScale + 4.0f;

    auto* wm = WeaponManager::GetInstance();
    const WeaponData& style = wm->GetCurrent();
    const int         idx   = wm->GetIndex();
    const int         total = wm->GetCount();
    const int         combo = player_->GetComboStep();

    constexpr float kX = 780.0f;
    float y = 448.0f;

    // スタイルインジケーター [1][2][3][4]
    float bx = kX;
    for (int i = 0; i < total; ++i) {
        const auto& w = wm->GetList()[i];
        Vector4 col = (i == idx)
            ? Vector4{ w.styleColor[0], w.styleColor[1], w.styleColor[2], w.styleColor[3] }
            : Vector4{ 0.45f, 0.45f, 0.45f, 1.0f };
        std::string btn = "[" + std::to_string(i + 1) + "]";
        fontRenderer_.DrawString(btn, bx, y, kScale, col);
        bx += static_cast<float>(btn.size()) * FontRenderer::kCharW * kScale + 2.0f;
    }
    y += kLineH;

    // スタイル名（日本語）
    Vector4 styleCol{ style.styleColor[0], style.styleColor[1],
                      style.styleColor[2], style.styleColor[3] };
    fontRenderer_.DrawStringW(style.styleNameJp, kX, y, kScale, styleCol);
    y += kLineH;

    fontRenderer_.DrawString("--------------------", kX, y, kScale,
        { 0.35f, 0.35f, 0.35f, 1.0f });
    y += kLineH;

    // コマンド一覧（キーは基本ASCIIだが「空中L」等の日本語混じりもあるためUTF-8として変換する）
    for (const auto& cmd : style.commands) {
        std::wstring line = L"[" + StringUtility::ConvertString(cmd.key) + L"] " + cmd.desc;
        fontRenderer_.DrawStringW(line, kX, y, kScale, { 0.85f, 0.85f, 0.85f, 1.0f });
        y += kLineH;
    }

    // 選択中の銃（Gキー切替、Kキーで銃種別のコンボ）
    const RangedWeaponData& gun = wm->GetRanged();
    Vector4 gunCol{ gun.color[0], gun.color[1], gun.color[2], gun.color[3] };
    std::wstring gunLine = L"銃[G]: " + gun.nameJp
        + L" (" + std::to_wstring(wm->GetRangedIndex() + 1) + L"/"
        + std::to_wstring(wm->GetRangedCount()) + L")";
    fontRenderer_.DrawStringW(gunLine, kX, y, kScale, gunCol);
    y += kLineH;

    // 格闘コンボのステップ表示（全スタイル、コンボ中のみ）
    if (combo > 0) {
        std::wstring dots = L"コンボ: ";
        int maxCombo = player_->GetComboMax();
        for (int i = 1; i <= maxCombo; ++i) {
            dots += (i <= combo) ? L"[*]" : L"[ ]";
        }
        fontRenderer_.DrawStringW(dots, kX, y, kScale, styleCol);
    }
}

bool GamePlayScene::IsGlassShatterFlow() const
{
    return glassShatterDebugTest_ || !RunData::GetInstance()->IsRunActive();
}

void GamePlayScene::TriggerGlassShatterTest()
{
    if (clearTriggered_) { return; }
    glassShatterDebugTest_ = true;
    clearTriggered_        = true;
    glassShatter_.Start();
}

void GamePlayScene::Finalize()
{
    ImGuiControlPanel::RegisterGlassShatterTrigger(nullptr);
    renderTexture_->Finalize(srvManager_);
    pm_->ClearAllGroups();
    glassShatter_.Finalize();
    finisherShatter_.Finalize();
    spaceWarp_.Finalize();
    bladeFlash_.Clear();
    SlashMark::GetInstance()->Clear();

    // 音を全部止める（BGM・SE どちらも）
    if (audio_) {
        audio_->StopBGM();
        audio_->StopAllSE();
    }
}
