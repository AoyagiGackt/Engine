/**
 * @file GamePlaySceneUI.cpp
 * @brief GamePlaySceneのHUD・オーバーレイ・武器スロット表示・スタイルコマンド表示を実装するファイル
 * @note GamePlayScene のHUD表示だけを GamePlayScene.cpp から分けて置いている（クラスは GamePlayScene のまま）
 */
#include "GamePlayScene.h"
#include "HitEffectLibrary.h"
#include "WeaponSlotHud.h"
#include "AwakenGaugeHud.h"
#include "AudioBridge.h"
#include "CombatTuning.h"
#include "FinisherEffects.h"
#include "GameConstants.h"
#include "GamePlaySceneInitializer.h"
#include "GameSettings.h"
#include "GrayscaleEffect.h"
#include "HsvFilter.h"
#include "ImGuiControlPanel.h"
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
using namespace engine::graphics;

namespace engine::game {
namespace {
constexpr float kWeaponStealPromptHalfWidth = 60.0f; // 文字列の見た目の半幅ぶんだけ左へずらして中央寄せする
constexpr Vector4 kFinisherOverlayTint = { 0.0f, 0.0f, 0.05f, 0.0f }; // アルファはHitEffectLibrary::GetInstance()->SceneParticles().finisherOverlayAlpha

// 以下は既定の配置（F2のエディタで動かした値は Resources/Config/UI/gameplay.json に保存される）
constexpr const char* kLayoutName = "gameplay";

// 武器の疲労警告はデバッグボタンの下に配置する
constexpr Vector2 kFatigueWarningPosition = { gameConstants::kScreenCenterX, 140.0f };
constexpr float kFatigueTitleScale = 1.6f;
constexpr float kFatigueDetailScale = 1.2f;
constexpr float kFatigueDetailOffsetY = 34.0f;
constexpr Vector4 kFatigueMildColor = { 1.0f, 0.85f, 0.25f, 1.0f }; // 効きが落ち始めた
constexpr Vector4 kFatigueSevereColor = { 1.0f, 0.3f, 0.2f, 1.0f }; // ほとんど効かない
constexpr float kFatigueSevereThreshold = 0.5f; // 落ち幅がこの割合を超えたら強い警告に切り替える
constexpr float kFatigueBlinkSpeed = 10.0f;
constexpr float kFatigueBlinkMinAlpha = 0.45f;
constexpr float kPercent = 100.0f;

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
constexpr int kWeaponSlotCount = WeaponManager::kSlotCount;
constexpr Vector2 kExchangeTitlePosition = { 360.0f, 220.0f };
constexpr float kExchangeTitleScale = 2.0f;
constexpr Vector4 kExchangeTitleColor = { 1.0f, 0.85f, 0.2f, 1.0f };
constexpr Vector2 kExchangeWeaponPosition = { 410.0f, 270.0f };
constexpr float kExchangeWeaponScale = 1.6f;
constexpr Vector4 kExchangeWeaponColor = { 0.8f, 0.95f, 1.0f, 1.0f };
constexpr Vector2 kExchangeHelpPosition = { 245.0f, 330.0f };
constexpr float kExchangeHelpScale = 1.35f;
constexpr Vector4 kExchangeHelpColor = { 1.0f, 1.0f, 1.0f, 1.0f };

// 収集物の個数表示
constexpr Vector2 kPickupCounterPosition = { 24.0f, 540.0f };
constexpr float kPickupCounterScale = 1.35f;
constexpr Vector4 kPickupCounterColor = { 0.3f, 0.9f, 1.0f, 1.0f };
}

/** @brief HPバー・ランク・武器欄・エディタ配置UIなどHUDのスプライトを描く（一時停止中は呼ばない） */
void GamePlayScene::DrawHudSprites() {
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

    sceneEditor_.DrawUIElements();
}

/** @brief Draw()の下請け エディタUI・フィニッシャー演出・フォントなど2D上乗せ描画をまとめて行う */
void GamePlayScene::DrawOverlaysAndUI() {
    spriteCommon_->CommonDrawSettings();

    // 一時停止中と開始演出・やられ演出の間はHUDを隠す
    if (hudReady_ && !pauseMenu_.IsPaused() && !stageIntro_.IsPlaying() && !playerDefeat_.IsActive() && !clearTriggered_) {
        DrawHudSprites();
        debugGameOverButton_.Draw();
        debugClearButton_.Draw();
    }
    stageIntro_.Draw();
    playerDefeat_.Draw();

    // 大技中と解放フレーム（凍結画面のキャプチャ前）だけ暗転を重ねる
    // 解放後の暗さは砕け散る凍結画面が持ち去るので、素の世界には重ねない
    const bool captureFrame = finisherShatter_.IsActive() && finisherShatter_.NeedCapture();
    if (finisherActive_ || captureFrame) {
        finisherOverlay_->SetColor({ kFinisherOverlayTint.x, kFinisherOverlayTint.y, kFinisherOverlayTint.z,
            HitEffectLibrary::GetInstance()->SceneParticles().finisherOverlayAlpha });
        finisherOverlay_->Update();
        finisherOverlay_->Draw();
    }
    SlashMark::GetInstance()->Draw();

    // 解放時の世界割れ（暗転+斬撃線ごと凍った画面を砕き、下から素の世界が現れる）
    if (finisherShatter_.IsActive() && IsDrawingToBackBuffer()) {
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

    if (pauseMenu_.IsPaused()) {
        pauseMenu_.Draw(input_);
    } else if (!stageIntro_.IsPlaying() && !playerDefeat_.IsActive() && !clearTriggered_) {
        // ゲームプレイ UI テキスト（レベルJSONの案内文）
        GetStageEditor().DrawUIText(fontRenderer_);
    }
    fontRenderer_.Draw();

    // クリアの決め演出: 暗幕・斬線・弾痕を重ね、割り始めるフレームでその画面を撮って破片を重ねる
    // （以降のフレームは DrawClearOverlayIfNeeded() が結果画面の上に破片を重ねる）
    if (clearTriggered_ && clearFinish_.IsActive() && clearFinish_.ShowsGameFrame()) {
        spriteCommon_->CommonDrawSettings();
        clearFinish_.DrawStrikeOverlay();
        if (glassShatter_.NeedCapture()) {
            glassShatter_.CaptureFrame();
            glassShatter_.Apply();
        }
    }
}

// ══════════════════════════════════════════════════════
// HUD描画
// ══════════════════════════════════════════════════════

/** @brief ラン中だけ、敵の残り体力などの周回用 HUD を描画する */
void GamePlayScene::DrawRogueliteHUD() {
    auto* rd = RunData::GetInstance();
    if (!rd->IsRunActive()) {
        return;
    }

    // 敵の残り体力を上部中央に数値で表示する
    const int enemyHp = enemy_ ? enemy_->GetHp() : 0;
    const int enemyMaxHp = enemy_ ? enemy_->GetMaxHp() : 0;
    const std::wstring enemyHpText = L"ボス体力  "
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

/** @brief ボス・道中の武器敵の頭上HPバーの位置と色を更新する（BattleTestSceneのダミーHPバーと同じ体裁） */
void GamePlayScene::UpdateEnemyHpBars() {
    UILayout& layout = UILayout::Get(kLayoutName);
    const float hpBarGreenScale = layout.Float("hp_bar.green_scale", kHpBarGreenScale);
    const float hpBarGreenBase = layout.Float("hp_bar.green_base", kHpBarGreenBase);
    const float enemyHpBarAlpha = layout.Float("enemy_hp_bar.alpha", kEnemyHpBarAlpha);
    // BattleTestScene::UpdateHpBars()と同じ体裁（頭上に固定サイズのバー、WorldToScreenで追従）
    const Vector3& cam = camera_->GetTranslate();
    const Vector2 barSize = layout.Vec2("enemy_hp_bar.size", kEnemyHpBarSize);
    const float barRaise = layout.Float("enemy_hp_bar.raise", kEnemyHpBarRaise);

    const sceneShared::HpBarStyle style { barSize, barRaise, hpBarGreenScale, hpBarGreenBase, enemyHpBarAlpha };
    auto layoutBar = [&](Sprite* background, Sprite* foreground, const Vector3& position, float ratio) {
        sceneShared::UpdateWorldHpBar(background, foreground, position, cam, ratio, style);
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

/** @brief 左下のプレイヤーHPゲージの位置と色を更新する */
void GamePlayScene::UpdatePlayerHpBar() {
    UILayout& layout = UILayout::Get(kLayoutName);
    const float hpBarGreenScale = layout.Float("hp_bar.green_scale", kHpBarGreenScale);
    const float hpBarGreenBase = layout.Float("hp_bar.green_base", kHpBarGreenBase);
    const float playerHpBarAlpha = layout.Float("player_hp_bar.alpha", kPlayerHpBarAlpha);
    auto* rd = RunData::GetInstance();
    if (!rd->IsRunActive() || rd->GetMaxHp() <= 0) {
        return;
    }

    const Vector2 barPosition = layout.Pos("player_hp_bar.pos", kPlayerHpBarPosition);
    const Vector2 barSize = layout.Vec2("player_hp_bar.size", kPlayerHpBarSize);

    const float ratio = std::clamp(static_cast<float>(rd->GetHp()) / static_cast<float>(rd->GetMaxHp()), 0.0f, 1.0f);
    playerHpBarBg_->SetPosition(barPosition);
    playerHpBarBg_->SetSize(barSize);
    playerHpBarBg_->Update();

    playerHpBarFg_->SetColor({ 1.0f - ratio, ratio * hpBarGreenScale + hpBarGreenBase, 0.0f, playerHpBarAlpha });
    playerHpBarFg_->SetPosition(barPosition);
    playerHpBarFg_->SetSize({ barSize.x * ratio, barSize.y });
    playerHpBarFg_->Update();
}

/** @brief スタイルランクと各種HUDを描画する */
void GamePlayScene::DrawStyleUI() {
    // 表示位置・大きさ・色はすべて UI レイアウトから読む（F2 で調整できる）
    UILayout& layout = UILayout::Get(kLayoutName);
    const float weaponStealPromptHalfWidth = layout.Float("steal_prompt.half_width", kWeaponStealPromptHalfWidth);
    const float guideTitleScale = layout.Float("combo_guide.title_scale", kGuideTitleScale);
    const Vector4 guideTitleColor = layout.Color("combo_guide.title_color", kGuideTitleColor);
    const float guideRouteOffsetY = layout.Float("combo_guide.route_offset_y", kGuideRouteOffsetY);
    const float guideRouteScale = layout.Float("combo_guide.route_scale", kGuideRouteScale);
    const Vector4 guideRouteColor = layout.Color("combo_guide.route_color", kGuideRouteColor);
    const float guideProgressScale = layout.Float("combo_guide.progress_scale", kGuideProgressScale);
    const Vector4 guideProgressColor = layout.Color("combo_guide.progress_color", kGuideProgressColor);
    const float guideNextOffsetY = layout.Float("combo_guide.next_offset_y", kGuideNextOffsetY);
    const float guideNextScale = layout.Float("combo_guide.next_scale", kGuideNextScale);
    const Vector4 guideNextAirColor = layout.Color("combo_guide.next_air_color", kGuideNextAirColor);
    const Vector4 guideNextOpenerColor = layout.Color("combo_guide.next_opener_color", kGuideNextOpenerColor);
    const Vector4 guideNextLauncherColor = layout.Color("combo_guide.next_launcher_color", kGuideNextLauncherColor);
    const Vector4 guideFinishColor = layout.Color("combo_guide.finish_color", kGuideFinishColor);
    const float weaponStealPromptScale = layout.Float("steal_prompt.scale", kWeaponStealPromptScale);
    const Vector4 weaponStealPromptColor = layout.Color("steal_prompt.color", kWeaponStealPromptColor);
    const float lockMarkerHalfWidth = layout.Float("lock_marker.half_width", kLockMarkerHalfWidth);
    const float lockMarkerScale = layout.Float("lock_marker.scale", kLockMarkerScale);
    const Vector4 lockMarkerColor = layout.Color("lock_marker.color", kLockMarkerColor);
    // 文字を積み直し、HPバー・ステージ案内・ラン情報・スタイルランクを更新する
    fontRenderer_.Reset();
    // 開始演出が終わった直後は GO! の文字がまだ消えていないので、HUD と一緒に積む
    stageIntro_.QueueText(fontRenderer_);

    UpdateEnemyHpBars();
    UpdatePlayerHpBar();
    DrawStageGuide();
    DrawRogueliteHUD();
    // 確認用ボタンの文字（F2 の編集中は Update を通らないので、ここで配置を読み直してから積む）
    debugGameOverButton_.RefreshLayout();
    debugGameOverButton_.QueueText(fontRenderer_);
    debugClearButton_.RefreshLayout();
    debugClearButton_.QueueText(fontRenderer_);
    styleRankHud_.UpdateHud(fontRenderer_); // 右上のスタイリッシュランク（BattleTestSceneと同じ体裁）

    // 基本ルートは常時見せ、コンボ開始後は現在地点と次の入力へ表示を切り替える。
    // トレーニングで覚えた操作を本編でも同じ順番で再確認できる導線にする。
    if (WeaponManager::GetInstance()->HasEquippedWeapon()) {
        const Vector2 guidePosition = layout.Pos("combo_guide.pos", kGuidePosition);
        const float guideX = guidePosition.x;
        const float guideY = guidePosition.y;
        // コンボ前は推奨ルートを、コンボ中は進み具合と次の一手を出す
        const bool chainActive = styleRankHud_.GetHitCount() > 0 || player_->IsMeleeAttacking();
        const int step = (std::max)(player_->GetComboStep(), 1);
        const int maxStep = (std::max)(player_->GetComboMax(), 1);
        if (!chainActive) {
            fontRenderer_.DrawStringW(L"推奨コンボルート",
                guideX, guideY, guideTitleScale, guideTitleColor);
            fontRenderer_.DrawStringW(input_->ExpandPrompts(L"[{Attack}] x2  >  [{Down}+{Attack}] 打ち上げ  >  [{Jump}] ジャンプ  >  [{Attack}] 空中追撃  >  [{Shoot}] 射撃"),
                guideX, guideY + guideRouteOffsetY, guideRouteScale, guideRouteColor);
        } else {
            std::wstring progress = L"COMBO  ";
            for (int i = 1; i <= maxStep; ++i) {
                progress += i <= step ? L"● " : L"○ ";
            }
            fontRenderer_.DrawStringW(progress, guideX, guideY, guideProgressScale, guideProgressColor);

            // 次の一手の案内（上の行から順に見て、最初に当てはまった案内を出す。最後の行は必ず当てはまる）
            struct GuideState {
                bool airborne;
                int step;
                int maxStep;
            };
            /** @brief 次の行動の案内文と表示条件の組 */
            struct NextHint {
                bool (*applies)(const GuideState&);
                const wchar_t* text;
                Vector4 color;
            };
            const NextHint kNextHints[] = {
                { [](const GuideState& g) {
                    return g.airborne;
                },
                    L"NEXT  [{Attack}] 空中追撃   [{Shoot}] 射撃   [{Skill}] 固有技", guideNextAirColor },
                { [](const GuideState& g) {
                    return g.step < kGuideOpenerSteps;
                },
                    L"NEXT  [{Attack}] もう一撃   または  [{Down}+{Attack}] 打ち上げ", guideNextOpenerColor },
                { [](const GuideState& g) {
                    return g.step < g.maxStep;
                },
                    L"NEXT  [{Down}+{Attack}] 打ち上げ   [{Slot}] 武器切替", guideNextLauncherColor },
                { [](const GuideState&) {
                    return true;
                },
                    L"FINISH!   [{Down}+{Attack}] 打ち上げ   [{Slot}] 別武器へ", guideFinishColor },
            };
            const GuideState state = { !player_->IsOnGround(), step, maxStep };
            for (const NextHint& hint : kNextHints) {
                if (hint.applies(state)) {
                    fontRenderer_.DrawStringW(input_->ExpandPrompts(hint.text),
                        guideX, guideY + guideNextOffsetY, guideNextScale, hint.color);
                    break;
                }
            }
        }
    }
    // 武器の疲労警告・武器一覧・操作説明
    DrawWeaponFatigueWarning();
    DrawWeaponListPanel();
    sceneShared::DrawControlsHud(fontRenderer_,
        layout.Pos("controls.pos", kGameplayControlsAnchor), L": ステージを進む");
    hud_.QueueText(fontRenderer_);
    // 倒した敵の頭上に武器回収の操作を出す（死んだかどうか・奪えるかが遠目にも分かるように）
    constexpr float kWeaponStealPromptHeight = 2.0f; // ロックオンマーカー（y+1.6）と重ならない高さ
    auto drawWeaponStealPrompt = [&](const Vector3& enemyPos) {
        const Vector3& cam = camera_->GetTranslate();
        float sx, sy;
        sceneShared::WorldToScreen(enemyPos.x, enemyPos.y + kWeaponStealPromptHeight, cam.x, cam.y, sx, sy);
        fontRenderer_.DrawStringW(input_->ExpandPrompts(L"[{Steal}] 武器を回収"), sx - weaponStealPromptHalfWidth, sy, weaponStealPromptScale,
            weaponStealPromptColor);
    };
    // ボスと道中の敵のうち、倒れて武器を奪える敵の頭上に出す
    if (enemy_ && enemy_->IsDefeated() && !weaponStealTriggered_ && !mainWeaponAbsorbing_) {
        drawWeaponStealPrompt(enemy_->GetWeaponPickupPosition());
    }
    for (const auto& weaponEnemy : weaponEnemies_) {
        if (weaponEnemy.hasWeapon && weaponEnemy.enemy->IsDefeated() && !weaponEnemy.weaponAcquired && !weaponEnemy.absorbing) {
            drawWeaponStealPrompt(weaponEnemy.enemy->GetWeaponPickupPosition());
        }
    }

    // ── ロックオン中の対象にマーカーを出す ────────────────────────
    if (lockedEnemy_ != nullptr) {
        const Vector3 tpos = lockedEnemy_->GetPosition();
        const Vector3& cam = camera_->GetTranslate();
        float sx, sy;
        sceneShared::WorldToScreen(tpos.x, tpos.y + kLockMarkerHeight, cam.x, cam.y, sx, sy);
        fontRenderer_.DrawString("v LOCK v", sx - lockMarkerHalfWidth, sy, lockMarkerScale, lockMarkerColor);
    }

    // スロットが満杯の時の武器交換画面
    DrawWeaponExchange();
    // 開始演出の直後の1フレームは、HUDの配置が済んでから描画する
    hudReady_ = true;
}

/** @brief テストシーンと共通の武器スロットUIを初期化する */
void GamePlayScene::InitializeWeaponSlotHud() {
    hud_.Add(std::make_unique<AwakenGaugeHud>());
    hud_.Add(std::make_unique<WeaponSlotHud>());
    hud_.Initialize({ *spriteCommon_, *modelCommon_, *dxCommon_, *WeaponManager::GetInstance() });
    UpdateWeaponSlotHud();
}

/** @brief 武器スロットUIの選択状態と演出を更新する */
void GamePlayScene::UpdateWeaponSlotHud() {
    HudFrame frame { *camera_, gameConstants::kFrameDeltaTime, player_->GetAwakenGauge(), player_->IsAwakened(), auraTimer_ };
    frame.input = input_;
    hud_.Update(frame);
}

/** @brief 武器スロットUIを描画する */
void GamePlayScene::DrawWeaponSlotHud() {
    hud_.Draw();
}

/** @brief 満杯時の武器交換入力を処理する */
void GamePlayScene::UpdateWeaponExchange() {
    auto* wm = WeaponManager::GetInstance();
    if (!wm->HasPendingWeapon()) {
        return;
    }

    // 十字キーは通常の武器切替と同じ並び（上=1、右=2、下=3）
    constexpr WORD kSlotPadButtons[kWeaponSlotCount] = {
        XINPUT_GAMEPAD_DPAD_UP, XINPUT_GAMEPAD_DPAD_RIGHT, XINPUT_GAMEPAD_DPAD_DOWN
    };
    for (int slot = 0; slot < kWeaponSlotCount; ++slot) {
        if (input_->TriggerKey(static_cast<uint8_t>(DIK_1 + slot)) || input_->TriggerButton(kSlotPadButtons[slot])) {
            wm->ReplacePendingWeapon(slot);
            pendingWeaponMandatory_ = false;
            hud_.Notify(HudEvent::kWeaponAcquired);
            return;
        }
    }
    if (!pendingWeaponMandatory_ && (input_->TriggerKey(DIK_BACK) || input_->TriggerButton(XINPUT_GAMEPAD_B))) {
        wm->DiscardPendingWeapon();
    }
}

/** @brief 満杯時の武器交換画面を描画する */
void GamePlayScene::DrawWeaponExchange() {
    auto* wm = WeaponManager::GetInstance();
    if (!wm->HasPendingWeapon()) {
        return;
    }

    UILayout& layout = UILayout::Get(kLayoutName);
    const Vector2 titlePosition = layout.Pos("exchange.title_pos", kExchangeTitlePosition);
    fontRenderer_.DrawStringW(L"武器スロットが満杯です", titlePosition.x, titlePosition.y,
        layout.Float("exchange.title_scale", kExchangeTitleScale), layout.Color("exchange.title_color", kExchangeTitleColor));
    const Vector2 weaponPosition = layout.Pos("exchange.weapon_pos", kExchangeWeaponPosition);
    // 開始演出と同じく日本語のスタイル名で見せる
    fontRenderer_.DrawStringW(L"入手武器  " + WeaponManager::GetDisplayName(wm->GetPendingWeapon()),
        weaponPosition.x, weaponPosition.y,
        layout.Float("exchange.weapon_scale", kExchangeWeaponScale), layout.Color("exchange.weapon_color", kExchangeWeaponColor));
    const Vector2 helpPosition = layout.Pos("exchange.help_pos", kExchangeHelpPosition);
    const bool pad = input_->IsUsingGamepad();
    const std::wstring slotKeys = pad ? L"十字キー(上・右・下)" : L"1から3";
    const std::wstring discard = pad ? L"Bで破棄" : L"Backspaceで破棄";
    fontRenderer_.DrawStringW(slotKeys + L"で交換するスロットを選択  "
            + (pendingWeaponMandatory_ ? std::wstring(L"ボスの武器は破棄できない") : discard),
        helpPosition.x, helpPosition.y,
        layout.Float("exchange.help_scale", kExchangeHelpScale), layout.Color("exchange.help_color", kExchangeHelpColor));
}

// ══════════════════════════════════════════════════════
// 一時停止メニュー
// ══════════════════════════════════════════════════════

/** @brief 一時停止メニューの項目を登録する（退出はステージセレクトへ戻る） */
void GamePlayScene::SetupPauseMenu() {
    pauseMenu_.Initialize(spriteCommon_.get(), &fontRenderer_, audio_, kLayoutName,
        { "STAGE SELECT", [] {
             auto* rd = RunData::GetInstance();
             if (rd->IsRunActive()) {
                 SaveDataManager::GetInstance()->SaveContinue(*rd);
             }
             SceneFlow::GetInstance()->Transition(kSceneName, "pause_quit", "MAP");
         } });
}
/** @brief プレイヤーの進行位置に対応する操作目標を描画する */
void GamePlayScene::DrawStageGuide() {
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
/** @brief 同じ武器で当て続けて効きが落ちている時に、持ち替えを促す警告を出す */
void GamePlayScene::DrawWeaponFatigueWarning() {
    UILayout& layout = UILayout::Get(kLayoutName);
    const float fatigueDetailOffsetY = layout.Float("fatigue.detail_offset_y", kFatigueDetailOffsetY);
    const Vector4 fatigueMildColor = layout.Color("fatigue.mild_color", kFatigueMildColor);
    const Vector4 fatigueSevereColor = layout.Color("fatigue.severe_color", kFatigueSevereColor);
    // 今の武器の疲労で威力が落ちていなければ出さない
    const float mult = WeaponFatigueDamageMult(lastFatigueIndex_);
    if (mult >= 1.0f) {
        return;
    }
    // 威力の落ち具合を 0〜1 にし、一定以上なら強い警告にする
    const float minMult = CombatTuning::GetInstance()->Get().weaponFatigueMinDamageMult;
    const float severity = minMult < 1.0f ? std::clamp((1.0f - mult) / (1.0f - minMult), 0.0f, 1.0f) : 0.0f;
    const bool severe = severity >= kFatigueSevereThreshold;
    // 持ち替え先として一番休めている武器（銃を含む）を探し、押すボタンまで具体的に示す
    const auto* wm = WeaponManager::GetInstance();
    int bestSlot = -1;
    float bestFatigue = 1.0f;
    for (int slot = 0; slot < WeaponManager::kSlotCount; ++slot) {
        const int weaponIndex = wm->GetSlotWeaponIndex(slot);
        if (weaponIndex < 0 || weaponIndex == lastFatigueIndex_ || weaponIndex >= static_cast<int>(weaponFatigue_.size())) {
            continue;
        }
        if (weaponFatigue_[weaponIndex] < bestFatigue) {
            bestFatigue = weaponFatigue_[weaponIndex];
            bestSlot = slot;
        }
    }
    // 銃のほうが休めていれば射撃を勧める
    const int gunIndex = GunFatigueIndex();
    const bool suggestGun = gunIndex != lastFatigueIndex_ && gunIndex < static_cast<int>(weaponFatigue_.size())
        && (bestSlot < 0 || weaponFatigue_[gunIndex] < bestFatigue);
    const std::wstring suggestion = (!suggestGun && bestSlot >= 0)
        ? L"[" + std::to_wstring(bestSlot + 1) + L"] に持ち替えろ"
        : input_->ExpandPrompts(L"[{Shoot}] 射撃を混ぜろ");
    // 強い警告は点滅させ、見出しと 威力% と勧める操作 を2行で出す
    const Vector2 pos = layout.Pos("fatigue.pos", kFatigueWarningPosition);
    Vector4 color = severe ? fatigueSevereColor : fatigueMildColor;
    if (severe) {
        const float blink = 0.5f + 0.5f * std::sin(floorElapsedSeconds_ * kFatigueBlinkSpeed);
        color.w = kFatigueBlinkMinAlpha + (1.0f - kFatigueBlinkMinAlpha) * blink;
    }
    // 各行の実測幅で中央をそろえる。
    constexpr float kCenterFactor = 0.5f;
    const std::wstring title = severe ? L"同じ武器ではもう効かない!" : L"同じ武器で効きが落ちてきた";
    const int percent = static_cast<int>(std::round(mult * kPercent));
    const std::wstring detail = L"威力 " + std::to_wstring(percent) + L"%    " + suggestion;
    const float titleScale = layout.Float("fatigue.title_scale", kFatigueTitleScale);
    const float detailScale = layout.Float("fatigue.detail_scale", kFatigueDetailScale);
    fontRenderer_.DrawStringW(title, pos.x - FontRenderer::MeasureStringW(title, titleScale).x * kCenterFactor,
        pos.y, titleScale, color);
    fontRenderer_.DrawStringW(detail, pos.x - FontRenderer::MeasureStringW(detail, detailScale).x * kCenterFactor,
        pos.y + fatigueDetailOffsetY, detailScale, color);
}
/** @brief 左上の武器スロット一覧を描画する（BattleTestSceneと同じ体裁） */
void GamePlayScene::DrawWeaponListPanel() {
    UILayout& layout = UILayout::Get(kLayoutName);
    // 左上には武器一覧と切り替え案内を表示する。
    const Vector2 weaponHudAnchor = layout.Pos("weapon_list.pos", kDefaultHudWeaponAnchor);
    std::vector<float> powerRatios(WeaponManager::GetInstance()->GetList().size(), 1.0f);
    for (int i = 0; i < static_cast<int>(powerRatios.size()); ++i) {
        powerRatios[i] = WeaponFatigueDamageMult(i);
    }
    sceneShared::DrawWeaponListHud(fontRenderer_, WeaponManager::GetInstance(),
        L"メインステージ", weaponHudAnchor, &powerRatios);
}
/** @brief シーン固有の演出資源と登録済みコールバックを破棄する */
void GamePlayScene::Finalize() {
    motionEditor_.Close(*player_);
    // enemy_/weaponEnemies_はStageEditor所有（非所有ポインタ）のため、EnemyRegistryへの
    // 登録解除もStageEditor自身のReleaseLevelResources()が行う。ここでは何もしない
    // デバッグパネルとカメラ揺れの受け口から、このシーンの登録を外す
    ImGuiControlPanel::RegisterGlassShatterTrigger(nullptr);
    SceneEffectBridge::GetInstance()->SetCameraShakeHandler({ });
    // 描画先と演出の資源を片付ける
    renderTexture_->Finalize(srvManager_);
    pm_->ClearAllGroups();
    finisherEffects::FinalizeAll(glassShatter_, finisherShatter_, spaceWarp_, bladeFlash_);
    comboFeel_.Finalize();
    // 音を全部止める（BGM・SE どちらも）
    if (audio_) {
        audio_->StopBGM();
        audio_->StopAllSE();
    }
}
} // namespace engine::game
