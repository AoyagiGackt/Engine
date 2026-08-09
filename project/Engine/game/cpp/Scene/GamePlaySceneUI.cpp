/**
 * @file GamePlaySceneUI.cpp
 * @brief GamePlaySceneのHUD・オーバーレイ・武器スロット表示・スタイルコマンド表示を実装するファイル
 * @note GamePlayScene.cppからの分割ファイルクラス自体はGamePlaySceneのまま、定義の置き場所だけを分けている
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

void GamePlayScene::DrawOverlaysAndUI()
{
    spriteCommon_->CommonDrawSettings();

    awakenGaugeBg_->Draw();
    if (player_->GetAwakenGauge() > 0.0f) {
        awakenGaugeFg_->Draw();
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
        finisherOverlay_->SetColor({ 0.0f, 0.0f, 0.05f, GameConstants::kFinisherOverlayAlpha });
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
    fontRenderer_.DrawStringW(enemyHpText, 460.0f, 10.0f, 1.5f,
        { 1.0f, 0.35f, 0.35f, 1.0f });

    // プレイヤーHP + ゴールド左上は武器一覧パネルと被るため、左下の武器スロットHUDの真上に置く
    std::string info = "HP:" + std::to_string(rd->GetHp()) + "/" + std::to_string(rd->GetMaxHp())
        + "  G:" + std::to_string(rd->GetGold());
    constexpr float kInfoX = 24.0f;
    const float infoY = static_cast<float>(WinApp::kClientHeight) - 120.0f;
    fontRenderer_.DrawString(info.c_str(), kInfoX, infoY, 1.5f, { 0.3f, 1.0f, 0.4f, 1.0f });
}

void GamePlayScene::DrawStyleUI()
{
    fontRenderer_.Reset();

    DrawStageGuide();
    DrawRogueliteHUD();
    styleRankHud_.UpdateHud(fontRenderer_); // 右上のスタイリッシュランク（BattleTestSceneと同じ体裁）
    DrawWeaponListPanel();
    SceneShared::DrawControlsHud(fontRenderer_,
        GetStageEditor().GetHudAnchorPosition("hud_anchor_controls", { 1020.0f, 12.0f }), L": ステージを進む");
    SceneShared::DrawAwakenGaugeHud(fontRenderer_, awakenGaugeBg_.get(), awakenGaugeFg_.get(),
        player_->GetAwakenGauge(), player_->IsAwakened(), auraTimer_);
    if (enemy_->IsDefeated() && !weaponStealTriggered_) {
        fontRenderer_.DrawStringW(L"[ J ] 敵の武器を吸収", 500.0f, 500.0f, 1.6f,
            { 0.55f, 0.9f, 1.0f, 1.0f });
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
            SceneShared::WorldToScreen(tpos.x, tpos.y + 1.6f, cam.x, cam.y, sx, sy);
            fontRenderer_.DrawString("v LOCK v", sx - 46.0f, sy, 1.3f, { 1.0f, 0.35f, 0.2f, 1.0f });
        }
    }

    DrawWeaponExchange();
}

void GamePlayScene::InitializeWeaponSlotHud()
{
    constexpr float size = 56.0f;
    constexpr float gap = 10.0f;
    constexpr float marginX = 24.0f;
    const float y = static_cast<float>(WinApp::kClientHeight) - 90.0f;

    SceneShared::InitializeWeaponSlotHud(spriteCommon_.get(), WeaponManager::GetInstance(),
        weaponSlots_.data(), weaponSlotPos_.data(), kWeaponSlotCount,
        size, gap, marginX, y, /*checkUnlockedForInitialColor=*/false,
        gunFrame_, gunIcon_, gunPos_);

    // 各スタイルに対応する実物3Dモデル（色塗り四角だけでは何の武器か分からないため、BattleTestSceneと同じ仕組みで重ねて表示する）
    const std::vector<SceneShared::WeaponIconAsset> kIconAssets = SceneShared::LoadWeaponIconAssets("Resources/Config/weapon_icons.json");

    const auto& list = WeaponManager::GetInstance()->GetList();
    for (int i = 0; i < kWeaponSlotCount && i < static_cast<int>(list.size()); ++i) {
        for (const auto& asset : kIconAssets) {
            if (list[i].type != asset.type) {
                continue;
            }
            auto& icon3d = weaponIcons3D_[i];
            icon3d.slotIndex = i;
            icon3d.scale = asset.scale;
            icon3d.baseYaw = asset.baseYaw;
            icon3d.model = std::make_unique<Model>();
            icon3d.model->Initialize(modelCommon_.get(), asset.modelPath, asset.texturePath);
            icon3d.object = std::make_unique<Object3d>();
            icon3d.object->Initialize(modelCommon_.get());
            icon3d.object->SetModel(icon3d.model.get());
            icon3d.object->SetEnableLighting(true);
            break;
        }
    }
}

