#include "GamePlayScene.h"
#include <cmath>
#include "GameConstants.h"
#include "GrayscaleEffect.h"
#include "HsvFilter.h"
#include "ImageFilter.h"
#include "ParticleManager.h"
#include "SceneManager.h"
#include "ScoreManager.h"
#include "TextureManager.h"

// =====================================================
// 初期化
// =====================================================

void GamePlayScene::Initialize(DirectXCommon* dxCommon, Input* input, Audio* audio)
{
    dxCommon_ = dxCommon;
    input_ = input;
    audio_ = audio;

    // ----- 描画共通設定 -----
    spriteCommon_ = std::make_unique<SpriteCommon>();
    spriteCommon_->Initialize(dxCommon_);

    modelCommon_ = std::make_unique<ModelCommon>();
    modelCommon_->Initialize(dxCommon_);

    objectCommon_ = std::make_unique<Object3dCommon>();
    objectCommon_->Initialize(dxCommon_);

    // ----- シングルトンをキャッシュ（以降はポインタ経由でアクセス）-----
    srvManager_      = SrvManager::GetInstance();
    particleManager_ = ParticleManager::GetInstance();
    scoreManager_    = ScoreManager::GetInstance();
    grayscaleEffect_ = GrayscaleEffect::GetInstance();
    imageFilter_     = ImageFilter::GetInstance();
    hsvFilter_       = HsvFilter::GetInstance();

    shadowManager_ = std::make_unique<ShadowManager>();
    shadowManager_->Initialize(dxCommon_, srvManager_);

    // ----- カメラ -----
    camera_ = std::make_unique<Camera>();
    camera_->SetTranslate({ 14.5f, 6.0f, -30.0f });
    Object3d::SetCommonCamera(camera_.get());

    // ----- スキンメッシュ（human）-----
    skinCommon_ = std::make_unique<SkinCommon>();
    skinCommon_->Initialize(dxCommon_);

    modelHuman_ = std::make_unique<SkinnedModel>();
    modelHuman_->Initialize(dxCommon_,
        "Resources/human/sneakWalk.gltf",
        "Resources/human/white.png");

    Node humanRootNode = LoadNodeHierarchyFromFile("Resources/human", "sneakWalk.gltf");
    Skeleton humanSkeleton = Skeleton::Create(humanRootNode);
    Animation humanAnimation = LoadAnimationFile("Resources/human", "sneakWalk.gltf");

    human_ = std::make_unique<SkinnedObject3d>();
    human_->Initialize(skinCommon_.get());
    human_->SetModel(modelHuman_.get());
    human_->SetSkeleton(std::move(humanSkeleton));
    human_->SetAnimation(std::move(humanAnimation));
    human_->SetPosition(humanPosition_);
    SkinnedObject3d::SetCommonCamera(camera_.get());
    SkinnedObject3d::SetCommonObjectCommon(objectCommon_.get());
    SkinnedObject3d::SetCommonShadowManager(shadowManager_.get());
    SkinnedObject3d::SetCommonModelCommon(modelCommon_.get());

    // ----- エフェクト・進行管理 -----
    // 白パーティクル（1024個を個別ライフタイムで散布・自動再配置）
    particleManager_->CreateParticleGroup("white", "Resources/white.png");
    particleManager_->SetAdditiveBlend("white", false);
    particleManager_->EmitScatterLoop(
        "white", whiteParticlePos_, GameConstants::kWhiteParticleScatterRadius,
        static_cast<uint32_t>(whiteParticleCount_),
        whiteParticleColor_,
        GameConstants::kWhiteParticleLifetimeMin, GameConstants::kWhiteParticleLifetimeMax,
        whiteParticleScale_);

    // 楕円パーティクルグループ（circle2.png を使用）
    particleManager_->CreateParticleGroup("ellipse", "Resources/circle2.png");

    // 斬撃パーティクルグループ（gradationLine.png を使用）
    particleManager_->CreateParticleGroup("slash", "Resources/gradationLine.png");

    // Ring（gradationLine.png、AddressV=CLAMP）
    ring_ = std::make_unique<Ring>();
    ring_->Initialize(dxCommon_);
    ring_->SetPosition(ringPosition_);
    ring_->SetInnerRadius(ringInnerRadius_);
    ring_->SetOuterRadius(ringOuterRadius_);

    // Cylinder
    cylinder_ = std::make_unique<Cylinder>();
    cylinder_->Initialize(dxCommon_);
    cylinder_->SetPosition(cylinderPosition_);
    cylinder_->SetTopRadius(cylinderTopRadius_);
    cylinder_->SetBottomRadius(cylinderBottomRadius_);
    cylinder_->SetHeight(cylinderHeight_);

    scoreManager_->LoadScores();
    scoreManager_->ResetCurrentScore();
    gameTime_.Initialize();

    // ----- オフスクリーンレンダリング -----
    renderTexture_ = std::make_unique<RenderTexture>();
    renderTexture_->Initialize(dxCommon_, srvManager_,
        WinApp::kClientWidth, WinApp::kClientHeight);

    renderTextureSprite_ = std::make_unique<Sprite>();
    renderTextureSprite_->Initialize(spriteCommon_.get(), "Resources/white.png");
    renderTextureSprite_->SetExternalTexture(renderTexture_->GetSrvIndex());
    renderTextureSprite_->SetPosition({ 0.0f, 0.0f });
    renderTextureSprite_->SetSize({ static_cast<float>(WinApp::kClientWidth),
                                    static_cast<float>(WinApp::kClientHeight) });

    // ----- デバッグパラメータ読み込み -----
    sceneEditor_.LoadAll(BuildEditContext());

    cameraTargetPos_ = camera_->GetTranslate();
    cameraTargetRot_ = camera_->GetRotate();
}

