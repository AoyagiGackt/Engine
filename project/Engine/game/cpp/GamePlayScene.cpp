#include "GamePlayScene.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <random>
#include "GameConstants.h"
#include "RunData.h"
#include "GrayscaleEffect.h"
#include "HsvFilter.h"
#include "ImageFilter.h"
#include "ParticleManager.h"
#include "SceneManager.h"
#include "ScoreManager.h"
#include "TextureManager.h"
#include "WeaponManager.h"

// =====================================================
// 初期化
// =====================================================

void GamePlayScene::Initialize(DirectXCommon* dxCommon, Input* input, Audio* audio)
{
    // 引数のポインタをメンバ変数に保存しておく（後で Update/Draw からも使えるように）
    dxCommon_ = dxCommon;
    input_ = input;
    audio_ = audio;

    // ----- 描画共通設定 -----
    // スプライト（2D画像）描画に必要な共通設定（シェーダーのセットアップなど）
    spriteCommon_ = std::make_unique<SpriteCommon>();
    spriteCommon_->Initialize(dxCommon_);

    // 3Dモデル描画に必要な共通設定
    modelCommon_ = std::make_unique<ModelCommon>();
    modelCommon_->Initialize(dxCommon_);

    // 3Dオブジェクト描画に必要な共通設定（ライト情報なども含む）
    objectCommon_ = std::make_unique<Object3dCommon>();
    objectCommon_->Initialize(dxCommon_);

    // ----- シングルトンをキャッシュ（以降はポインタ経由でアクセス）-----
    srvManager_      = SrvManager::GetInstance();
    scoreManager_    = ScoreManager::GetInstance();
    grayscaleEffect_ = GrayscaleEffect::GetInstance();
    imageFilter_     = ImageFilter::GetInstance();
    hsvFilter_       = HsvFilter::GetInstance();

    // ----- 影の描画（シャドウマップ）-----
    // シャドウマップ = 光源視点から深度画像を作り、それを使ってオブジェクトの影を描く手法
    shadowManager_ = std::make_unique<ShadowManager>();
    shadowManager_->Initialize(dxCommon_, srvManager_);

    // ----- カメラ -----
    camera_ = std::make_unique<Camera>();
    camera_->SetTranslate({ 19.0f, 6.0f, -30.0f }); // 初期位置（少し高く、手前から見下ろす角度）
    Object3d::SetCommonCamera(camera_.get()); // 全 3D オブジェクトがこのカメラを使うよう設定

    // ----- 天球（Skydome）-----
    modelSkydome_ = std::make_unique<Model>();
    modelSkydome_->Initialize(modelCommon_.get(),
        "Resources/SkyDome/SkyDome.obj",
        "Resources/SkyDome/skySphere.png");

    skydome_ = std::make_unique<Skydome>();
    skydome_->Initialize(modelCommon_.get(), modelSkydome_.get());

    // ----- 画面を囲むブロック -----
    modelBlock_ = std::make_unique<Model>();
    modelBlock_->Initialize(modelCommon_.get(),
        "Resources/block/block.obj",
        "Resources/block/block.png");

    // カメラ (14.5, 6, -30), fovY=0.45rad で Z=0 面の可視範囲:
    //   X: 約 2〜27、Y: 約 -1〜13
    // ブロックを X=0〜28、Y=-1〜13 の矩形フレームに配置する
    auto addBlock = [&](float x, float y, float z) {
        auto block = std::make_unique<Object3d>();
        block->Initialize(modelCommon_.get());
        block->SetModel(modelBlock_.get());
        block->SetEnableLighting(false);
        block->SetPosition({ x, y, z });
        block->Update();
        borderBlocks_.push_back(std::move(block));
    };

    // 下段（床）: Y=-0.6
    for (int x = 0; x <= 36; ++x) { addBlock(static_cast<float>(x), -0.6f,  0.0f); }
    // 上段（天井）: Y=13
    for (int x = 0; x <= 36; ++x) { addBlock(static_cast<float>(x), 13.0f, 0.0f); }
    // 左列: Y=0〜12（Y=0 で床ブロックと重なり、隙間を埋める）
    for (int y = 0; y <= 12; ++y) { addBlock(2.0f,  static_cast<float>(y), 0.0f); }
    // 右列: Y=0〜12
    for (int y = 0; y <= 12; ++y) { addBlock(36.0f, static_cast<float>(y), 0.0f); }

    // ----- プレイヤー -----
    player_ = std::make_unique<Player>();
    player_->Initialize(modelCommon_.get());

    // ローグライトモード: スキル適用
    {
        auto* rd = RunData::GetInstance();
        if (rd->isRunActive) {
            Player::SkillMods mods;
            if (rd->HasSkill(RunData::Skill::BlinkPlus))    mods.blinkDistMult    = 1.5f;
            if (rd->HasSkill(RunData::Skill::ComboExtend))  mods.comboMaxBonus    = 1;
            if (rd->HasSkill(RunData::Skill::FastFire))     mods.fireIntervalMult = 0.5f;
            if (rd->HasSkill(RunData::Skill::AwakenBoost))  mods.gaugeChargeMult  = 1.5f;
            if (rd->HasSkill(RunData::Skill::SpeedUp))      mods.speedMult        = 1.2f;
            if (rd->HasSkill(RunData::Skill::HighJump))     mods.jumpMult         = 1.25f;
            if (rd->HasSkill(RunData::Skill::JuggleExtend)) mods.juggleMaxBonus   = 4;
            player_->ApplySkillMods(mods);
        }
    }

    // ----- 敵 -----
    enemy_ = std::make_unique<EnemyEntity>();
    enemy_->Initialize(modelCommon_.get(), { 28.0f, 0.4f, 0.0f });
    // ローグライトモード: 敵HPをノードタイプに応じて設定
    {
        auto* rd = RunData::GetInstance();
        if (rd->isRunActive) {
            int hp = 20;
            if (rd->currentNode == RunData::NodeType::Elite) hp = 35;
            else if (rd->currentNode == RunData::NodeType::Boss) hp = 60;
            enemy_->SetMaxHp(hp);
        }
    }

    // スコアをファイルから読み込み、今回プレイのスコアをリセットする
    scoreManager_->LoadScores();
    scoreManager_->ResetCurrentScore();

    // ゲーム内時刻を初期化（ゲーム開始 = 0時0分）
    gameTime_.Initialize();

    // ----- オフスクリーンレンダリング -----
    // 画面に直接描かずに一旦テクスチャに描いておく仕組み
    // これにより、後からポストエフェクト（グレースケールなど）を重ねて適用できる
    renderTexture_ = std::make_unique<RenderTexture>();
    renderTexture_->Initialize(dxCommon_, srvManager_,
        WinApp::kClientWidth, WinApp::kClientHeight);

    // レンダーテクスチャを画面いっぱいに貼るためのスプライト
    renderTextureSprite_ = std::make_unique<Sprite>();
    renderTextureSprite_->Initialize(spriteCommon_.get(), "Resources/white.png");
    renderTextureSprite_->SetExternalTexture(renderTexture_->GetSrvIndex()); // テクスチャをレンダーテクスチャに差し替え
    renderTextureSprite_->SetPosition({ 0.0f, 0.0f });
    renderTextureSprite_->SetSize({ static_cast<float>(WinApp::kClientWidth),
                                    static_cast<float>(WinApp::kClientHeight) });

    // ----- クリア演出の背景スプライト（白背景のみ）-----
    // ガラスが割れている間、青いクリアカラーの代わりに白を表示する
    // スコア・ランキングは ClearScene で初めて出す（二重表示防止）
    clearBgSprite_ = std::make_unique<Sprite>();
    clearBgSprite_->Initialize(spriteCommon_.get(), "Resources/white.png");
    clearBgSprite_->SetPosition({ 0.0f, 0.0f });
    clearBgSprite_->SetSize({ static_cast<float>(WinApp::kClientWidth),
                               static_cast<float>(WinApp::kClientHeight) });

    // ----- パーティクルグループ登録 -----
    pm_ = ParticleManager::GetInstance();
    pm_->CreateParticleGroup("hit_ring",    "Resources/circle2.png");
    pm_->CreateParticleGroup("hit_spark",   "Resources/circle2.png");
    pm_->CreateParticleGroup("land_dust",   "Resources/circle2.png");
    pm_->CreateParticleGroup("jump_smoke",  "Resources/circle2.png");
    pm_->CreateParticleGroup("sword_slash", "Resources/circle2.png");
    pm_->CreateParticleGroup("gun_shot",    "Resources/circle2.png");
    pm_->CreateParticleGroup("blink_trail", "Resources/circle2.png");
    pm_->CreateParticleGroup("awaken_aura", "Resources/circle2.png");
    pm_->SetAdditiveBlend("hit_ring",    true);
    pm_->SetAdditiveBlend("hit_spark",   true);
    pm_->SetAdditiveBlend("land_dust",   false);
    pm_->SetAdditiveBlend("jump_smoke",  false);
    pm_->SetAdditiveBlend("sword_slash", true);
    pm_->SetAdditiveBlend("gun_shot",    true);
    pm_->SetAdditiveBlend("blink_trail", true);
    pm_->SetAdditiveBlend("awaken_aura", true);

    waterPool_ = std::make_unique<WaterPool>();
    waterPool_->Initialize(spriteCommon_.get());

    fontRenderer_.Initialize(spriteCommon_.get());

    // ----- 残像用ゴーストオブジェクト -----
    ghostObject_ = std::make_unique<Object3d>();
    ghostObject_->Initialize(modelCommon_.get());
    ghostObject_->SetModel(player_->GetModel());
    ghostObject_->SetEnableLighting(false);

    // ----- デバッグパラメータ読み込み -----
    // 前回エディタで保存したカメラ位置・UIレイアウトなどを JSON から復元する
    sceneEditor_.LoadAll(BuildEditContext());

    // カメラスムージング用の初期目標値を現在のカメラ位置から取る
    cameraTargetPos_ = camera_->GetTranslate();
    cameraTargetRot_ = camera_->GetRotate();

    glassShatter_.Initialize(dxCommon_, srvManager_);
}

