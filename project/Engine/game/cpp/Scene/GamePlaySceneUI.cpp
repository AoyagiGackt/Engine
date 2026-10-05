/**
 * @file GamePlaySceneUI.cpp
 * @brief GamePlaySceneのHUD・オーバーレイ・武器スロット表示・スタイルコマンド表示を実装するファイル
 * @note GamePlayScene.cppからの分割ファイルクラス自体はGamePlaySceneのまま、定義の置き場所だけを分けている
 */
#include "GamePlayScene.h"
#include "WeaponSlotHud.h"
#include "AwakenGaugeHud.h"
#include "AudioBridge.h"
#include "GameConstants.h"
#include "GamePlaySceneInitializer.h"
#include "GameSettings.h"
#include "GrayscaleEffect.h"
#include "HsvFilter.h"
#include "ImGuiControl.h"
#include "ImageFilter.h"
#include "ModelManager.h"
#include "ParticleManager.h"
#include "PipelineStateGuard.h"
#include "PlayerBridge.h"
#include "PostEffectRenderTarget.h"
#include "RunData.h"
#include "SaveData.h"
#include "SceneEffectBridge.h"
#include "SceneFlow.h"
#include "SceneManager.h"
#include "ScoreManager.h"
#include "ScreenFlash.h"
#include "SlashMark.h"
#include "StageEditor.h"
#include "StringUtility.h"
#include "TextureManager.h"
#include "UILayout.h"
#include "WeaponManager.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
using namespace engine;
using namespace engine::graphics;
using namespace engine::game;