// =====================================================
// EditContext 構築ヘルパー
// =====================================================

SceneEditor::EditContext GamePlayScene::BuildEditContext()
{
    SceneEditor::EditContext ctx;
    ctx.camera       = camera_.get();
    ctx.ring         = ring_.get();
    ctx.cylinder     = cylinder_.get();
    ctx.skydome      = skydome_.get();
    ctx.human        = human_.get();
    ctx.spriteCommon = spriteCommon_.get();

    ctx.cameraTargetPos    = &cameraTargetPos_;
    ctx.cameraTargetRot    = &cameraTargetRot_;
    ctx.cameraSmoothFrames = &cameraSmoothFrames_;
    ctx.cameraPosHistory   = &cameraPosHistory_;
    ctx.cameraRotHistory   = &cameraRotHistory_;

    ctx.ringPosition    = &ringPosition_;
    ctx.ringRotation    = &ringRotation_;
    ctx.ringColor       = &ringColor_;
    ctx.ringScale       = &ringScale_;
    ctx.ringInnerRadius = &ringInnerRadius_;
    ctx.ringOuterRadius = &ringOuterRadius_;

    ctx.cylinderPosition     = &cylinderPosition_;
    ctx.cylinderRotation     = &cylinderRotation_;
    ctx.cylinderColor        = &cylinderColor_;
    ctx.cylinderScale        = &cylinderScale_;
    ctx.cylinderTopRadius    = &cylinderTopRadius_;
    ctx.cylinderBottomRadius = &cylinderBottomRadius_;
    ctx.cylinderHeight       = &cylinderHeight_;
    ctx.cylinderAlphaRef     = &cylinderAlphaRef_;

    ctx.skyColor      = &skyColor_;
    ctx.skyRotOffsetY = &skyRotOffsetY_;

    ctx.humanPosition  = &humanPosition_;
    ctx.humanRotation  = &humanRotation_;
    ctx.humanScale     = &humanScale_;
    ctx.humanAnimSpeed = &humanAnimSpeed_;
    ctx.showSkeleton   = &showSkeletonDebug_;

    ctx.whiteParticlePos   = &whiteParticlePos_;
    ctx.whiteParticleColor = &whiteParticleColor_;
    ctx.whiteParticleScale = &whiteParticleScale_;
    ctx.whiteParticleCount = &whiteParticleCount_;

    ctx.gameHour   = gameTime_.GetHour();
    ctx.gameMinute = gameTime_.GetMinute();
    return ctx;
}

// =====================================================
// 更新処理
// =====================================================

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

// =====================================================
// 更新処理
// =====================================================

