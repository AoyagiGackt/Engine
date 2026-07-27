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

namespace {
/** @brief コンボランク表示1段ぶんの定義（styleMeter_のしきい値とラベル・色） */
struct StyleRankDef {
    const char* label;
    float threshold; ///< styleMeter_(0.0〜1.0)がこの値以上でこのランクになる
    Vector4 color;
};
// 低い方から並べる。DrawRankAndAwakenGauge()がしきい値以下の最高ランクを検索する
// しきい値本体はGameConstants::kStyleRankThresholds（ランクアップ演出側と共通）を参照する
constexpr StyleRankDef kStyleRanks[] = {
    { "D", GameConstants::kStyleRankThresholds[0], { 0.45f, 0.45f, 0.45f, 1.0f } },
    { "C", GameConstants::kStyleRankThresholds[1], { 0.85f, 0.85f, 0.85f, 1.0f } },
    { "B", GameConstants::kStyleRankThresholds[2], { 0.85f, 0.85f, 0.20f, 1.0f } },
    { "A", GameConstants::kStyleRankThresholds[3], { 0.95f, 0.55f, 0.15f, 1.0f } },
    { "S", GameConstants::kStyleRankThresholds[4], { 0.20f, 0.85f, 1.00f, 1.0f } },
    { "SS", GameConstants::kStyleRankThresholds[5], { 1.00f, 0.85f, 0.00f, 1.0f } },
    { "SSS", GameConstants::kStyleRankThresholds[6], { 1.00f, 0.15f, 0.15f, 1.0f } },
};
constexpr float kRankHudRightEdge = 1260.0f; ///< ランク文字を右揃えする画面X座標
} // namespace

void GamePlayScene::DrawOverlaysAndUI()
{
    spriteCommon_->CommonDrawSettings();

    awakenGaugeBg_->Draw();
    if (player_->GetAwakenGauge() > 0.0f) {
        awakenGaugeFg_->Draw();
    }
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

    // プレイヤーHP + ゴールド（左上）
    std::string info = "HP:" + std::to_string(rd->GetHp()) + "/" + std::to_string(rd->GetMaxHp())
        + "  G:" + std::to_string(rd->GetGold());
    fontRenderer_.DrawString(info.c_str(), 10.0f, 10.0f, 1.5f, { 0.3f, 1.0f, 0.4f, 1.0f });
}