namespace {
constexpr Vector4 kFinisherOverlayTint = { 0.0f, 0.0f, 0.05f, 0.0f }; // アルファはGameConstants::kFinisherOverlayAlpha

// 以下は既定の配置（F2のエディタで動かした値は Resources/Config/UI/gameplay.json に保存される）
constexpr const char* kLayoutName = "gameplay";

// 敵体力の数値表示
constexpr Vector2 kEnemyHpTextPosition = { 460.0f, 10.0f };
constexpr float kEnemyHpTextScale = 1.5f;
constexpr Vector4 kEnemyHpTextColor = { 1.0f, 0.35f, 0.35f, 1.0f };

// プレイヤーHP・ゴールドの数値表示（左下の武器スロットの真上）
constexpr Vector2 kInfoPosition = { 24.0f, 600.0f };
constexpr float kInfoScale = 1.5f;
constexpr Vector4 kInfoColor = { 0.3f, 1.0f, 0.4f, 1.0f };

// HPバーの色（残量が減るほど緑から赤へ）
constexpr float kHpBarGreenScale = 0.85f;
constexpr float kHpBarGreenBase = 0.15f;
constexpr float kEnemyHpBarAlpha = 0.9f;
constexpr float kPlayerHpBarAlpha = 0.95f;
constexpr Vector2 kPlayerHpBarPosition = { 24.0f, 570.0f };
constexpr Vector2 kPlayerHpBarSize = { 200.0f, 16.0f };
constexpr Vector2 kEnemyHpBarSize = { 60.0f, 8.0f };
constexpr float kEnemyHpBarRaise = 70.0f; // 敵の足元からバーまでの高さ（画面px）

// コンボルート案内
constexpr Vector2 kGuidePosition = { 355.0f, 574.0f };
constexpr float kGuideTitleScale = 1.15f;
constexpr Vector4 kGuideTitleColor = { 1.0f, 0.82f, 0.2f, 0.82f };
constexpr float kGuideRouteOffsetY = 25.0f;
constexpr float kGuideRouteScale = 1.0f;
constexpr Vector4 kGuideRouteColor = { 0.78f, 0.84f, 0.92f, 0.8f };
constexpr float kGuideProgressScale = 1.35f;
constexpr Vector4 kGuideProgressColor = { 1.0f, 0.82f, 0.2f, 0.95f };
constexpr float kGuideNextOffsetY = 28.0f;
constexpr float kGuideNextScale = 1.15f;
constexpr Vector4 kGuideNextAirColor = { 0.45f, 0.95f, 1.0f, 1.0f };
constexpr Vector4 kGuideNextOpenerColor = { 1.0f, 1.0f, 1.0f, 0.98f };
constexpr Vector4 kGuideNextLauncherColor = { 1.0f, 0.85f, 0.25f, 1.0f };
constexpr Vector4 kGuideFinishColor = { 1.0f, 0.42f, 0.16f, 1.0f };
constexpr int kGuideOpenerSteps = 2; // この段数に達するまでは追撃を案内する

// 本編の操作説明パネルの既定位置
constexpr Vector2 kGameplayControlsAnchor = { 1020.0f, 12.0f };

// 武器回収の操作表示
constexpr float kWeaponStealPromptScale = 1.3f;
constexpr Vector4 kWeaponStealPromptColor = { 0.55f, 0.9f, 1.0f, 1.0f };

// ロックオンマーカー
constexpr float kLockMarkerHeight = 1.6f;
constexpr float kLockMarkerHalfWidth = 46.0f;
constexpr float kLockMarkerScale = 1.3f;
constexpr Vector4 kLockMarkerColor = { 1.0f, 0.35f, 0.2f, 1.0f };

// 武器スロット満杯時の交換案内
constexpr int kWeaponSlotCount = 4;
constexpr Vector2 kExchangeTitlePosition = { 360.0f, 220.0f };
constexpr float kExchangeTitleScale = 2.0f;
constexpr Vector4 kExchangeTitleColor = { 1.0f, 0.85f, 0.2f, 1.0f };
constexpr Vector2 kExchangeWeaponPosition = { 410.0f, 270.0f };
constexpr float kExchangeWeaponScale = 1.6f;
constexpr Vector4 kExchangeWeaponColor = { 0.8f, 0.95f, 1.0f, 1.0f };
constexpr Vector2 kExchangeHelpPosition = { 245.0f, 330.0f };
constexpr float kExchangeHelpScale = 1.35f;
constexpr Vector4 kExchangeHelpColor = { 1.0f, 1.0f, 1.0f, 1.0f };

// 一時停止メニュー
constexpr Vector4 kPauseOverlayColor = { 0.0f, 0.0f, 0.0f, 0.75f };
constexpr Vector2 kPauseMenuPosition = { 440.0f, 220.0f };
constexpr Vector2 kPauseMenuItemSize = { 400.0f, 60.0f };
constexpr float kVolumeStep = 0.1f;
constexpr float kPercentScale = 100.0f;
constexpr float kRoundingOffset = 0.5f;
constexpr Vector2 kBgmVolumeTextPosition = { 900.0f, 291.0f };
constexpr Vector2 kSeVolumeTextPosition = { 900.0f, 351.0f };
constexpr float kVolumeTextScale = 1.5f;
constexpr Vector4 kVolumeTextColor = { 1.0f, 1.0f, 1.0f, 1.0f };
constexpr Vector2 kPausedTitlePosition = { 560.0f, 130.0f };
constexpr float kPausedTitleScale = 2.0f;
constexpr Vector4 kPausedTitleColor = { 1.0f, 1.0f, 0.6f, 1.0f };

// 武器一覧パネル下の操作ヒント
constexpr float kHintLineHeight = 24.0f;
constexpr float kHintScale = 1.5f;
constexpr Vector4 kHintColor = { 0.80f, 0.76f, 0.65f, 1.0f };
constexpr Vector4 kHintShadowColor = { 0.05f, 0.04f, 0.02f, 0.9f };
constexpr float kHintShadowOffset = 1.6f;

// 収集物の個数表示
constexpr Vector2 kPickupCounterPosition = { 24.0f, 540.0f };
constexpr float kPickupCounterScale = 1.35f;
constexpr Vector4 kPickupCounterColor = { 0.3f, 0.9f, 1.0f, 1.0f };
}