// =====================================================
// EditContext 構築ヘルパー
// =====================================================

// SceneEditor（ImGuiデバッグUI）が各オブジェクトにアクセスするための「参照集」を組み立てる。
// ポインタを渡すので、エディタ側でスライダーを動かすと即座にゲーム側の変数に反映される。
SceneEditor::EditContext GamePlayScene::BuildEditContext()
{
    SceneEditor::EditContext ctx;

    ctx.camera       = camera_.get();
    ctx.skydome      = skydome_.get();
    ctx.spriteCommon = spriteCommon_.get();

    // カメラ制御パラメータへのポインタ
    ctx.cameraTargetPos    = &cameraTargetPos_;
    ctx.cameraTargetRot    = &cameraTargetRot_;
    ctx.cameraSmoothFrames = &cameraSmoothFrames_;
    ctx.cameraPosHistory   = &cameraPosHistory_;
    ctx.cameraRotHistory   = &cameraRotHistory_;

    // Skydome パラメータへのポインタ
    ctx.skyColor      = &skyColor_;
    ctx.skyRotOffsetY = &skyRotOffsetY_;

    // ゲーム内時刻（表示用の値コピー）
    ctx.gameHour   = gameTime_.GetHour();
    ctx.gameMinute = gameTime_.GetMinute();

    ctx.requestClear = &requestClear_;
    return ctx;
}