void GamePlayScene::UpdateWeaponSlotHud()
{
    weaponSlotPulse_ += GameConstants::kFrameDeltaTime;
    gunIconAngle_ += GameConstants::kFrameDeltaTime * 0.6f;

    auto* wm = WeaponManager::GetInstance();
    const int activeIndex = wm->GetSelectedSlot();
    const float pulse = 0.7f + 0.3f * std::sin(weaponSlotPulse_ * 6.0f);

    SceneShared::UpdateWeaponSlotHud(wm, weaponSlots_.data(), kWeaponSlotCount,
        weaponSlotPulse_, /*flash=*/0.0f, gunIcon_.get(), gunIconAngle_);

    gunFrame_->SetColor({ 0.08f, 0.08f, 0.15f, 0.85f });
    gunFrame_->Update();

    // 3Dモデルで表示中のスロットは、下地の色四角を隠して実物モデルだけ見せる
    for (int i = 0; i < kWeaponSlotCount; ++i) {
        const int weaponIndex = wm->GetSlotWeaponIndex(i);
        const bool unlocked = weaponIndex >= 0 && weaponIndex < static_cast<int>(wm->GetList().size())
            && wm->IsUnlocked(weaponIndex);
        const bool show3DIcon = unlocked && weaponIndex == i
            && (weaponIcons3D_[i].slotIndex == i) && weaponIcons3D_[i].object;
        if (show3DIcon) {
            Vector4 c = weaponSlots_[i].icon->GetColor();
            weaponSlots_[i].icon->SetColor({ c.x, c.y, c.z, 0.0f });
            weaponSlots_[i].icon->Update();
        }
    }

    // ── 各スロットの3Dアイコン（画面左下に固定表示、ゆっくり回転） ────────
    // カメラは回転しないので、スロットの画面位置をワールド座標へ逆算して張り付ける
    // カメラのすぐ手前(奥行き6)に置き、WorldToScreenの基準距離(24)に対する比率でオフセット/スケールを縮小する
    constexpr float kSlotSize = 56.0f;
    constexpr float kIconDepth = 6.0f; // カメラからの距離
    constexpr float kIconDepthScale = kIconDepth / 24.0f; // WorldToScreen基準距離(24)との比
    for (int i = 0; i < kWeaponSlotCount; ++i) {
        auto& icon3d = weaponIcons3D_[i];
        if (icon3d.slotIndex != i || !icon3d.object) {
            continue;
        }
        if (wm->GetSlotWeaponIndex(i) != i || !wm->IsUnlocked(i)) {
            continue;
        }

        float sx = weaponSlotPos_[i].x + kSlotSize * 0.5f;
        float sy = weaponSlotPos_[i].y + kSlotSize * 0.5f;
        const Vector3& cam = camera_->GetTranslate();
        Vector3 iconPos = {
            cam.x + (sx - GameConstants::kScreenCenterX) / GameConstants::kScreenCenterX * GameConstants::kCameraHalfW * kIconDepthScale,
            cam.y - (sy - GameConstants::kScreenCenterY) / GameConstants::kScreenCenterY * GameConstants::kCameraHalfH * kIconDepthScale,
            cam.z + kIconDepth
        };
        float iconScale = icon3d.scale * kIconDepthScale;
        // フルスピンだと必ず背面がカメラを向く瞬間が来て武器が判別できなくなるため、正面(baseYaw)を中心に小さく揺らすだけにする
        icon3d.wobbleTime += GameConstants::kFrameDeltaTime;
        float yaw = icon3d.baseYaw + std::sin(icon3d.wobbleTime * 0.8f) * 0.35f;
        icon3d.object->SetPosition(iconPos);
        icon3d.object->SetRotation({ 0.3f, yaw, 0.0f });
        icon3d.object->SetScale({ iconScale, iconScale, iconScale });
        const bool active = (i == activeIndex);
        const float b = active ? (0.9f + pulse * 0.1f) : 0.6f;
        icon3d.object->SetColor({ b, b, b, 1.0f });
        icon3d.object->Update();
    }
}

void GamePlayScene::DrawWeaponSlotHud()
{
    constexpr float kSlotSize = 56.0f;

    // 背景の枠を先に描く（3Dモデルがこの手前に来るようにする）
    SceneShared::DrawWeaponSlotFrames(weaponSlots_.data(), kWeaponSlotCount, gunFrame_.get());

    // 枠の中身 実物3Dモデルのスロットは、いったん3D描画パイプラインに切り替えて
    // 枠より手前に描画する（2Dの枠と同じパスで描くと後から描かれる枠に隠れてしまう）
    {
        ID3D12GraphicsCommandList* cmd = dxCommon_->GetCommandList();
        modelCommon_->CommonDrawSettings();
        Object3d::RebindCommonLighting(cmd);
        auto* wm = WeaponManager::GetInstance();
        for (int i = 0; i < kWeaponSlotCount; ++i) {
            auto& icon3d = weaponIcons3D_[i];
            if (wm->GetSlotWeaponIndex(i) == i
                && icon3d.slotIndex == i && icon3d.object && wm->IsUnlocked(i)) {
                icon3d.object->Draw();
            }
        }
        spriteCommon_->CommonDrawSettings(); // 以降のスプライト描画のため2Dへ戻す
    }

    // 色四角のアイコン（3Dモデル未対応のスタイル用。3Dモデル表示中のスロットはアルファ0で透明）
    SceneShared::DrawWeaponSlotIconsAndLabels(weaponSlots_.data(), kWeaponSlotCount, weaponSlotPos_.data(),
        gunIcon_.get(), gunPos_, WeaponManager::GetInstance(), fontRenderer_, kSlotSize);
}