void GamePlayScene::DrawOverlaysAndUI()
{
    spriteCommon_->CommonDrawSettings();

    // 敵頭上のHPバー（BattleTestSceneのダミーHPバーと同じ描画順序位置はUpdateEnemyHpBars()が
    // DrawStyleUI()内で毎フレーム更新している）
    if (enemy_ && !enemy_->IsDefeated()) {
        bossHpBarBg_->Draw();
        bossHpBarFg_->Draw();
    }
    for (auto& entry : weaponEnemies_) {
        if (entry.enemy && !entry.enemy->IsDefeated()) {
            entry.hpBarBg->Draw();
            entry.hpBarFg->Draw();
        }
    }
    if (RunData::GetInstance()->IsRunActive()) {
        playerHpBarBg_->Draw();
        playerHpBarFg_->Draw();
    }
    if (bossSlamWarningActive_) {
        bossSlamWarningSprite_->Draw();
    }

    styleRankHud_.DrawHud(); // 右上ランクの進捗バー（BattleTestSceneと同じ見た目）
    DrawWeaponSlotHud();

    for (auto& e : sceneEditor_.GetUIElements()) {
        e.sprite->Update();
        e.sprite->Draw();
    }

    // 大技中と解放フレーム（凍結画面のキャプチャ前）だけ暗転を重ねる
    // 解放後の暗さは砕け散る凍結画面が持ち去るので、素の世界には重ねない
    const bool captureFrame = finisherShatter_.IsActive() && finisherShatter_.NeedCapture();
    if (finisherActive_ || captureFrame) {
        finisherOverlay_->SetColor({ kFinisherOverlayTint.x, kFinisherOverlayTint.y, kFinisherOverlayTint.z,
            GameConstants::kFinisherOverlayAlpha });
        finisherOverlay_->Update();
        finisherOverlay_->Draw();
    }
    SlashMark::GetInstance()->Draw();

    // 解放時の世界割れ（暗転+斬撃線ごと凍った画面を砕き、下から素の世界が現れる）
    if (finisherShatter_.IsActive()
        && GetActiveRTVHandle().ptr == dxCommon_->GetCurrentBackBufferHandle().ptr) {
        if (finisherShatter_.NeedCapture()) {
            finisherShatter_.CaptureFrame();
        }
        // スコープを抜けた瞬間に、Apply が変えたレンダーターゲットとルートシグネチャを後続のスプライト描画用に戻す
        PipelineStateGuard restoreGuard([this] {
            SetupMainRenderTarget();
            spriteCommon_->CommonDrawSettings();
        });
        finisherShatter_.Apply();
    }

    if (pauseController_.IsPaused()) {
        DrawPauseOverlay();
    }

    // ゲームプレイ UI テキスト
    GetStageEditor().DrawUIText(fontRenderer_);
    fontRenderer_.Draw();

    // ガラス割れエフェクト（サンドボックスのクリア演出 / デバッグテスト再生時のみ）
    if (clearTriggered_ && IsGlassShatterFlow()) {
        if (glassShatter_.NeedCapture()) {
            glassShatter_.CaptureFrame();
        }
        glassShatter_.Apply();
    }
}

// ══════════════════════════════════════════════════════
// HUD描画
// ══════════════════════════════════════════════════════