// =====================================================
// カメラスムージング（毎フレーム呼ぶ）
// =====================================================

// 過去 cameraSmoothFrames_ フレーム分の位置・角度を記録しておき、
// その平均値を実際のカメラ座標として適用する（ボックスフィルタ補間）。
// フレーム数が多いほど動きが滑らかになるが、反応が遅くなる。
void GamePlayScene::UpdateCameraSmoothing()
{
    // 今フレームの目標位置・角度を履歴に追加する
    cameraPosHistory_.push_back(cameraTargetPos_);
    cameraRotHistory_.push_back(cameraTargetRot_);

    // 履歴が指定フレーム数を超えたら古いものを先頭から捨てる
    while ((int)cameraPosHistory_.size() > cameraSmoothFrames_) {
        cameraPosHistory_.pop_front();
        cameraRotHistory_.pop_front();
    }

    // 履歴全体の平均を計算する
    Vector3 avgPos = {};
    Vector3 avgRot = {};
    for (const auto& p : cameraPosHistory_) {
        avgPos.x += p.x; avgPos.y += p.y; avgPos.z += p.z;
    }
    for (const auto& r : cameraRotHistory_) {
        avgRot.x += r.x; avgRot.y += r.y; avgRot.z += r.z;
    }
    float n = static_cast<float>(cameraPosHistory_.size());

    // 平均値をカメラに適用する
    camera_->SetTranslate({ avgPos.x / n, avgPos.y / n, avgPos.z / n });
    camera_->SetRotate({ avgRot.x / n, avgRot.y / n, avgRot.z / n });
}