void GamePlayScene::DrawStyleUI()
{
    fontRenderer_.Reset();

    DrawStageGuide();
    DrawRogueliteHUD();
    SceneShared::DrawControlsHud(fontRenderer_, L": ステージを進む");
    SceneShared::DrawAwakenGaugeHud(fontRenderer_, awakenGaugeBg_.get(), awakenGaugeFg_.get(),
        player_->GetAwakenGauge(), player_->IsAwakened(), auraTimer_);
    if (enemy_->IsDefeated() && !weaponStealTriggered_) {
        fontRenderer_.DrawStringW(L"[ J ] 敵の武器を吸収", 500.0f, 500.0f, 1.6f,
            { 0.55f, 0.9f, 1.0f, 1.0f });
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
}

void GamePlayScene::UpdateWeaponSlotHud()
{
    weaponSlotPulse_ += GameConstants::kFrameDeltaTime;
    gunIconAngle_ += GameConstants::kFrameDeltaTime * 0.6f;

    SceneShared::UpdateWeaponSlotHud(WeaponManager::GetInstance(), weaponSlots_.data(), kWeaponSlotCount,
        weaponSlotPulse_, /*flash=*/0.0f, gunIcon_.get(), gunIconAngle_);

    gunFrame_->SetColor({ 0.08f, 0.08f, 0.15f, 0.85f });
    gunFrame_->Update();
}

void GamePlayScene::DrawWeaponSlotHud()
{
    constexpr float kSlotSize = 56.0f;
    SceneShared::DrawWeaponSlotFrames(weaponSlots_.data(), kWeaponSlotCount, gunFrame_.get());
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
        L"入手武器  " + wm->GetPendingWeapon().styleNameJp,
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
    } else if (swordGateActive_ && x < 19.0f) {
        fontRenderer_.DrawStringW(
            L"剣敵を倒して装備し  SPACEの瞬迅斬りで赤い障壁を壊す",
            24.0f, 575.0f, kScale, kGuideColor);
    } else if (spearGateActive_ && x < 27.0f) {
        fontRenderer_.DrawStringW(
            L"槍敵を倒して装備し  SPACEの間合い外しで青い障壁を解く",
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

void GamePlayScene::DrawRankAndAwakenGauge()
{
    // ══════════════════════════════════════════════════════
    // 右上 コンボランク ＋ 覚醒ゲージ
    // ══════════════════════════════════════════════════════
    constexpr float kScale = 1.5f;
    constexpr float kLineH = FontRenderer::kCharH * kScale + 4.0f;

    const float gauge = player_->GetAwakenGauge();
    const bool awakened = player_->IsAwakened();

    // ランク算出（しきい値表を下から検索し、条件を満たす最高ランクを採用する）
    const StyleRankDef* rankDef = &kStyleRanks[0];
    for (const auto& def : kStyleRanks) {
        if (styleMeter_ >= def.threshold) {
            rankDef = &def;
        }
    }

    // ランク文字（大きく右揃え）
    constexpr float kRankScale = 4.0f;
    int rankLen = static_cast<int>(strlen(rankDef->label));
    float rankX = kRankHudRightEdge - rankLen * FontRenderer::kCharW * kRankScale;
    fontRenderer_.DrawString(rankDef->label, rankX, 20.0f, kRankScale, rankDef->color);

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
        Vector4 col = maxed ? Vector4 { 1.0f, 0.95f, 0.3f, 1.0f }
            : ready         ? Vector4 { 0.85f, 0.5f, 1.0f, 1.0f }
                            : Vector4 { 0.45f, 0.45f, 0.45f, 1.0f };
        const wchar_t* label = maxed ? L"覚醒ゲージ 満タン！[F]で発動"
            : ready                  ? L"覚醒ゲージ [R]で発動"
                                     : L"覚醒ゲージ";
        fontRenderer_.DrawStringW(label, 1030.0f, gy, kScale, col);
    }
    gy += kLineH;

    {
        int filled = std::clamp(static_cast<int>(gauge * 16.0f), 0, 16);
        std::string bar = "[";
        for (int i = 0; i < 16; ++i) {
            bar += (i < filled ? '#' : ' ');
        }
        bar += "] ";
        bar += std::to_string(static_cast<int>(gauge * 100.0f)) + "%";
        Vector4 col = awakened ? Vector4 { 1.0f, 0.85f, 0.0f, 1.0f }
                               : Vector4 { 0.55f, 0.15f, 0.9f, 1.0f };
        fontRenderer_.DrawString(bar, 1030.0f, gy, kScale, col);
    }
}

void GamePlayScene::DrawStyleCommands()
{
    // ══════════════════════════════════════════════════════
    // 右側 スタイルコマンド UI
    // ══════════════════════════════════════════════════════
    constexpr float kScale = 1.5f;
    constexpr float kLineH = FontRenderer::kCharH * kScale + 4.0f;

    auto* wm = WeaponManager::GetInstance();
    if (!wm->HasEquippedWeapon()) {
        fontRenderer_.DrawStringW(L"武器なし  敵を倒して武器を奪え", 780.0f, 448.0f, 1.5f,
            { 0.85f, 0.85f, 0.9f, 1.0f });
        return;
    }
    const WeaponData& style = wm->GetCurrent();
    const int selectedSlot = wm->GetSelectedSlot();
    const int combo = player_->GetComboStep();

    constexpr float kX = 780.0f;
    float y = 448.0f;

    // スタイルインジケーター [1][2][3][4]
    float bx = kX;
    for (int i = 0; i < 4; ++i) {
        const int weaponIndex = wm->GetSlotWeaponIndex(i);
        Vector4 col = { 0.28f, 0.28f, 0.28f, 1.0f };
        if (weaponIndex >= 0) {
            const auto& w = wm->GetList()[weaponIndex];
            col = (i == selectedSlot)
                ? Vector4 { w.styleColor[0], w.styleColor[1], w.styleColor[2], w.styleColor[3] }
                : Vector4 { 0.55f, 0.55f, 0.55f, 1.0f };
        }
        std::string btn = "[" + std::to_string(i + 1) + "]";
        fontRenderer_.DrawString(btn, bx, y, kScale, col);
        bx += static_cast<float>(btn.size()) * FontRenderer::kCharW * kScale + 2.0f;
    }
    y += kLineH;

    // スタイル名（日本語）
    Vector4 styleCol { style.styleColor[0], style.styleColor[1],
        style.styleColor[2], style.styleColor[3] };
    fontRenderer_.DrawStringW(style.styleNameJp, kX, y, kScale, styleCol);
    y += kLineH;

    fontRenderer_.DrawString("--------------------", kX, y, kScale,
        { 0.35f, 0.35f, 0.35f, 1.0f });
    y += kLineH;

    // コマンド一覧（キーは基本ASCIIだが空中L等の日本語混じりもあるためUTF-8として変換する）
    for (const auto& cmd : style.commands) {
        std::wstring line = L"[" + StringUtility::ConvertString(cmd.key) + L"] " + cmd.desc;
        fontRenderer_.DrawStringW(line, kX, y, kScale, { 0.85f, 0.85f, 0.85f, 1.0f });
        y += kLineH;
    }

    // 選択中の銃（Gキー切替、Kキーで銃種別のコンボ）
    const RangedWeaponData& gun = wm->GetRanged();
    Vector4 gunCol { gun.color[0], gun.color[1], gun.color[2], gun.color[3] };
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
    if (clearTriggered_) {
        return;
    }
    glassShatterDebugTest_ = true;
    clearTriggered_ = true;
    glassShatter_.Start();
}

void GamePlayScene::Finalize()
{
    if (enemy_) {
        EnemyRegistry::GetInstance()->Unregister(enemy_->GetId());
    }
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