void GamePlayScene::DrawRogueliteHUD()
{
    auto* rd = RunData::GetInstance();
    if (!rd->IsRunActive()) {
        return;
    }

    // 敵の残り体力を上部中央に数値で表示する
    const int enemyHp = enemy_->GetHp();
    const int enemyMaxHp = enemy_->GetMaxHp();
    const std::wstring enemyHpText = L"敵体力  "
        + std::to_wstring(enemyHp) + L" / " + std::to_wstring(enemyMaxHp);
    UILayout& layout = UILayout::Get(kLayoutName);
    const Vector2 enemyHpPosition = layout.Pos("enemy_hp.pos", kEnemyHpTextPosition);
    fontRenderer_.DrawStringW(enemyHpText, enemyHpPosition.x, enemyHpPosition.y,
        layout.Float("enemy_hp.scale", kEnemyHpTextScale), layout.Color("enemy_hp.color", kEnemyHpTextColor));

    // プレイヤーHP + ゴールド左上は武器一覧パネルと被るため、左下の武器スロットHUDの真上に置く
    std::string info = "HP:" + std::to_string(rd->GetHp()) + "/" + std::to_string(rd->GetMaxHp())
        + "  G:" + std::to_string(rd->GetGold());
    const Vector2 infoPosition = layout.Pos("info.pos", kInfoPosition);
    fontRenderer_.DrawString(info.c_str(), infoPosition.x, infoPosition.y,
        layout.Float("info.scale", kInfoScale), layout.Color("info.color", kInfoColor));
}

void GamePlayScene::UpdateEnemyHpBars()
{
    // BattleTestScene::UpdateHpBars()と同じ体裁（頭上に固定サイズのバー、WorldToScreenで追従）
    const Vector3& cam = camera_->GetTranslate();
    UILayout& layout = UILayout::Get(kLayoutName);
    const Vector2 barSize = layout.Vec2("enemy_hp_bar.size", kEnemyHpBarSize);
    const float barRaise = layout.Float("enemy_hp_bar.raise", kEnemyHpBarRaise);

    auto layoutBar = [&](Sprite* bg, Sprite* fg, const Vector3& pos, float ratio) {
        float sx, sy;
        SceneShared::WorldToScreen(pos.x, pos.y, cam.x, cam.y, sx, sy);
        fg->SetColor({ 1.0f - ratio, ratio * kHpBarGreenScale + kHpBarGreenBase, 0.0f, kEnemyHpBarAlpha });
        bg->SetPosition({ sx - barSize.x * 0.5f, sy - barRaise });
        bg->SetSize(barSize);
        bg->Update();
        fg->SetPosition({ sx - barSize.x * 0.5f, sy - barRaise });
        fg->SetSize({ barSize.x * ratio, barSize.y });
        fg->Update();
    };

    if (enemy_ && !enemy_->IsDefeated() && enemy_->GetMaxHp() > 0) {
        const float ratio = static_cast<float>(enemy_->GetHp()) / static_cast<float>(enemy_->GetMaxHp());
        layoutBar(bossHpBarBg_.get(), bossHpBarFg_.get(), enemy_->GetPosition(), ratio);
    }
    for (auto& entry : weaponEnemies_) {
        if (!entry.enemy || entry.enemy->IsDefeated() || entry.enemy->GetMaxHp() <= 0) {
            continue;
        }
        const float ratio = static_cast<float>(entry.enemy->GetHp()) / static_cast<float>(entry.enemy->GetMaxHp());
        layoutBar(entry.hpBarBg.get(), entry.hpBarFg.get(), entry.enemy->GetPosition(), ratio);
    }
}

void GamePlayScene::UpdatePlayerHpBar()
{
    auto* rd = RunData::GetInstance();
    if (!rd->IsRunActive() || rd->GetMaxHp() <= 0) {
        return;
    }

    UILayout& layout = UILayout::Get(kLayoutName);
    const Vector2 barPosition = layout.Pos("player_hp_bar.pos", kPlayerHpBarPosition);
    const Vector2 barSize = layout.Vec2("player_hp_bar.size", kPlayerHpBarSize);

    const float ratio = std::clamp(static_cast<float>(rd->GetHp()) / static_cast<float>(rd->GetMaxHp()), 0.0f, 1.0f);
    playerHpBarBg_->SetPosition(barPosition);
    playerHpBarBg_->SetSize(barSize);
    playerHpBarBg_->Update();

    playerHpBarFg_->SetColor({ 1.0f - ratio, ratio * kHpBarGreenScale + kHpBarGreenBase, 0.0f, kPlayerHpBarAlpha });
    playerHpBarFg_->SetPosition(barPosition);
    playerHpBarFg_->SetSize({ barSize.x * ratio, barSize.y });
    playerHpBarFg_->Update();
}

