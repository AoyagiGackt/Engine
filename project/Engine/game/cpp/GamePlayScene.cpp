#include "GamePlayScene.h"
#include <cmath>
#include "GameConstants.h"
#include "GrayscaleEffect.h"
#include "HsvFilter.h"
#include "ImageFilter.h"
#include "SceneManager.h"
#include "ScoreManager.h"
#include "TextureManager.h"

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
    camera_->SetTranslate({ 14.5f, 6.0f, -30.0f }); // 初期位置（少し高く、手前から見下ろす角度）
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
    for (int x = 0; x <= 28; ++x) { addBlock(static_cast<float>(x), -0.6f,  0.0f); }
    // 上段（天井）: Y=13
    for (int x = 0; x <= 28; ++x) { addBlock(static_cast<float>(x), 13.0f, 0.0f); }
    // 左列: Y=0〜12（Y=0 で床ブロックと重なり、隙間を埋める）
    for (int y = 0; y <= 12; ++y) { addBlock(2.0f,  static_cast<float>(y), 0.0f); }
    // 右列: Y=0〜12
    for (int y = 0; y <= 12; ++y) { addBlock(28.0f, static_cast<float>(y), 0.0f); }

    // ----- プレイヤー -----
    modelPlayer_ = std::make_unique<Model>();
    modelPlayer_->Initialize(modelCommon_.get(),
        "Resources/player/player.obj",
        "Resources/player/player.png");
    player_ = std::make_unique<Object3d>();
    player_->Initialize(modelCommon_.get());
    player_->SetModel(modelPlayer_.get());
    player_->SetEnableLighting(false);
    player_->SetPosition(playerPos_);
    player_->Update();

    // ----- 敵（静止）-----
    modelEnemy_ = std::make_unique<Model>();
    modelEnemy_->Initialize(modelCommon_.get(),
        "Resources/block/block.obj",
        "Resources/monsterBall.png");
    enemy_ = std::make_unique<Object3d>();
    enemy_->Initialize(modelCommon_.get());
    enemy_->SetModel(modelEnemy_.get());
    enemy_->SetEnableLighting(false);
    enemy_->SetPosition(enemyPos_);
    enemy_->Update();

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
        glassShatter_.Update(GameConstants::kFrameDeltaTime);
        if (glassShatter_.IsFinished()) {
            SceneManager::GetInstance()->ChangeScene("CLEAR");
        }
        return;
    }

    // ゲーム内時刻を1フレーム分進める（引数は時刻の進み速度。1.0f = リアルタイムと同じ速度）
    gameTime_.Update(1.0f);

    // ----- プレイヤー操作（A/D: 横移動、W/Space: ジャンプ、重力あり）-----
    {
        // 横移動
        if (input_->PushKey(DIK_A) || input_->PushKey(DIK_LEFT))  { playerPos_.x -= playerSpeed_; }
        if (input_->PushKey(DIK_D) || input_->PushKey(DIK_RIGHT)) { playerPos_.x += playerSpeed_; }

        // ジャンプ（地面にいるときだけ）
        if (playerOnGround_) {
            if (input_->TriggerKey(DIK_W)     ||
                input_->TriggerKey(DIK_UP)    ||
                input_->TriggerKey(DIK_SPACE)) {
                playerVelocityY_ = kJumpPower_;
                playerOnGround_  = false;
            }
        }

        // 重力・落下
        playerVelocityY_ -= kGravity_;
        playerPos_.y     += playerVelocityY_;

        // 着地判定（床クランプ）
        if (playerPos_.y <= kGroundY_) {
            playerPos_.y     = kGroundY_;
            playerVelocityY_ = 0.0f;
            playerOnGround_  = true;
        }

        // 天井クランプ（頭をぶつけたら速度をゼロに）
        if (playerPos_.y > kCeilingY_) {
            playerPos_.y     = kCeilingY_;
            playerVelocityY_ = 0.0f;
        }
        // 左右クランプ
        playerPos_.x = std::clamp(playerPos_.x, kPlayerMinX_, kPlayerMaxX_);

        player_->SetPosition(playerPos_);
        player_->Update();
    }

    // ----- カメラをプレイヤーに追従（境界ブロックが画面外に出ないよう clamp）-----
    // fovY=0.45rad, dist=30 のとき Z=0 面の可視半幅≈12.25、可視半高≈6.89
    // 左壁X=2, 右壁X=28, 床Y=-0.6, 天井Y=13（ブロック中心値）
    {
        constexpr float kHalfW = 12.25f;
        constexpr float kHalfH =  6.89f;
        constexpr float kBlkR  =  0.5f;  // ブロック半径
        cameraTargetPos_ = {
            std::clamp(playerPos_.x,       2.0f - kBlkR + kHalfW,  28.0f + kBlkR - kHalfW),
            std::clamp(playerPos_.y + 6.0f, -0.6f - kBlkR + kHalfH, 13.0f + kBlkR - kHalfH),
            -30.0f
        };
    }

    // カメラのスムージング（ぬるぬる補間）を更新する
    UpdateCameraSmoothing();

    // ----- 影の更新 -----
    // 光源方向が変わったときに影用の視錐台（光が届く範囲）を更新する
    shadowManager_->Update(objectCommon_->GetLightDirection());

    // 3Dオブジェクトに最新の「影行列」を渡す（シャドウマップ参照に使う）
    Object3d::SetLightViewProjection(shadowManager_->GetLightViewProjection());

    // ----- ゲームオブジェクトの更新 -----
    for (auto& obj : gameObjects_) {
        obj->Update();
    }

    // ----- 敵（静止、カメラ変化に合わせて WVP を更新するだけ）-----
    enemy_->Update();

    // ----- 天球の更新（カメラに追従し、ゲーム内時刻に合わせて回転）-----
    float timeRatio = gameTime_.GetElapsedMinutes() / GameTime::kTotalGameMinutes;
    skydome_->Update(camera_.get(), timeRatio);

    // ----- 境界ブロックの更新（カメラが動くたびに WVP 行列を再計算）-----
    for (auto& block : borderBlocks_) {
        block->Update();
    }

    // ----- デバッグ UI 更新 -----
    sceneEditor_.Update(BuildEditContext());

    // ---- クリア条件チェック ----
    // ImGui ボタン or タイマー満了のどちらかでクリア演出を起動する
    if (requestClear_ || gameTime_.IsCleared()) {
        requestClear_   = false;
        clearTriggered_ = true;
        glassShatter_.Start();
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
    // キャプチャが必要なフレーム（Start() 直後）はシーンを描いてからキャプチャする
    if (clearTriggered_ && !glassShatter_.NeedCapture()) {
        // ガラスのシャードが飛び去った後ろに白背景を表示する
        // （青いクリアカラーが透けないようにするだけ。スコアは ClearScene で表示）
        spriteCommon_->CommonDrawSettings();
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

    // ----- プレイヤー・敵を描画 -----
    player_->Draw();
    enemy_->Draw();

    // ----- 2D UI（ImGuiで追加したスプライト要素）-----
    // SceneEditor で追加したスプライト UI をすべて描画する
    spriteCommon_->CommonDrawSettings();
    shadowManager_->SetShadowMap(dxCommon_->GetCommandList(), srvManager_);

    for (auto& e : sceneEditor_.GetUIElements()) {
        e.sprite->Update();
        e.sprite->Draw();
    }

    // ----- フィルター適用 -----
    ApplyActiveFilter();

    // ---- ガラス割れエフェクト（クリア演出時のみ）----
    if (clearTriggered_) {
        if (glassShatter_.NeedCapture()) {
            glassShatter_.CaptureFrame();
        }
        glassShatter_.Apply();
    }
}

// =====================================================
// 終了（シーン切り替えやゲーム終了時に1度だけ呼ばれる）
// =====================================================

void GamePlayScene::Finalize()
{
    glassShatter_.Finalize();

    // 音を全部止める（BGM・SE どちらも）
    if (audio_) {
        audio_->StopBGM();
        audio_->StopAllSE();
    }
}