void GamePlayScene::Update()
{
    gameTime_.Update(1.0f);
    UpdateCameraSmoothing();

    // シャドウの更新
    shadowManager_->Update(objectCommon_->GetLightDirection());
    Object3d::SetLightViewProjection(shadowManager_->GetLightViewProjection());
    SkinnedObject3d::SetLightViewProjection(shadowManager_->GetLightViewProjection());

    // ゲームオブジェクトの更新
    for (auto& obj : gameObjects_) {
        obj->Update();
    }

    human_->Update();

    // デバッグ UI 更新
    sceneEditor_.Update(BuildEditContext());

    // 楕円パーティクルをリング周回軌道で放出
    static constexpr float kOrbitSpeed = GameConstants::kTwoPi / GameConstants::kOrbitPeriodSeconds;
    ringOrbitAngle_ += kOrbitSpeed * GameConstants::kFrameDeltaTime;
    if (ringOrbitAngle_ > GameConstants::kTwoPi) {
        ringOrbitAngle_ -= GameConstants::kTwoPi;
    }

    ellipseParticleTimer_ += GameConstants::kFrameDeltaTime;
    while (ellipseParticleTimer_ >= kEllipseEmitInterval) {
        ellipseParticleTimer_ -= kEllipseEmitInterval;

        float r = (ringInnerRadius_ + ringOuterRadius_) * 0.5f;
        Vector3 emitPos = {
            ringPosition_.x + std::cos(ringOrbitAngle_) * r,
            ringPosition_.y,
            ringPosition_.z + std::sin(ringOrbitAngle_) * r
        };
        Vector3 vel = {
            -std::sin(ringOrbitAngle_) * GameConstants::kEllipseTangentSpeed,
             GameConstants::kEllipseYVelocity,
             std::cos(ringOrbitAngle_) * GameConstants::kEllipseTangentSpeed
        };

        // 水色（R=0.6, G=0.85, B=1.0, A=1.0）
        particleManager_->EmitEllipse(
            "ellipse",
            emitPos,
            vel,
            { 0.6f, 0.85f, 1.0f, 1.0f },
            GameConstants::kEllipseLifetime,
            GameConstants::kEllipseBaseScale,
            GameConstants::kEllipseScaleRandom
        );
    }

    ring_->Update(camera_.get());
    cylinder_->Update(camera_.get());
}

// =====================================================
// 描画
// =====================================================

void GamePlayScene::DrawShadowPass()
{
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    shadowManager_->BeginShadowPass(commandList);
    modelCommon_->BeginShadowPass();

    shadowManager_->EndShadowPass(commandList);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv;
    if (imageFilter_->IsEnabled()) {
        rtv = imageFilter_->GetSceneRTVHandle();
    } else if (grayscaleEffect_->IsEnabled()) {
        rtv = grayscaleEffect_->GetSceneRTVHandle();
    } else if (hsvFilter_->IsEnabled()) {
        rtv = hsvFilter_->GetSceneRTVHandle();
    } else {
        rtv = dxCommon_->GetCurrentBackBufferHandle();
    }
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = dxCommon_->GetDsvHandle();
    commandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    D3D12_VIEWPORT vp = { 0, 0,
        static_cast<float>(WinApp::kClientWidth), static_cast<float>(WinApp::kClientHeight),
        0.0f, 1.0f };
    D3D12_RECT scissor = { 0, 0, WinApp::kClientWidth, WinApp::kClientHeight };
    commandList->RSSetViewports(1, &vp);
    commandList->RSSetScissorRects(1, &scissor);
}

void GamePlayScene::Draw()
{
    // ----- オフスクリーンパス（赤でクリアしてSRVに遷移）-----
    renderTexture_->BeginRendering();
    renderTexture_->EndRendering();

    DrawShadowPass();

    // ----- 背景としてレンダーテクスチャを描画 -----
    spriteCommon_->CommonDrawSettings();
    shadowManager_->SetShadowMap(dxCommon_->GetCommandList(), srvManager_);
    renderTextureSprite_->Update();
    renderTextureSprite_->Draw();

    // 3Dオブジェクト
    modelCommon_->CommonDrawSettings();
    objectCommon_->SetDefaultLight(dxCommon_->GetCommandList());
    shadowManager_->SetShadowMap(dxCommon_->GetCommandList(), srvManager_);

    for (auto& obj : gameObjects_) {
        obj->Draw();
    }

    // スキンメッシュ描画（PSO 切り替え後にライト・シャドウを再バインド）
    skinCommon_->CommonDrawSettings();
    objectCommon_->SetDefaultLight(dxCommon_->GetCommandList());
    shadowManager_->SetShadowMap(dxCommon_->GetCommandList(), srvManager_);
    human_->Draw();

    // スケルトンデバッグ描画（3Dワールド空間）
#ifdef USE_IMGUI
    if (showSkeletonDebug_ && human_) {
        human_->DebugDraw();
    }
#endif

    // PSO を通常に戻す
    modelCommon_->CommonDrawSettings();
    objectCommon_->SetDefaultLight(dxCommon_->GetCommandList());
    shadowManager_->SetShadowMap(dxCommon_->GetCommandList(), srvManager_);

    particleManager_->Update(camera_.get());
    particleManager_->Draw(camera_.get());
    ring_->Draw();
    cylinder_->Draw();

    // 2D UI（ImGuiで追加したスプライト要素）
    spriteCommon_->CommonDrawSettings();
    shadowManager_->SetShadowMap(dxCommon_->GetCommandList(), srvManager_);

    for (auto& e : sceneEditor_.GetUIElements()) {
        e.sprite->Update();
        e.sprite->Draw();
    }
}

// =====================================================
// 終了
// =====================================================

void GamePlayScene::Finalize()
{
    if (audio_) {
        audio_->StopBGM();
        audio_->StopAllSE();
    }
    if (particleManager_) { particleManager_->ClearAllGroups(); }
}