void GamePlayScene::DrawStyleUI()
{
    fontRenderer_.Reset();

    UpdateEnemyHpBars();
    UpdatePlayerHpBar();
    DrawStageGuide();
    DrawRogueliteHUD();
    styleRankHud_.UpdateHud(fontRenderer_); // 右上のスタイリッシュランク（BattleTestSceneと同じ体裁）

    // 基本ルートは常時見せ、コンボ開始後は現在地点と次の入力へ表示を切り替える。
    // トレーニングで覚えた操作を本編でも同じ順番で再確認できる導線にする。
    if (WeaponManager::GetInstance()->HasEquippedWeapon()) {
        const Vector2 guidePosition = UILayout::Get(kLayoutName).Pos("combo_guide.pos", kGuidePosition);
        const float guideX = guidePosition.x;
        const float guideY = guidePosition.y;
        const bool chainActive = styleRankHud_.GetHitCount() > 0 || player_->IsMeleeAttacking();
        const int step = (std::max)(player_->GetComboStep(), 1);
        const int maxStep = (std::max)(player_->GetComboMax(), 1);
        if (!chainActive) {
            fontRenderer_.DrawStringW(L"推奨コンボルート",
                guideX, guideY, kGuideTitleScale, kGuideTitleColor);
            fontRenderer_.DrawStringW(L"[L] x2  >  [S+L] 打ち上げ  >  [W] ジャンプ  >  [L] 空中追撃  >  [K] 射撃",
                guideX, guideY + kGuideRouteOffsetY, kGuideRouteScale, kGuideRouteColor);
        } else {
            std::wstring progress = L"COMBO  ";
            for (int i = 1; i <= maxStep; ++i) {
                progress += i <= step ? L"● " : L"○ ";
            }
            fontRenderer_.DrawStringW(progress, guideX, guideY, kGuideProgressScale, kGuideProgressColor);

            if (!player_->IsOnGround()) {
                fontRenderer_.DrawStringW(L"NEXT  [L] 空中追撃   [K] 射撃   [SPACE] 固有技",
                    guideX, guideY + kGuideNextOffsetY, kGuideNextScale, kGuideNextAirColor);
            } else if (step < kGuideOpenerSteps) {
                fontRenderer_.DrawStringW(L"NEXT  [L] もう一撃   または  [S+L] 打ち上げ",
                    guideX, guideY + kGuideNextOffsetY, kGuideNextScale, kGuideNextOpenerColor);
            } else if (step < maxStep) {
                fontRenderer_.DrawStringW(L"NEXT  [S+L] 打ち上げ   [1-4] 武器切替",
                    guideX, guideY + kGuideNextOffsetY, kGuideNextScale, kGuideNextLauncherColor);
            } else {
                fontRenderer_.DrawStringW(L"FINISH!   [S+L] 打ち上げ   [1-4] 別武器へ",
                    guideX, guideY + kGuideNextOffsetY, kGuideNextScale, kGuideFinishColor);
            }
        }
    }
    DrawWeaponListPanel();
    SceneShared::DrawControlsHud(fontRenderer_,
        UILayout::Get(kLayoutName).Pos("controls.pos", kGameplayControlsAnchor), L": ステージを進む");
    hud_.QueueText(fontRenderer_);
    // 倒した敵の頭上に武器回収の操作を出す（死んだかどうか・奪えるかが遠目にも分かるように）
    constexpr float kWeaponStealPromptHeight = 2.0f; // ロックオンマーカー（y+1.6）と重ならない高さ
    constexpr float kWeaponStealPromptHalfWidth = 60.0f; // 文字列の見た目の半幅ぶんだけ左へずらして中央寄せする
    auto drawWeaponStealPrompt = [&](const Vector3& enemyPos) {
        const Vector3& cam = camera_->GetTranslate();
        float sx, sy;
        SceneShared::WorldToScreen(enemyPos.x, enemyPos.y + kWeaponStealPromptHeight, cam.x, cam.y, sx, sy);
        fontRenderer_.DrawStringW(L"[J] 武器を回収", sx - kWeaponStealPromptHalfWidth, sy, kWeaponStealPromptScale,
            kWeaponStealPromptColor);
    };
    if (enemy_->IsDefeated() && !weaponStealTriggered_ && !mainWeaponAbsorbing_) {
        drawWeaponStealPrompt(enemy_->GetPosition());
    }
    for (const auto& weaponEnemy : weaponEnemies_) {
        if (weaponEnemy.enemy->IsDefeated() && !weaponEnemy.weaponAcquired && !weaponEnemy.absorbing) {
            drawWeaponStealPrompt(weaponEnemy.enemy->GetPosition());
        }
    }

    // ── ロックオン中の対象にマーカーを出す ────────────────────────
    if (lockedKind_ != LockTargetKind::None) {
        Vector3 tpos { };
        bool valid = true;
        if (lockedKind_ == LockTargetKind::MainEnemy) {
            tpos = enemy_->GetPosition();
        } else if (lockedKind_ == LockTargetKind::WeaponEnemy && lockedWeaponEnemyIndex_ < weaponEnemies_.size()) {
            tpos = weaponEnemies_[lockedWeaponEnemyIndex_].enemy->GetPosition();
        } else {
            valid = false;
        }
        if (valid) {
            const Vector3& cam = camera_->GetTranslate();
            float sx, sy;
            SceneShared::WorldToScreen(tpos.x, tpos.y + kLockMarkerHeight, cam.x, cam.y, sx, sy);
            fontRenderer_.DrawString("v LOCK v", sx - kLockMarkerHalfWidth, sy, kLockMarkerScale, kLockMarkerColor);
        }
    }

    DrawWeaponExchange();
}