void GamePlayScene::UpdateWeaponExchange()
{
    auto* wm = WeaponManager::GetInstance();
    if (!wm->HasPendingWeapon()) {
        return;
    }

    for (int slot = 0; slot < 4; ++slot) {
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

    fontRenderer_.DrawStringW(
        L"武器スロットが満杯です", 360.0f, 220.0f, 2.0f,
        { 1.0f, 0.85f, 0.2f, 1.0f });
    fontRenderer_.DrawStringW(
        L"入手武器  " + StringUtility::ConvertString(wm->GetPendingWeapon().name),
        410.0f, 270.0f, 1.6f, { 0.8f, 0.95f, 1.0f, 1.0f });
    fontRenderer_.DrawStringW(
        L"1から4で交換するスロットを選択  Backspaceで破棄",
        245.0f, 330.0f, 1.35f, { 1.0f, 1.0f, 1.0f, 1.0f });
}

void GamePlayScene::DrawStageGuide()
{
    const float x = player_->GetPosition().x;
    constexpr float kScale = 1.35f;
    constexpr Vector4 kGuideColor = { 0.85f, 0.95f, 1.0f, 0.95f };

    const std::wstring coreCount = L"エネルギーコア  "
        + std::to_wstring(collectedEnergyCores_) + L" / "
        + std::to_wstring(energyCores_.size());
    fontRenderer_.DrawStringW(coreCount, 24.0f, 540.0f, kScale,
        { 0.3f, 0.9f, 1.0f, 1.0f });

    if (x < 11.0f) {
        fontRenderer_.DrawStringW(
            L"訓練区画  移動 A D  ジャンプ W  攻撃 L",
            24.0f, 575.0f, kScale, kGuideColor);
    } else if (x < 17.0f) {
        fontRenderer_.DrawStringW(
            L"剣を持つ敵を倒し  Jで武器を奪って強化しよう",
            24.0f, 575.0f, kScale, kGuideColor);
    } else if (x < 24.0f) {
        fontRenderer_.DrawStringW(
            L"槍を持つ敵を倒し  Jで武器を奪って強化しよう",
            24.0f, 575.0f, kScale, kGuideColor);
    } else if (x < 31.5f) {
        fontRenderer_.DrawStringW(
            L"短剣を持つ敵を倒し  Jで武器を奪って強化しよう",
            24.0f, 575.0f, kScale, kGuideColor);
    } else if (!enemy_->IsDefeated()) {
        fontRenderer_.DrawStringW(
            L"戦闘区画  技を変えてスタイルランクを上げる",
            24.0f, 575.0f, kScale, { 1.0f, 0.75f, 0.25f, 1.0f });
    } else {
        fontRenderer_.DrawStringW(
            L"撃破完了  敵の武器を奪って次の区画へ進む",
            24.0f, 575.0f, kScale, { 0.45f, 1.0f, 0.65f, 1.0f });
    }
}

void GamePlayScene::DrawWeaponListPanel()
{
    // BattleTestScene::DrawWeaponHud()と同じ体裁左上アンカーに武器スロット一覧＋操作ヒント
    constexpr float kScale = 1.5f;
    constexpr Vector4 kColorHint = { 0.80f, 0.76f, 0.65f, 1.0f };
    constexpr Vector4 kShadow = { 0.05f, 0.04f, 0.02f, 0.9f };
    constexpr float kShadowOffset = 1.6f;
    auto drawShadowedHint = [&](const std::wstring& text, float x, float y) {
        fontRenderer_.DrawStringW(text, x + kShadowOffset, y + kShadowOffset, kScale, kShadow);
        fontRenderer_.DrawStringW(text, x, y, kScale, kColorHint);
    };

    const Vector2 weaponHudAnchor = GetStageEditor().GetHudAnchorPosition("hud_anchor_weapon_list", { 12.0f, 12.0f });
    float py = SceneShared::DrawWeaponListHud(fontRenderer_, WeaponManager::GetInstance(),
        L"メインステージ", weaponHudAnchor);
    drawShadowedHint(L"[L] コンボ  [S+L] 打ち上げ  [空中L] 空中コンボ", weaponHudAnchor.x, py);
    drawShadowedHint(L"[K] 射撃  [R] 覚醒  [Shift長押し] ロックオン（最寄りの敵）", weaponHudAnchor.x, py + 24.0f);
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
