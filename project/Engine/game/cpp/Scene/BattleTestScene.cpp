/**
 * @file BattleTestScene.cpp
 * @brief 訓練用バトルシーン（BattleTestScene）の初期化と基本更新フローの実装
 */
#include "BattleTestScene.h"
#include "GameSounds.h"
#include "FinisherEffects.h"
#include "BattleTestTuning.h"
#include "WaterPool.h"
#include "SceneVisualTuning.h"
#include "AudioBridge.h"
#include "BattleTestSceneRenderer.h"
#include "Collision.h"
#include "DiagnosticsDraw.h"
#include "GameConstants.h"
#include "GrayscaleEffect.h"
#include "HsvFilter.h"
#include "ImGuiControlPanel.h"
#include "ModelManager.h"
#include "PipelineStateGuard.h"
#include "PlayerBridge.h"
#include "PostEffectRenderTarget.h"
#include "SceneFlow.h"
#include "SceneManager.h"
#include "ScreenFlash.h"
#include "SlashMark.h"
#include "StageEditor.h"
#include "TimeManager.h"
#include "UILayout.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
using namespace engine::graphics;

namespace engine::game {


// トレーニングへ戻るポータル
static constexpr Vector2 kWarpLabelOffset = { 110.0f, 36.0f };

// ダミー

// ヒット演出

// 水しぶき


// HUD（既定の配置。F2のエディタで動かした値は Resources/Config/UI/battletest.json に保存される）
static constexpr const char* kLayoutName = "battletest";
static constexpr Vector2 kBattleControlsAnchor = { 1020.0f, 12.0f };
static constexpr Vector2 kColliderLabelPosition = { 12.0f, 84.0f };

// ══════════════════════════════════════════════════════
// シーン初期化
// ══════════════════════════════════════════════════════

/** @brief 訓練用のステージ・ダミー敵・プレイヤー・HUD・演出を初期化する */
void BattleTestScene::Initialize(DirectXCommon* dxCommon, Input* input, Audio* audio) {
    spriteCommon_ = InitializeCommonResources(dxCommon, input, audio, dxCommon_, input_, audio_);
    audio_->PlaySceneBGM(bgmKey::kBattleTest);

    InitializeCoreSystems();
    InitializeStageModels();
    InitializeDummyEnemies();
    InitializePlayerAndBullets();
    InitializeHud();
    InitializeEffects();
}

/** @brief Initialize()の下請け マネージャ取得・各種Common初期化・カメラ生成を行う */
void BattleTestScene::InitializeCoreSystems() {
    srvManager_ = SrvManager::GetInstance();
    weaponManager_ = WeaponManager::GetInstance();
    pm_ = ParticleManager::GetInstance();

    // テストシーンでは全武器のコンボを試せるように最初から全解放する
    weaponManager_->UnlockAll();

    grayscaleEffect_ = GrayscaleEffect::GetInstance();
    imageFilter_ = ImageFilter::GetInstance();
    hsvFilter_ = HsvFilter::GetInstance();

    BuildRenderFoundation(dxCommon_, srvManager_, "battletest", { modelCommon_, objectCommon_, shadowManager_, camera_ });
}

/** @brief Initialize()の下請け 境界ブロック・ワープポータル・ダミー用モデル・パーティクル群を初期化する */
void BattleTestScene::InitializeStageModels() {
    modelBlock_ = ModelManager::GetInstance()->GetOrLoad(modelCommon_.get(),
        "Resources/block/block.obj",
        "Resources/block/block.png");

    // 背景の街並みはbattletest.json側のkind="background"配置物として管理する
    // （エディタのHierarchy/Inspectorから通常の配置物と同じく選択・移動・削除・保存ができる）

    // 境界ブロック・トリガーは本番ステージと同じ level01.json から読み込む
    // （GetEditorLevelPath()経由でBaseScene::Init()が自動でOpen()する。F2でその場編集も可能）

    sceneShared::CreateWarpPortalBlocks(modelCommon_.get(), modelBlock_,
        { BattleTestTune().warpReturnX, BattleTestTune().dummyGroundY, 0.0f }, SceneVisualTuning::Battle().warpPortalBlockCount,
        SceneVisualTuning::Battle().warpPortalBlockSpacing, SceneVisualTuning::Battle().warpPortalColor, warpPortalBlocks_);

    modelDummy_ = ModelManager::GetInstance()->GetOrLoad(modelCommon_.get(),
        "Resources/AnimatedMonsterPackby@Quaternius/OBJ/Slime.obj",
        "Resources/AnimatedMonsterPackby@Quaternius/OBJ/SlimePalette.png");

    sceneShared::CreateParticleGroupsFromJson(pm_, "Resources/particles/battletest.json");
}

/** @brief Initialize()の下請け ナイト敵と訓練マネキン1体を生成する */
void BattleTestScene::InitializeDummyEnemies() {
    Dummy d;
    d.pos = { BattleTestTune().dummySpawnX, BattleTestTune().dummyGroundY, 0.0f };
    d.homePos = d.pos;
    d.hp = BattleTestTune().dummyMaxHp;
    d.maxHp = BattleTestTune().dummyMaxHp;
    d.hitFlash = 0.0f;

    d.object = std::make_unique<Object3d>();
    d.object->Initialize(modelCommon_.get());
    d.object->SetModel(modelDummy_);
    d.object->SetEnableLighting(false);
    d.object->SetScale({ kDummyModelScale, kDummyModelScale, kDummyModelScale });
    d.object->SetPosition({ d.pos.x, d.pos.y + kDummyModelFootOffsetY, d.pos.z });
    d.object->Update();

    // 頭上のHPバー（前景は満タンの色から始める）
    sceneShared::CreateHpBarSprites(spriteCommon_.get(), d.hpBarBg, d.hpBarFg, SceneVisualTuning::Battle().hpBarBackgroundColor);
    d.hpBarFg->SetColor(SceneVisualTuning::Battle().hpBarInitialColor);

    dummies_.push_back(std::move(d));
}

/** @brief Initialize()の下請け プレイヤーと弾丸プールを初期化する */
void BattleTestScene::InitializePlayerAndBullets() {
    // 水平方向の移動範囲は固定値で決め打ちしない壁ブロックの当たり判定（ResolveBlockCollision）が
    // StageEditorでの編集をそのまま反映するので、それ自体が境界として機能する
    // "Player"のRegisterExternalEntity()はGetEditorPlayerPositionTarget()経由でBaseScene::Init()が自動で行う
    sceneShared::InitializePlayerAndBullets(player_, bulletPool_, modelCommon_.get(), modelBlock_, audio_);
}

/** @brief Initialize()の下請け HUD（覚醒ゲージ・武器スロット・スタイルメーター・フォント等）を初期化する */
void BattleTestScene::InitializeHud() {

    InitializeWeaponSlotHud();

    styleMeter_.Initialize(spriteCommon_.get());

    fontRenderer_.Initialize(spriteCommon_.get());
    pauseMenu_.Initialize(spriteCommon_.get(), &fontRenderer_, audio_, kLayoutName,
        {
            "BACK TO TRAINING", [] {
                SceneFlow::GetInstance()->Transition("BATTLETEST", "back", "TRAINING");
            }
        });
    comboTrial_.Initialize(spriteCommon_.get());
    pauseMenu_.EnableComboTrial(&comboTrial_);
    SlashMark::GetInstance()->Initialize(spriteCommon_.get());

    finisherOverlay_ = sceneShared::CreateFinisherOverlay(spriteCommon_.get());

    glassShatterBgSprite_ = std::make_unique<Sprite>();
    glassShatterBgSprite_->Initialize(spriteCommon_.get(), "Resources/white.png");
    glassShatterBgSprite_->SetPosition({ 0.0f, 0.0f });
    glassShatterBgSprite_->SetSize({ static_cast<float>(WinApp::kClientWidth),
        static_cast<float>(WinApp::kClientHeight) });
}

/** @brief Initialize()の下請け 画面演出エフェクト（ガラス割れ・刃・空間歪み・切断・世界割れ）を初期化する */
void BattleTestScene::InitializeEffects() {
    glassShatter_.Initialize(dxCommon_, srvManager_);
    bladeFlash_.Initialize(dxCommon_);
    comboFeel_.Initialize(dxCommon_, srvManager_);
    spaceWarp_.Initialize(dxCommon_, srvManager_);
    dummySlice_.Initialize(dxCommon_);
    finisherShatter_.Initialize(dxCommon_, srvManager_);
    finisherShatter_.SetDuration(SceneVisualTuning::Battle().finisherShatterSeconds);
    ImGuiControlPanel::RegisterGlassShatterTrigger([this]() {
        TriggerGlassShatterTest();
    });

    GetStageEditor().SetWaterSplashCallback([this](const Vector3& pos) {
        SpawnWaterSplashEffect(pos);
    });
}

/** @brief 指定座標にヒットエフェクト（パーティクル）を生成する */
void BattleTestScene::SpawnHitEffect(const Vector3& pos) {
    pm_->EmitRing("bt_hit_ring", pos, SceneVisualTuning::Battle().hitRingSpeed, SceneVisualTuning::Battle().hitRingColor, SceneVisualTuning::Battle().hitRingCount, SceneVisualTuning::Battle().hitRingLifetime, SceneVisualTuning::Battle().hitRingSize);
    static std::mt19937 sRng { std::random_device { }() };
    std::uniform_real_distribution<float> vx(-SceneVisualTuning::Battle().hitSparkSpreadX, SceneVisualTuning::Battle().hitSparkSpreadX);
    std::uniform_real_distribution<float> vy(SceneVisualTuning::Battle().hitSparkRiseMin, SceneVisualTuning::Battle().hitSparkRiseMax);
    for (int i = 0; i < SceneVisualTuning::Battle().hitSparkCount; ++i) {
        pm_->EmitGravity("bt_hit_spark", pos,
            { vx(sRng), vy(sRng), 0.0f },
            SceneVisualTuning::Battle().hitSparkColor, SceneVisualTuning::Battle().hitSparkLifetime, SceneVisualTuning::Battle().hitSparkSize);
    }
}

/** @brief 指定座標に水しぶきエフェクト（パーティクル）を生成する StageEditorのトリガー(spawnsWaterSplash)から呼ばれる */
void BattleTestScene::SpawnWaterSplashEffect(const Vector3& pos) {
    static std::mt19937 sRng { std::random_device { }() };
    WaterPool::EmitSplashParticles(pm_, "bt_water_splash", pos, sRng);
}

/** @brief 全マネキンのHPバースプライトを現在HPに合わせて更新する */
void BattleTestScene::UpdateHpBars() {
    UILayout& layout = UILayout::Get(kLayoutName);
    const float barUp = layout.Float("enemy_hp_bar.raise", SceneVisualTuning::Battle().barUp);
    const float barH = layout.Float("enemy_hp_bar.height", SceneVisualTuning::Battle().barH);
    const float barW = layout.Float("enemy_hp_bar.width", SceneVisualTuning::Battle().barW);
    const Vector4 hpBarBackgroundColor = layout.Color("enemy_hp_bar.background_color", SceneVisualTuning::Battle().hpBarBackgroundColor);
    const float hpBarGreenScale = layout.Float("enemy_hp_bar.green_scale", SceneVisualTuning::Battle().hpBarGreenScale);
    const float hpBarGreenBase = layout.Float("enemy_hp_bar.green_base", SceneVisualTuning::Battle().hpBarGreenBase);
    const float hpBarAlpha = layout.Float("enemy_hp_bar.alpha", SceneVisualTuning::Battle().hpBarAlpha);
    const Vector3& cam = camera_->GetTranslate();

    const sceneShared::HpBarStyle style { { barW, barH }, barUp, hpBarGreenScale, hpBarGreenBase,
        hpBarAlpha, &hpBarBackgroundColor };
    for (auto& dummy : dummies_) {
        sceneShared::UpdateWorldHpBar(dummy.hpBarBg.get(), dummy.hpBarFg.get(), dummy.pos, cam, dummy.hpDisplay, style);
    }
}

// ══════════════════════════════════════════════════════
// シーン更新
// ══════════════════════════════════════════════════════

/** @brief 訓練用の操作・戦闘・ダミー敵・演出を更新する（ポーズ中は止める） */
void BattleTestScene::Update() {
    if (glassShatter_.IsActive()) {
        glassShatter_.Update(gameConstants::kFrameDeltaTime);
        return;
    }

    // 一時停止中はゲームを止め、文字コマンドの破棄だけ行う（HUDは描画側で隠す）
    const bool paused = pauseMenu_.Update(input_);
    // 配置物と敵は BaseScene が Update の後に更新するので、ポーズ中はそちらも止める
    GetStageEditor().SetGameplayHeld(paused);
    if (paused) {
        fontRenderer_.Reset();
        return;
    }

    // モーション編集のプレビュー中はゲームを止め、見た目と武器の持ち替えだけ進める
    if (motionEditor_.Update(input_, *player_, weaponManager_->GetCurrent().type)) {
        sceneShared::UpdateWeaponCycle(input_, weaponManager_, weaponCycleTimer_, true);
        RefreshVisualTransformsForEditor();
        return;
    }

    UpdateSceneFlow();
}

/** @brief 訓練シーンの入力、戦闘、演出、HUD更新を順序付けて実行する */
void BattleTestScene::UpdateSceneFlow() {
    fontRenderer_.Reset();

    // StageEditor::Update()（F2トグル・パネル・トリガー判定）と、エディタ表示中の一時停止分岐は
    // BaseScene::Tick()が面倒を見る（表示中はこのUpdate()自体が呼ばれずRefreshVisualTransformsForEditor()が代わりに呼ばれる）

    if (input_->TriggerKey(DIK_F3)) {
        showColliders_ = !showColliders_;
    }
    if (input_->TriggerKey(DIK_F4)) {
        showHud_ = !showHud_;
    }
    if (showColliders_) {
        DrawColliderOverlay();
    }

    sceneShared::UpdateWeaponCycle(input_, weaponManager_, weaponCycleTimer_, true);
    UpdateTargetLock();
    UpdatePlayerAndCamera();
    {
        // 刃の位置はプレイヤー更新後・当たり判定前に積む（斬撃線の向きにこのフレームの振りを使うため）
        const WeaponData& weapon = weaponManager_->GetCurrent();
        const Vector4 weaponColor = ColorFromArray(weapon.effectColor);
        comboFeel_.SetAwakened(player_->IsAwakened());
        comboFeel_.Update(*player_, weaponColor, camera_.get(), true);
        comboFeel_.UpdateDebugPanel(input_);
    }
    UpdateEnvironment();

    UpdateCombat();
    UpdateFinisherSlash();
    styleMeter_.Update(gameConstants::kFrameDeltaTime);
    UpdateDummies();
    UpdatePlacedKnights();
    UpdateWeaponSlotHud();

    dummySlice_.Update(gameConstants::kFrameDeltaTime, camera_.get());
    bladeFlash_.Update(gameConstants::kFrameDeltaTime, camera_.get());

    // 切断演出が飛散に移ったら、隠していたダミーを再表示する
    if (dummySlice_.IsBursting() || !dummySlice_.IsActive()) {
        for (auto& d : dummies_) {
            d.sliced = false;
        }
    }
    // プレイヤー位置を画面UVへ投影して空間歪みの中心に設定する
    if (spaceWarp_.IsActive() || finisherActive_) {
        finisherEffects::UpdateWarpCenter(spaceWarp_, *camera_, player_->GetPosition());
    }
    spaceWarp_.Update(gameConstants::kFrameDeltaTime);
    // 解放時の世界割れ
    finisherShatter_.Update(gameConstants::kFrameDeltaTime);
    if (finisherShatter_.IsFinished()) {
        finisherShatter_.Reset();
    }
    SlashMark::GetInstance()->Update(gameConstants::kFrameDeltaTime);
    bool nearReturn = sceneShared::UpdatePortalTransition(input_, player_->GetPosition(), BattleTestTune().warpReturnX, BattleTestTune().returnProximity, "BATTLETEST", "back", "TRAINING", audio_);
    if (showHud_) {
        DrawHud(nearReturn);
        comboTrial_.QueueText(fontRenderer_, input_);
    }
}
/** @brief エディタ表示中（ゲームプレイ停止中）にプレイヤー/ナイトの見た目だけ追従させる */
void BattleTestScene::RefreshVisualTransformsForEditor() {
    fontRenderer_.Reset();
    // ステージエディタ表示中はゲームプレイ（敵AI・プレイヤー操作・カメラ追従）を丸ごと止める
    // TimeManagerのタイムスケールだけでは、このシーンの各種Updateが固定dt(gameConstants::kFrameDeltaTime)で
    // 動いてしまい止まらないため、BaseScene::Tick()がUpdate()の代わりにこちらを呼ぶ
    // Object3dのUpdate()はカメラのVP行列込みで定数バッファを書くため、
    // WASDでカメラを動かしても正しい位置に描かれるよう、全モデルの行列だけは毎フレーム再計算する
    // （これを怠ると古いカメラ行列のまま描画され、モデルが画面に張り付いてついてくるように見える）
    player_->RefreshVisualTransforms();
    bulletPool_.RefreshVisualTransforms();
    for (auto& d : dummies_) {
        d.object->Update();
    }
    for (auto& p : warpPortalBlocks_) {
        p->Update();
    }
    // 武器スロットの3Dアイコン（カメラ相対配置のHUD）とHPバー（WorldToScreen配置）はカメラ移動に追従させる
    UpdateWeaponSlotHud();
    UpdateHpBars();
    if (showColliders_) {
        DrawColliderOverlay();
    }
    if (showHud_) {
        DrawHud(false);
    }
}
/** @brief プレイヤーを更新し、カメラをプレイヤーに追従させる */
void BattleTestScene::UpdatePlayerAndCamera() {
    const auto stageColliders = GetStageEditor().GetSolidColliders();
    // 乱舞のターゲット ロック中ならその対象、そうでなければ最も近いダミー
    {
        const Vector3& pp = player_->GetPosition();
        Vector3 rampTarget = { pp.x + player_->GetLastDirX() * BattleTestTune().rampageDefaultTargetDistance, pp.y, 0.0f };
        bool targetSolid = false;
        const Dummy* locked = LockedDummy();
        if (locked != nullptr && locked->hp > 0.0f) {
            rampTarget = locked->pos;
            targetSolid = true;
        } else {
            float minDist = FLT_MAX;
            for (const auto& d : dummies_) {
                if (d.hp <= 0.0f) {
                    continue;
                }
                float dist = std::abs(d.pos.x - pp.x);
                if (dist < minDist) {
                    minDist = dist;
                    rampTarget = d.pos;
                    targetSolid = true;
                }
            }
        }
        player_->Update(input_, rampTarget, targetSolid, !stageColliders.empty());
        gameSounds::PlayPlayerActions(*player_);
        comboTrial_.Observe(*player_);
    }
    // ステージエディタでsolidにしたブロックとの当たり判定
    // カメラ追従・見た目反映より前に解決しないと、重力計算のブロック非考慮ぶん（1フレーム分の
    // 突き抜け）がそのままカメラとモデルに映り、次フレームで正しい高さへ戻る様子が
    // 高速で降りて後からカメラが付いてくるように見えてしまう
    player_->ResolveBlockCollision(stageColliders);
    // ロック中は移動入力に関係なく対象の方を向かせ、カメラも少しだけ対象側へ寄せて気付きやすくする
    const Vector3* lockTargetPos = nullptr;
    if (const Dummy* locked = LockedDummy()) {
        lockTargetPos = &locked->pos;
        player_->FaceTarget(*lockTargetPos);
    }
    sceneShared::UpdateCameraFollow(camera_.get(), player_->GetPosition(), GetStageEditor().GetSolidColliders(), lockTargetPos);
    comboFeel_.ApplyCameraZoom(camera_.get());
    player_->RefreshVisualTransforms();
}
/** @brief 影・境界ブロック・ワープポータルの演出を更新する */
void BattleTestScene::UpdateEnvironment() {
    shadowManager_->Update(objectCommon_->GetLightDirection());
    Object3d::SetLightViewProjection(shadowManager_->GetLightViewProjection());
    // GetStageEditor().UpdateObjects()はBaseScene::Tick()がUpdate()の後に一括して呼ぶ
    warpPulseTimer_ += gameConstants::kFrameDeltaTime;
    float pulse = SceneVisualTuning::Battle().warpPulseBase + SceneVisualTuning::Battle().warpPulseAmplitude * std::sin(warpPulseTimer_ * SceneVisualTuning::Battle().warpPulseSpeed);
    for (auto& p : warpPortalBlocks_) {
        p->SetColor({ SceneVisualTuning::Battle().warpPortalColor.x * pulse, SceneVisualTuning::Battle().warpPortalColor.y * pulse, SceneVisualTuning::Battle().warpPortalColor.z * pulse, SceneVisualTuning::Battle().warpPortalColor.w });
        p->Update();
    }
}
/** @brief Shiftキーでのロックオン対象の選択・切り替え・自動解除を処理する */
void BattleTestScene::UpdateTargetLock() {
    // Shiftを押している間だけロックオンし、その間は常に一番近いダミーを対象にし続ける
    // （押した瞬間の対象に固定するのではなく、離れたら別の敵が近くなるような場面でも自然に切り替わる）
    if (!input_->PushAction(Input::Action::kLockOn)) {
        lockedDummyIndex_.reset();
        return;
    }
    // 画面外の敵まで拾うとロック対象の方へカメラが大きく寄ってしまい暴れて見えるため、
    // 画面内に映る範囲（カメラ半幅）より遠い敵はロック対象から除外する
    constexpr float kMaxLockRange = gameConstants::kCameraHalfW;
    const Vector3& pp = player_->GetPosition();
    float minDist = FLT_MAX;
    int nearest = -1;
    for (size_t i = 0; i < dummies_.size(); ++i) {
        if (dummies_[i].hp <= 0.0f) {
            continue;
        }
        const float dist = std::abs(dummies_[i].pos.x - pp.x);
        if (dist > kMaxLockRange) {
            continue;
        }
        if (dist < minDist) {
            minDist = dist;
            nearest = static_cast<int>(i);
        }
    }
    if (nearest < 0) {
        lockedDummyIndex_.reset();
        return;
    }
    lockedDummyIndex_ = static_cast<size_t>(nearest);
}
// ══════════════════════════════════════════════════════
// HUD更新と描画
// ══════════════════════════════════════════════════════
void BattleTestScene::DrawHud(bool nearReturnPortal) {
    UILayout& layout = UILayout::Get(kLayoutName);
    const float lockMarkerHalfWidth = layout.Float("lock_marker.half_width", SceneVisualTuning::Battle().lockMarkerHalfWidth);
    DrawWeaponHud(nearReturnPortal);
    sceneShared::DrawControlsHud(fontRenderer_,
        layout.Pos("controls.pos", kBattleControlsAnchor), L": トレーニングへ戻る");
    styleMeter_.UpdateHud(fontRenderer_); // 右上のスタイリッシュランク
    hud_.QueueText(fontRenderer_);
    // ── ロックオン中の対象にマーカーを出す ────────────────────────
    if (const Dummy* locked = LockedDummy()) {
        const Vector3& tpos = locked->pos;
        const Vector3& cam = camera_->GetTranslate();
        float sx, sy;
        sceneShared::WorldToScreen(tpos.x, tpos.y + SceneVisualTuning::Battle().lockMarkerHeight, cam.x, cam.y, sx, sy);
        fontRenderer_.DrawString("v LOCK v", sx - lockMarkerHalfWidth, sy,
            layout.Float("lock_marker.scale", SceneVisualTuning::Battle().lockMarkerScale), layout.Color("lock_marker.color", SceneVisualTuning::Battle().lockMarkerColor));
    }
}
/** @brief 武器一覧と戻りポータルのラベルを描画する */
void BattleTestScene::DrawWeaponHud(bool nearReturnPortal) {
    UILayout& layout = UILayout::Get(kLayoutName);
    const Vector2 warpLabelOffset = layout.Vec2("portal.label_offset", kWarpLabelOffset);
    // ポータルとコライダー表示の文字サイズを取得する。
    const float hintScale = layout.Float("weapon_list.hint_scale", SceneVisualTuning::Battle().hintScale);
    // 左上には武器一覧と切り替え案内を表示する。
    const Vector2 weaponHudAnchor = layout.Pos("weapon_list.pos", kDefaultHudWeaponAnchor);
    sceneShared::DrawWeaponListHud(fontRenderer_, weaponManager_, L"テストステージ", weaponHudAnchor);
    // 戻りポータルのラベル
    if (nearReturnPortal) {
        const Vector3& cam = camera_->GetTranslate();
        float sx, sy;
        sceneShared::WorldToScreen(BattleTestTune().warpReturnX, SceneVisualTuning::Battle().warpLabelWorldY, cam.x, cam.y, sx, sy);
        fontRenderer_.DrawStringW(input_->ExpandPrompts(L"[ {Interact} ] トレーニングへ"), sx - warpLabelOffset.x, sy - warpLabelOffset.y,
            hintScale, layout.Color("portal.label_color", SceneVisualTuning::Battle().returnLabelColor));
    }
    if (showColliders_) {
        const Vector2 colliderLabelPosition = layout.Pos("collider_label.pos", kColliderLabelPosition);
        fontRenderer_.DrawString("[ F3 ] Colliders: ON", colliderLabelPosition.x, colliderLabelPosition.y,
            hintScale, layout.Color("collider_label.color", SceneVisualTuning::Battle().colliderLabelColor));
    }
}
/** @brief F3で表示切替。プレイヤー/ダミー/ナイト/solidブロックの当たり判定をワイヤーフレームで描く */
void BattleTestScene::DrawColliderOverlay() {
    diagnosticsDraw::SetCamera(camera_->GetViewProjectionMatrix(),
        static_cast<float>(WinApp::kClientWidth), static_cast<float>(WinApp::kClientHeight));
    // プレイヤー（緑）
    diagnosticsDraw::DrawCollider(player_->GetCollider(), diagnosticsDraw::kColorGreen);
    // 訓練マネキン（生存中のみ、シアン）
    for (const auto& d : dummies_) {
        if (d.hp > 0.0f) {
            diagnosticsDraw::DrawAABB(DummyBounds(d), diagnosticsDraw::kColorCyan);
        }
    }
    // StageEditorで配置したナイト（生存中のみ、赤）
    for (KnightEnemy* placedKnight : GetStageEditor().GetKnights()) {
        if (placedKnight->IsAlive()) {
            diagnosticsDraw::DrawAABB(placedKnight->GetAABB(), diagnosticsDraw::kColorRed);
        }
    }
    // ステージエディタでsolidにしたブロック（オレンジ、エディタを開いていなくても見える）
    for (const auto& b : GetStageEditor().GetSolidColliders()) {
        diagnosticsDraw::DrawAABB(b, diagnosticsDraw::kColorOrange);
    }
}
// ══════════════════════════════════════════════════════
// シーン描画と終了処理
// ══════════════════════════════════════════════════════
void BattleTestScene::Draw() {
    BattleTestSceneRenderer::Draw(*this);
}
/** @brief 訓練シーン固有の演出資源を破棄する */
void BattleTestScene::Finalize() {
    ImGuiControlPanel::RegisterGlassShatterTrigger(nullptr);
    pm_->ClearAllGroups();
    finisherEffects::FinalizeAll(glassShatter_, finisherShatter_, spaceWarp_, bladeFlash_);
    comboFeel_.Finalize();
    motionEditor_.Close(*player_);
}
} // namespace engine::game