void GamePlayScene::InitializeWeaponSlotHud()
{
    hud_.Add(std::make_unique<AwakenGaugeHud>());
    hud_.Add(std::make_unique<WeaponSlotHud>());
    hud_.Initialize({ *spriteCommon_, *modelCommon_, *dxCommon_, *WeaponManager::GetInstance() });
    UpdateWeaponSlotHud();
}

void GamePlayScene::UpdateWeaponSlotHud()
{
    HudFrame frame { *camera_, GameConstants::kFrameDeltaTime, player_->GetAwakenGauge(), player_->IsAwakened(), auraTimer_ };
    hud_.Update(frame);
}

void GamePlayScene::DrawWeaponSlotHud()
{
    hud_.Draw();
}

void GamePlayScene::UpdateWeaponExchange()
{
    auto* wm = WeaponManager::GetInstance();
    if (!wm->HasPendingWeapon()) {
        return;
    }

    for (int slot = 0; slot < kWeaponSlotCount; ++slot) {
        if (input_->TriggerKey(static_cast<uint8_t>(DIK_1 + slot))) {
            wm->ReplacePendingWeapon(slot);
            return;
        }
    }
    if (input_->TriggerKey(DIK_BACK) || input_->TriggerButton(XINPUT_GAMEPAD_B)) {
        wm->DiscardPendingWeapon();
    }
}

void GamePlayScene::DrawWeaponExchange()
{
    auto* wm = WeaponManager::GetInstance();
    if (!wm->HasPendingWeapon()) {
        return;
    }

    UILayout& layout = UILayout::Get(kLayoutName);
    const Vector2 titlePosition = layout.Pos("exchange.title_pos", kExchangeTitlePosition);
    fontRenderer_.DrawStringW(L"武器スロットが満杯です", titlePosition.x, titlePosition.y,
        layout.Float("exchange.title_scale", kExchangeTitleScale), layout.Color("exchange.title_color", kExchangeTitleColor));
    const Vector2 weaponPosition = layout.Pos("exchange.weapon_pos", kExchangeWeaponPosition);
    fontRenderer_.DrawStringW(L"入手武器  " + StringUtility::ConvertString(wm->GetPendingWeapon().name),
        weaponPosition.x, weaponPosition.y,
        layout.Float("exchange.weapon_scale", kExchangeWeaponScale), layout.Color("exchange.weapon_color", kExchangeWeaponColor));
    const Vector2 helpPosition = layout.Pos("exchange.help_pos", kExchangeHelpPosition);
    fontRenderer_.DrawStringW(L"1から4で交換するスロットを選択  Backspaceで破棄", helpPosition.x, helpPosition.y,
        layout.Float("exchange.help_scale", kExchangeHelpScale), layout.Color("exchange.help_color", kExchangeHelpColor));
}