// =====================================================
// 更新処理（毎フレーム呼ばれる）
// =====================================================

void GamePlayScene::Update()
{
    // ---- クリア演出中はシーン遷移待ちのみ行う ----
    if (clearTriggered_) {
        auto* rd = RunData::GetInstance();
        if (rd->isRunActive) {
            // ローグライト: 結果表示 → MAP遷移
            if (!showResult_) {
                showResult_  = true;
                resultTimer_ = 2.5f;
                lastGold_    = RunData::CalcGold(peakStyle_);
                rd->gold    += lastGold_;
                rd->floor++;
            }
            resultTimer_ -= GameConstants::kFrameDeltaTime;
            if (resultTimer_ <= 0.0f) {
                if (rd->floor >= 4) {
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
        return;
    }

    auto* tm        = TimeManager::GetInstance();
    const float dt  = tm->GetDeltaTime(); // ヒットストップ中 = 0、スロー時は比例値

    // ゲーム内時刻を1フレーム分進める
    gameTime_.Update(1.0f);

    // ----- ヒットストップ中はプレイヤー・オブジェクト物理をスキップ -----
    if (!tm->IsHitStopped()) {
        player_->Update(input_, enemy_->GetPosition());

        // 乱舞：打ち上げヒット
        if (player_->JustLaunched()) {
            enemy_->Launch(0.48f);
            const Vector3& epos = enemy_->GetPosition();
            tm->RequestHitStop(8);
            cameraShaker_.Request(0.35f, 0.28f);
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
            tm->RequestHitStop(4);
            cameraShaker_.Request(0.12f + cnt * 0.01f, 0.10f);
        }

        // 乱舞：フィニッシュ
        if (player_->JustRampageFinish()) {
            const Vector3& epos = enemy_->GetPosition();
            tm->RequestHitStop(12);
            cameraShaker_.Request(0.55f, 0.40f);
            pm_->EmitRing("hit_ring",  epos, 8.0f, { 1.0f, 0.3f, 0.3f, 1.0f }, 24, 0.5f, 0.35f);
            pm_->EmitRing("sword_slash", epos, 5.0f, { 1.0f, 1.0f, 0.5f, 1.0f }, 16, 0.45f, 0.30f);
            std::uniform_real_distribution<float> vxF(-6.0f, 6.0f);
            std::uniform_real_distribution<float> vyF( 4.0f, 10.0f);
            for (int i = 0; i < 16; ++i) {
                pm_->EmitGravity("hit_spark", epos,
                    { vxF(rng_), vyF(rng_), 0.0f },
                    { 1.0f, 0.4f + i * 0.04f, 0.1f, 1.0f }, 1.0f, 0.20f);
            }
        }

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

    // 入水・出水スプラッシュ
    if (player_->JustEnteredWater() || player_->JustExitedWater()) {
        waterPool_->EmitSplash(player_->GetPosition());
    }

    // ----- カメラをプレイヤーに追従（境界ブロックが画面外に出ないよう clamp）-----
    {
        constexpr float kBlkR  =  0.5f;
        const Vector3& ppos = player_->GetPosition();
        cameraTargetPos_ = {
            std::clamp(ppos.x,       2.0f - kBlkR + GameConstants::kCameraHalfW,  36.0f + kBlkR - GameConstants::kCameraHalfW),
            std::clamp(ppos.y + 6.0f, -0.6f - kBlkR + GameConstants::kCameraHalfH, 13.0f + kBlkR - GameConstants::kCameraHalfH),
            -30.0f
        };
    }

    // カメラのスムージング（ぬるぬる補間）を更新する
    UpdateCameraSmoothing();

    // カメラシェイク（スムージングの後に直接カメラ座標へ加算）
    {
        Vector3 shake = cameraShaker_.Update(GameConstants::kFrameDeltaTime);
        if (shake.x != 0.0f || shake.y != 0.0f) {
            Vector3 cam = camera_->GetTranslate();
            camera_->SetTranslate({ cam.x + shake.x, cam.y + shake.y, cam.z });
        }
    }

    // ----- 影の更新 -----
    shadowManager_->Update(objectCommon_->GetLightDirection());
    Object3d::SetLightViewProjection(shadowManager_->GetLightViewProjection());

    // ----- デバッグ UI 更新 -----
    sceneEditor_.Update(BuildEditContext());

    // ── スタイルメーター更新（dt=0 のときは自然に止まる）──
    if (player_->JustComboHit()) {
        styleMeter_ = std::clamp(styleMeter_ + 0.08f + player_->GetComboStep() * 0.05f, 0.0f, 1.0f);
        enemy_->TakeDamage(1);
    }
    if (player_->JustFired()) {
        styleMeter_ = std::clamp(styleMeter_ + 0.05f, 0.0f, 1.0f);
        enemy_->TakeDamage(1);
    }
    if (player_->JustBlinked()) {
        styleMeter_ = std::clamp(styleMeter_ + 0.10f, 0.0f, 1.0f);
    }
    if (player_->JustRampageHit()) {
        // 乱舞スラッシュ：回数が増えるほど多くゲージが溜まる
        styleMeter_ = std::clamp(
            styleMeter_ + 0.10f + player_->GetJuggleCount() * 0.02f, 0.0f, 1.0f);
        enemy_->TakeDamage(1);
    }
    {
        float decayMult = RunData::GetInstance()->HasSkill(RunData::Skill::StylePersist) ? 0.6f : 1.0f;
        styleMeter_ = std::clamp(styleMeter_ - 0.12f * dt * decayMult, 0.0f, 1.0f);
    }
    peakStyle_ = (std::max)(peakStyle_, styleMeter_);

    DrawStyleUI();

    // ----- パーティクル発生 -----
    {
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
        constexpr float kGhostLifetime = 0.3f;
        for (auto& g : ghostTrail_) { g.age += dt; }
        while (!ghostTrail_.empty() && ghostTrail_.front().age >= kGhostLifetime) {
            ghostTrail_.pop_front();
        }

        // 敵との当たり判定（Collision::CheckCollision を使用）
        hitCooldown_ -= dt;
        {
            Collider playerCol = player_->GetCollider();
            const Vector3& epos = enemy_->GetPosition();
            AABB enemyAABB = { { epos.x - 0.5f, epos.y - 0.5f, -0.5f },
                               { epos.x + 0.5f, epos.y + 0.5f,  0.5f } };
            if (Collision::CheckCollision(playerCol.aabb, enemyAABB) && hitCooldown_ <= 0.0f) {
                hitCooldown_ = 0.5f;
                enemy_->TakeDamage(1);

                // ヒットストップ＋カメラシェイク
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

        // ── スタイル技エフェクト ───────────────────────────────────
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
            // コンボヒット時にヒットストップ＋軽い揺れ
            tm->RequestHitStop(3);
            cameraShaker_.Request(0.10f * step, 0.10f);
        }

        if (player_->JustFired()) {
            float dir = player_->GetLastDirX();
            const auto& sc = styles[wm->GetIndex()].styleColor;
            Vector4 col = { sc[0], sc[1], sc[2], sc[3] };
            for (int i = 0; i < 4; ++i) {
                float spread = (i - 1.5f) * 0.12f;
                pm_->EmitWithColor("gun_shot", ppos,
                    { dir * (7.0f + i * 1.5f), spread, 0.0f },
                    col, 0.35f, 0.14f);
            }
        }

        if (player_->JustBlinked()) {
            const auto& sc = styles[2].styleColor;
            Vector4 col = { sc[0], sc[1], sc[2], 0.75f };
            pm_->EmitRing("blink_trail", ppos, 2.8f, col, 10, 0.28f, 0.17f);
        }

        if (player_->JustChargedGauge()) {
            Vector4 col = { 0.75f, 0.25f, 1.0f, 0.9f };
            pm_->EmitRing("awaken_aura", ppos, 1.6f, col, 8, 0.38f, 0.2f);
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

    // ローグライト: 敵撃破でクリア
    if (!clearTriggered_ && enemy_->IsDefeated() && RunData::GetInstance()->isRunActive) {
        requestClear_ = true;
    }

    // ---- クリア条件チェック ----
    if (requestClear_ || gameTime_.IsCleared()) {
        requestClear_   = false;
        clearTriggered_ = true;
        if (!RunData::GetInstance()->isRunActive) {
            glassShatter_.Start();
        }
    }
}

// =====================================================
// 描画ヘルパー
// =====================================================

D3D12_CPU_DESCRIPTOR_HANDLE GamePlayScene::GetActiveRTVHandle() const
{
    if (imageFilter_->IsEnabled())     { return imageFilter_->GetSceneRTVHandle(); }
    if (grayscaleEffect_->IsEnabled()) { return grayscaleEffect_->GetSceneRTVHandle(); }
    if (hsvFilter_->IsEnabled())       { return hsvFilter_->GetSceneRTVHandle(); }
    return dxCommon_->GetCurrentBackBufferHandle();
}

void GamePlayScene::ApplyActiveFilter()
{
    if (imageFilter_->IsEnabled()) {
        imageFilter_->Apply(srvManager_);
    } else if (grayscaleEffect_->IsEnabled()) {
        grayscaleEffect_->Apply(srvManager_);
    } else if (hsvFilter_->IsEnabled()) {
        hsvFilter_->Apply(srvManager_);
    }
}

// =====================================================
// メイン RTV セットアップ
// =====================================================

void GamePlayScene::SetupMainRenderTarget()
{
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = GetActiveRTVHandle();
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = dxCommon_->GetDsvHandle();

    commandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    D3D12_VIEWPORT vp = { 0, 0,
        static_cast<float>(WinApp::kClientWidth), static_cast<float>(WinApp::kClientHeight),
        0.0f, 1.0f };
    D3D12_RECT scissor = { 0, 0, WinApp::kClientWidth, WinApp::kClientHeight };
    commandList->RSSetViewports(1, &vp);
    commandList->RSSetScissorRects(1, &scissor);
}

void GamePlayScene::DrawShadowPass()
{
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();

    // 光源視点で深度バッファに描画する（この結果がシャドウマップになる）
    shadowManager_->BeginShadowPass(commandList);
    modelCommon_->BeginShadowPass();
    // ※ ここでシャドウキャスター（影を落とすオブジェクト）の Draw を呼ぶ場所

    shadowManager_->EndShadowPass(commandList);
}

void GamePlayScene::Draw()
{
    // ---- クリア演出中（かつキャプチャ済み）はシーン描画をスキップ ----
    if (clearTriggered_ && RunData::GetInstance()->isRunActive && showResult_) {
        // ローグライト: 結果オーバーレイ
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
    if (clearTriggered_ && !RunData::GetInstance()->isRunActive && !glassShatter_.NeedCapture()) {
        // サンドボックス: ガラス割れ
        spriteCommon_->CommonDrawSettings();
        clearBgSprite_->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
        clearBgSprite_->Update();
        clearBgSprite_->Draw();
        glassShatter_.Apply();
        return;
    }

    // renderTextureSprite_ を後段で SRV として使うために PIXEL_SHADER_RESOURCE 状態へ遷移する
    // （中身は赤でクリアされるが、すぐ上から renderTextureSprite_ で上書きされるので見えない）
    renderTexture_->BeginRendering();
    renderTexture_->EndRendering();

    // シャドウマップの生成
    DrawShadowPass();
    // メインの描画先 RTV・ビューポートをセットアップ
    SetupMainRenderTarget();

    // ----- 背景としてレンダーテクスチャを描画 -----
    // 2D描画モードに切り替えてから、オフスクリーンで作ったテクスチャを画面いっぱいに貼る
    spriteCommon_->CommonDrawSettings();
    shadowManager_->SetShadowMap(dxCommon_->GetCommandList(), srvManager_);
    renderTextureSprite_->Update();
    renderTextureSprite_->Draw();

    // ----- 水エフェクトを描画（背景の直後・3Dオブジェクトの前）-----
    spriteCommon_->CommonDrawSettings();
    waterPool_->Draw(camera_.get());

    // ----- 天球を描画（他の3Dオブジェクトより前に描く）-----
    modelCommon_->CommonDrawSettings();
    objectCommon_->SetDefaultLight(dxCommon_->GetCommandList());
    shadowManager_->SetShadowMap(dxCommon_->GetCommandList(), srvManager_);
    skydome_->Draw();

    // ----- 3Dオブジェクトを描画 -----
    modelCommon_->CommonDrawSettings();
    objectCommon_->SetDefaultLight(dxCommon_->GetCommandList());
    shadowManager_->SetShadowMap(dxCommon_->GetCommandList(), srvManager_);

    for (auto& obj : gameObjects_) {
        obj->Draw();
    }

    // ----- 境界ブロックを描画 -----
    for (auto& block : borderBlocks_) {
        block->Draw();
    }

    // ----- 残像ゴーストを描画（プレイヤーより先に描くことでプレイヤーが前面に来る）-----
    if (!ghostTrail_.empty()) {
        constexpr float kGhostLifetime = 0.3f;
        modelCommon_->CommonDrawSettings();
        objectCommon_->SetDefaultLight(dxCommon_->GetCommandList());
        shadowManager_->SetShadowMap(dxCommon_->GetCommandList(), srvManager_);
        for (const auto& g : ghostTrail_) {
            float alpha = (1.0f - g.age / kGhostLifetime) * 0.5f;
            ghostObject_->SetPosition(g.pos);
            ghostObject_->SetColor({ 0.4f, 0.75f, 1.0f, alpha });
            ghostObject_->Update();
            ghostObject_->Draw();
        }
    }

    // ----- プレイヤー・敵を描画 -----
    player_->Draw();
    enemy_->Draw();

    // ----- パーティクル更新（CS ディスパッチ）＆描画 -----
    // ※ PreDraw() がコマンドリストをリセットするため、pm_->Update は必ず Draw() 内で呼ぶ
    pm_->Update(camera_.get());
    pm_->Draw(camera_.get());

    // ----- 2D UI（ImGuiで追加したスプライト要素）-----
    // SceneEditor で追加したスプライト UI をすべて描画する
    spriteCommon_->CommonDrawSettings();
    shadowManager_->SetShadowMap(dxCommon_->GetCommandList(), srvManager_);

    for (auto& e : sceneEditor_.GetUIElements()) {
        e.sprite->Update();
        e.sprite->Draw();
    }

    // ----- ゲームプレイ UI テキスト -----
    fontRenderer_.Draw();

    // ----- フィルター適用 -----
    ApplyActiveFilter();

    // ---- ガラス割れエフェクト（サンドボックスのクリア演出時のみ）----
    if (clearTriggered_ && !RunData::GetInstance()->isRunActive) {
        if (glassShatter_.NeedCapture()) {
            glassShatter_.CaptureFrame();
        }
        glassShatter_.Apply();
    }
}

// =====================================================
// スタイル＆コマンド UI（FontRenderer）
// =====================================================

void GamePlayScene::DrawStyleUI()
{
    auto* wm = WeaponManager::GetInstance();
    const WeaponData& style = wm->GetCurrent();
    const int         idx   = wm->GetIndex();
    const int         total = wm->GetCount();

    const float gauge    = player_->GetAwakenGauge();
    const bool  awakened = player_->IsAwakened();
    const int   combo    = player_->GetComboStep();

    constexpr float kScale = 1.5f;
    constexpr float kLineH = FontRenderer::kCharH * kScale + 4.0f; // ~28px

    fontRenderer_.Reset();

    // ══════════════════════════════════════════════════════
    // ローグライト: 敵HPバー + プレイヤーHP + ゴールド
    // ══════════════════════════════════════════════════════
    {
        auto* rd = RunData::GetInstance();
        if (rd->isRunActive) {
            // 敵HPバー（上部中央）
            int eHp    = enemy_->GetHp();
            int eMaxHp = enemy_->GetMaxHp();
            int filled = (eMaxHp > 0) ? (eHp * 20 / eMaxHp) : 0;
            std::string hpBar = "ENEMY [";
            for (int i = 0; i < 20; ++i) hpBar += (i < filled ? '#' : ' ');
            hpBar += "] ";
            hpBar += std::to_string(eHp) + "/" + std::to_string(eMaxHp);
            fontRenderer_.DrawString(hpBar.c_str(), 280.0f, 10.0f, 1.5f, { 1.0f, 0.35f, 0.35f, 1.0f });

            // プレイヤーHP + ゴールド（左上）
            std::string info = "HP:" + std::to_string(rd->hp) + "/" + std::to_string(rd->maxHp)
                             + "  G:" + std::to_string(rd->gold);
            fontRenderer_.DrawString(info.c_str(), 10.0f, 10.0f, 1.5f, { 0.3f, 1.0f, 0.4f, 1.0f });
        }
    }

    // ══════════════════════════════════════════════════════
    // 右上：コンボランク ＋ 覚醒ゲージ
    // ══════════════════════════════════════════════════════
    {
        // ランク算出
        struct RankInfo { const char* label; Vector4 color; };
        RankInfo ri;
        if      (styleMeter_ >= 0.90f) ri = { "SSS", { 1.0f, 0.15f, 0.15f, 1.0f } };
        else if (styleMeter_ >= 0.70f) ri = { "SS",  { 1.0f, 0.85f, 0.00f, 1.0f } };
        else if (styleMeter_ >= 0.50f) ri = { "S",   { 0.2f, 0.85f, 1.00f, 1.0f } };
        else if (styleMeter_ >= 0.30f) ri = { "A",   { 0.95f,0.55f, 0.15f, 1.0f } };
        else if (styleMeter_ >= 0.15f) ri = { "B",   { 0.85f,0.85f, 0.20f, 1.0f } };
        else if (styleMeter_ >= 0.05f) ri = { "C",   { 0.85f,0.85f, 0.85f, 1.0f } };
        else                           ri = { "D",   { 0.45f,0.45f, 0.45f, 1.0f } };

        // ランク文字（大きく右揃え）
        constexpr float kRankScale = 4.0f;
        int   rankLen = static_cast<int>(strlen(ri.label));
        float rankX   = 1260.0f - rankLen * FontRenderer::kCharW * kRankScale;
        fontRenderer_.DrawString(ri.label, rankX, 20.0f, kRankScale, ri.color);

        // 覚醒ゲージ（ランクの下）
        float gy = 20.0f + FontRenderer::kCharH * kRankScale + 6.0f; // ~102px

        if (awakened) {
            fontRenderer_.DrawStringW(L"★ 覚醒中!", 1030.0f, gy, kScale,
                { 1.0f, 0.88f, 0.15f, 1.0f });
        }
        gy += kLineH;

        {
            bool ready = (gauge >= 0.3f);
            Vector4 col = ready ? Vector4{ 0.85f,0.5f,1.0f,1.0f }
                                : Vector4{ 0.45f,0.45f,0.45f,1.0f };
            fontRenderer_.DrawStringW(ready ? L"覚醒ゲージ [R]で発動" : L"覚醒ゲージ",
                1030.0f, gy, kScale, col);
        }
        gy += kLineH;

        {
            int filled = std::clamp(static_cast<int>(gauge * 16.0f), 0, 16);
            std::string bar = "[";
            for (int i = 0; i < 16; ++i) bar += (i < filled ? '#' : ' ');
            bar += "] ";
            bar += std::to_string(static_cast<int>(gauge * 100.0f)) + "%";
            Vector4 col = awakened ? Vector4{ 1.0f,0.85f,0.0f,1.0f }
                                   : Vector4{ 0.55f,0.15f,0.9f,1.0f };
            fontRenderer_.DrawString(bar, 1030.0f, gy, kScale, col);
        }
    }

    // ══════════════════════════════════════════════════════
    // 右側：スタイルコマンド UI
    // ══════════════════════════════════════════════════════
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

    // コマンド一覧（キーはASCII、説明は日本語）
    for (const auto& cmd : style.commands) {
        std::wstring line = L"[" + std::wstring(cmd.key.begin(), cmd.key.end()) + L"] " + cmd.desc;
        fontRenderer_.DrawStringW(line, kX, y, kScale, { 0.85f, 0.85f, 0.85f, 1.0f });
        y += kLineH;
    }

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

// =====================================================
// 終了（シーン切り替えやゲーム終了時に1度だけ呼ばれる）
// =====================================================

void GamePlayScene::Finalize()
{
    pm_->ClearAllGroups();
    glassShatter_.Finalize();

    // 音を全部止める（BGM・SE どちらも）
    if (audio_) {
        audio_->StopBGM();
        audio_->StopAllSE();
    }
}