// ══════════════════════════════════════════════════════
// 一時停止メニュー
// ══════════════════════════════════════════════════════

void GamePlayScene::SetupPauseMenu()
{
    pauseOverlay_ = std::make_unique<Sprite>();
    pauseOverlay_->Initialize(spriteCommon_.get(), "Resources/white.png");
    pauseOverlay_->SetPosition({ 0.0f, 0.0f });
    pauseOverlay_->SetSize({ GameConstants::kScreenWidth, GameConstants::kScreenHeight });

    pauseMenu_.Initialize(spriteCommon_.get(), &fontRenderer_, audio_);
    pauseMenu_.BindLayout(kLayoutName, "pause_menu", kPauseMenuPosition, kPauseMenuItemSize);
    pauseMenu_.SetItems({
        { "RESUME" },
        { "BGM VOLUME" },
        { "SE VOLUME" },
        { "QUIT TO TITLE" },
    });
}

void GamePlayScene::UpdatePauseMenu()
{
    // ESCでの開閉自体はGamePlayScene::Update()側のトグルで処理済み（このフレームには来ない）
    pauseMenu_.Update(input_);

    const int row = pauseMenu_.GetSelectedIndex();
    if (row == kPauseRowBgmVolume || row == kPauseRowSeVolume) {
        float delta = 0.0f;
        if (input_->TriggerKey(DIK_A) || input_->TriggerKey(DIK_LEFT)) {
            delta = -kVolumeStep;
        } else if (input_->TriggerKey(DIK_D) || input_->TriggerKey(DIK_RIGHT)) {
            delta = kVolumeStep;
        }
        if (delta != 0.0f) {
            SceneShared::AdjustVolume(audio_, row == kPauseRowBgmVolume, delta);
        }
    }

    if (!pauseMenu_.ConsumeConfirm(input_)) {
        return;
    }
    switch (row) {
    case kPauseRowResume:
        pauseController_.Resume();
        break;
    case kPauseRowQuit: {
        auto* rd = RunData::GetInstance();
        if (rd->IsRunActive()) {
            SaveDataManager::GetInstance()->SaveContinue(*rd);
        }
        pauseController_.Resume();
        SceneFlow::GetInstance()->Transition("GAMEPLAY", "pause_quit", "TITLE");
        break;
    }
    default:
        break;
    }
}

void GamePlayScene::DrawPauseOverlay()
{
    UILayout& layout = UILayout::Get(kLayoutName);
    pauseOverlay_->SetColor(layout.Color("pause.background_color", kPauseOverlayColor));
    pauseOverlay_->Update();
    pauseOverlay_->Draw();
    pauseMenu_.Draw();

    const float volumeScale = layout.Float("pause.volume_scale", kVolumeTextScale);
    const Vector4 volumeColor = layout.Color("pause.volume_color", kVolumeTextColor);
    const Vector2 bgmPosition = layout.Pos("pause.bgm_volume_pos", kBgmVolumeTextPosition);
    const Vector2 sePosition = layout.Pos("pause.se_volume_pos", kSeVolumeTextPosition);
    const auto& settings = GameSettingsManager::GetInstance()->Get();
    char bgmBuf[32];
    char seBuf[32];
    snprintf(bgmBuf, sizeof(bgmBuf), "< %3d%% >", static_cast<int>(settings.bgmVolume * kPercentScale + kRoundingOffset));
    snprintf(seBuf, sizeof(seBuf), "< %3d%% >", static_cast<int>(settings.seVolume * kPercentScale + kRoundingOffset));
    fontRenderer_.DrawString(bgmBuf, bgmPosition.x, bgmPosition.y, volumeScale, volumeColor);
    fontRenderer_.DrawString(seBuf, sePosition.x, sePosition.y, volumeScale, volumeColor);
    const Vector2 titlePosition = layout.Pos("pause.title_pos", kPausedTitlePosition);
    fontRenderer_.DrawString("PAUSED", titlePosition.x, titlePosition.y,
        layout.Float("pause.title_scale", kPausedTitleScale), layout.Color("pause.title_color", kPausedTitleColor));
}

void GamePlayScene::DrawStageGuide()
{
    // 区画ごとの案内文はレベルJSONのui_text（トリガーのフラグで切り替わる）が担当する。ここは収集物の個数だけ
    int collectedPickups = 0;
    int totalPickups = 0;
    GetStageEditor().GetPickupCounts(collectedPickups, totalPickups);
    if (totalPickups <= 0) {
        return;
    }
    const std::wstring coreCount = L"エネルギーコア  "
        + std::to_wstring(collectedPickups) + L" / "
        + std::to_wstring(totalPickups);
    UILayout& layout = UILayout::Get(kLayoutName);
    const Vector2 counterPosition = layout.Pos("pickup_counter.pos", kPickupCounterPosition);
    fontRenderer_.DrawStringW(coreCount, counterPosition.x, counterPosition.y,
        layout.Float("pickup_counter.scale", kPickupCounterScale), layout.Color("pickup_counter.color", kPickupCounterColor));
}

void GamePlayScene::DrawWeaponListPanel()
{
    // BattleTestScene::DrawWeaponHud()と同じ体裁左上アンカーに武器スロット一覧＋操作ヒント
    UILayout& layout = UILayout::Get(kLayoutName);
    const float hintScale = layout.Float("weapon_list.hint_scale", kHintScale);
    const Vector4 hintColor = layout.Color("weapon_list.hint_color", kHintColor);
    auto drawShadowedHint = [&](const std::wstring& text, float x, float y) {
        fontRenderer_.DrawStringW(text, x + kHintShadowOffset, y + kHintShadowOffset, hintScale, kHintShadowColor);
        fontRenderer_.DrawStringW(text, x, y, hintScale, hintColor);
    };

    const Vector2 weaponHudAnchor = layout.Pos("weapon_list.pos", kDefaultHudWeaponAnchor);
    float py = SceneShared::DrawWeaponListHud(fontRenderer_, WeaponManager::GetInstance(),
        L"メインステージ", weaponHudAnchor);
    drawShadowedHint(L"[L] コンボ  [S+L] 打ち上げ  [空中L] 空中コンボ  [I] 回避", weaponHudAnchor.x, py);
    drawShadowedHint(L"[K] 射撃  [R] 覚醒  [Shift長押し] ロックオン（最寄りの敵）", weaponHudAnchor.x, py + kHintLineHeight);
}

bool GamePlayScene::IsGlassShatterFlow() const
{
    return glassShatterDebugTest_ || !RunData::GetInstance()->IsRunActive();
}

void GamePlayScene::TriggerGlassShatterTest()
{
    if (clearTriggered_) {
        return;
    }
    glassShatterDebugTest_ = true;
    clearTriggered_ = true;
    glassShatter_.Start();
}

void GamePlayScene::Finalize()
{
    // enemy_/weaponEnemies_はStageEditor所有（非所有ポインタ）のため、EnemyRegistryへの
    // 登録解除もStageEditor自身のReleaseLevelResources()が行う。ここでは何もしない
    ImGuiControlPanel::RegisterGlassShatterTrigger(nullptr);
    SceneEffectBridge::GetInstance()->SetCameraShakeHandler({ });
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
