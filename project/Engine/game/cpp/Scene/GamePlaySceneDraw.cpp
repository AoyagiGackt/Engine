/**
 * @file GamePlaySceneDraw.cpp
 * @brief GamePlaySceneのフィニッシャー演出・クリア判定・メイン描画パスを実装するファイル
 * @note GamePlayScene の演出と描画だけを GamePlayScene.cpp から分けて置いている（クラスは GamePlayScene のまま）
 */
#include "GamePlayScene.h"
#include "HitEffectLibrary.h"
#include "TextBufferCapacity.h"
#include "AudioBridge.h"
#include "CombatTuning.h"
#include "GameConstants.h"
#include "GameFlags.h"
#include "GameRules.h"
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
#include "ScreenFlash.h"
#include "SlashMark.h"
#include "StageEditor.h"
#include "StringUtility.h"
#include "TextureManager.h"
#include "UILayout.h"
#include "FinisherEffects.h"
#include "WeaponManager.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <random>
using namespace engine::graphics;

namespace engine::game {
namespace {
// ボスの武器吸収
constexpr float kMinAbsorbDirectionLength = 0.001f;

// クリア結果画面
constexpr Vector2 kClearTitlePosition = { 490.0f, 200.0f };
constexpr float kClearTitleScale = 4.0f;
constexpr Vector4 kClearTitleColor = { 1.0f, 1.0f, 0.3f, 1.0f };
constexpr Vector2 kStyleLabelPosition = { 420.0f, 310.0f };
constexpr float kStyleLabelScale = 3.0f;
constexpr Vector4 kStyleLabelColor = { 0.8f, 0.8f, 0.8f, 1.0f };
constexpr Vector2 kRankPosition = { 580.0f, 305.0f };
constexpr float kRankScale = 4.0f;
constexpr Vector4 kRankColor = { 1.0f, 0.5f, 0.1f, 1.0f };
constexpr Vector2 kGoldPosition = { 540.0f, 400.0f };
constexpr float kGoldScale = 3.0f;
constexpr Vector4 kGoldColor = { 0.9f, 0.85f, 0.2f, 1.0f };
constexpr Vector2 kScorePosition = { 500.0f, 470.0f };
constexpr float kScoreScale = 2.5f;
constexpr Vector4 kScoreColor = { 0.85f, 0.95f, 1.0f, 1.0f };

}

/** @brief 斬撃を一定間隔で重ね、最終段で敵の破片と画面効果を解放する */
void GamePlayScene::UpdateFinisherSlash(float dt)
{
    const auto& visuals = HitEffectLibrary::GetInstance()->SceneParticles();
    if (!finisherActive_)
    {
        return;
    }

    finisherBeatTimer_ -= dt;
    if (finisherBeatTimer_ > 0.0f)
    {
        return;
    }

    auto* tm = TimeManager::GetInstance();
    const Vector3& epos = enemy_->GetPosition();

    if (finisherLineIdx_ < CombatTune().finisherSlashLines)
    {
        // カメラ視界全体にランダムな位置を高速で斬り刻む
        const FinisherEffects::BeatSlash slash = FinisherEffects::SpawnBeatSlash(rng_, camera_->GetTranslate(), finisherLineIdx_);

        // 1本ごとに実際にヒットさせ、敵を空中に拘束し続ける
        enemy_->TakeDamage(CombatTune().finisherLineDamage);
        enemy_->Launch(CombatTune().finisherBeatLaunchSpeed);
        styleMeter_ = std::clamp(styleMeter_ + CombatTune().finisherBeatStyleGain, 0.0f, 1.0f);

        tm->RequestHitStop(HitEffectLibrary::GetInstance()->SceneParticles().hitStopFinisherBeat);
        cameraShaker_.Request(visuals.beatShakeAmount, visuals.beatShakeSeconds);
        SceneShared::EmitFinisherSlashLine(pm_, "sword_slash", "hit_spark",
            { slash.center.x, slash.center.y, 0.0f }, slash.angle, slash.length);

        // 空間にガラス質の刃を明滅させ、歪みを脈動させる
        FinisherEffects::EmitBeatBlades(bladeFlash_, { slash.center.x, slash.center.y, epos.z });
        spaceWarp_.AddImpulse(visuals.beatWarpImpulse);

        finisherLineIdx_++;
        finisherBeatTimer_ = CombatTune().FinisherBeatDelayAfter(finisherLineIdx_);
        return;
    }

    // 解放 溜めた斬撃が一斉に炸裂する
    finisherActive_ = false;
    tm->RequestHitStop(HitEffectLibrary::GetInstance()->SceneParticles().hitStopFinisherSlash);
    cameraShaker_.Request(HitEffectLibrary::GetInstance()->SceneParticles().shakeFinisherSlashAmt, HitEffectLibrary::GetInstance()->SceneParticles().shakeFinisherSlashDur);
    styleMeter_ = std::clamp(styleMeter_ + CombatTune().finisherReleaseStyleGain, 0.0f, 1.0f);
    enemy_->TakeDamage(CombatTune().finisherSlashDamage);
    enemy_->Launch(CombatTune().launchSpeed);

    // 敵本体を切断破片に差し替える（演出が飛散に移るまで本体は非表示）
    enemySlice_.Start(enemy_->GetModel(), epos, { 1.0f, 1.0f, 1.0f }, rng_());
    enemy_->SetVisible(false);

    // 解放の瞬間 刃の一斉放出と空間歪みの最大化
    FinisherEffects::EmitReleaseBlades(bladeFlash_, spaceWarp_, epos);

    // 白閃光とともに暗転+斬撃線ごと凍った画面を敵位置から砕き、素の世界を見せる
    ScreenFlash::GetInstance()->Request(visuals.releaseFlashColor, visuals.releaseFlashSeconds);
    FinisherEffects::StartShatterAt(finisherShatter_, *camera_, epos);

    // 溜めた斬撃線を一斉に白く光らせてから消し、太く短い閃光の斬撃線を重ねる
    FinisherEffects::SpawnReleaseSlashes(rng_, epos, camera_->GetTranslate());

    SceneShared::EmitFinisherRelease(pm_, "hit_ring", "hit_spark", epos);
}

/** @brief ステージのクリア条件を満たしたかを調べる */
void GamePlayScene::CheckClearCondition()
{
    const auto& visuals = HitEffectLibrary::GetInstance()->SceneParticles();
    const GameRulesData& rules = GameRules::GetInstance()->Get();

    // 最終敵の撃破後にgame_rules.jsonで指定した武器を奪い、スロットを完成させる
    if (!weaponStealTriggered_ && enemy_->IsDefeated()
        && !finisherActive_ && !enemySlice_.IsActive())
    {
        // 打ち飛ばされて武器を落としていれば、本体ではなく落ちた武器の場所で奪う
        const Vector3 epos = enemy_->GetWeaponPickupPosition();
        const Vector3& ppos = player_->GetPosition();
        const float dx = ppos.x - epos.x;
        const float dy = ppos.y - epos.y;
        if (!mainWeaponAbsorbing_ && dx * dx + dy * dy <= CombatTune().weaponAbsorbRange * CombatTune().weaponAbsorbRange
            && input_->TriggerAction(Input::Action::Steal))
        {
            mainWeaponAbsorbing_ = true;
            mainWeaponAbsorbTimer_ = CombatTune().weaponAbsorbDuration;
            player_->PlayStealStab();
            Vector3 toPlayer = { ppos.x - epos.x, ppos.y + visuals.absorbTargetHeight - epos.y, 0.0f };
            float len = std::sqrt(toPlayer.x * toPlayer.x + toPlayer.y * toPlayer.y);
            if (len > kMinAbsorbDirectionLength)
            {
                toPlayer.x /= len;
                toPlayer.y /= len;
            }
            for (int i = 0; i < visuals.absorbOrbCount; ++i)
            {
                float speed = visuals.absorbOrbSpeedBase + static_cast<float>(i) * visuals.absorbOrbSpeedStep;
                pm_->EmitGravity("weapon_orb",
                    { epos.x, epos.y + visuals.absorbTargetHeight, 0.0f },
                    { toPlayer.x * speed, toPlayer.y * speed, 0.0f },
                    visuals.absorbGlowColor, visuals.absorbOrbLifetime, visuals.absorbOrbSize);
            }
        }
        if (mainWeaponAbsorbing_)
        {
            mainWeaponAbsorbTimer_ -= GameConstants::kFrameDeltaTime;
            const Vector3 absorbTarget = { ppos.x, ppos.y + visuals.absorbTargetHeight, ppos.z };
            if (enemy_->HasDroppedWeapon())
            {
                enemy_->PullDroppedWeaponToward(absorbTarget, visuals.absorbPullRate);
            }
            else
            {
                enemy_->MoveTowardXY(absorbTarget, visuals.absorbPullRate);
                enemy_->RefreshVisualTransforms();
            }
            if (mainWeaponAbsorbTimer_ <= 0.0f)
            {
                weaponStealTriggered_ = true;
                enemy_->SetVisible(false);
                // ボスの配置物に設定された武器を奪う（未設定ならgame_rules.jsonの既定）。あわせてボス技を習得する
                const WeaponManager::AcquireResult acquired = WeaponManager::GetInstance()->Acquire(bossWeaponType_);
                ApplyWeaponAcquireResult(acquired);
                // ボスの武器は次のステージの専用の壁を壊す鍵になるため、満杯でも破棄させず必ずどれかと交換させる
                pendingWeaponMandatory_ = acquired == WeaponManager::AcquireResult::NeedsReplacement;
                RunData::GetInstance()->AddBossTechnique(rules.bossTechnique);
            }
        }
    }

    // ローグライト: 敵撃破でクリア（大技・切断演出は見せ切ってから遷移する）
    if (rules.requireBossWeaponSteal && !clearTriggered_ && enemy_->IsDefeated() && weaponStealTriggered_
        && !finisherActive_ && !enemySlice_.IsActive()
        /** @brief RunData の唯一のインスタンスを取得する */
        && RunData::GetInstance()->IsRunActive())
    {
        requestClear_ = true;
    }

    // ノードグラフ（RequestStageClear/SetFlag）やステージのトリガーがクリアフラグを立てた場合もクリアへ進む
    if (!clearTriggered_ && !rules.clearFlag.empty() && GameFlags::GetInstance()->GetFlag(rules.clearFlag)
        && !finisherActive_ && !enemySlice_.IsActive())
    {
        GameFlags::GetInstance()->SetFlag(rules.clearFlag, false); // 次のステージへ持ち越さない
        requestClear_ = true;
    }

    if (requestClear_)
    {
        requestClear_ = false;
        clearTriggered_ = true;
        if (!RunData::GetInstance()->IsRunActive())
        {
            glassShatter_.Start();
        }
    }
}

// ══════════════════════════════════════════════════════
// レンダリング
// ══════════════════════════════════════════════════════

/** @brief 有効なポストエフェクトに応じて、いまの描画先 RTV のハンドルを返す */
D3D12_CPU_DESCRIPTOR_HANDLE GamePlayScene::GetActiveRTVHandle() const
{
    return SceneShared::GetActiveRTVHandle(dxCommon_, { imageFilter_, grayscaleEffect_, hsvFilter_ });
}

/** @brief いまの描画先がバックバッファか（ポストエフェクトを使わずに直接画面へ描いているか） */
bool GamePlayScene::IsDrawingToBackBuffer() const
{
    return GetActiveRTVHandle().ptr == dxCommon_->GetCurrentBackBufferHandle().ptr;
}

/** @brief シーンの描画先・深度バッファ・ビューポート・シザーを設定し直す */
void GamePlayScene::SetupMainRenderTarget()
{
    SceneShared::SetupMainRenderTarget(dxCommon_, { imageFilter_, grayscaleEffect_, hsvFilter_ });
}

/** @brief モデル用のパイプラインとライトの設定を戻す */
void GamePlayScene::SetupModelRenderState()
{
    modelCommon_->CommonDrawSettings();
    objectCommon_->SetDefaultLight(dxCommon_->GetCommandList());
    shadowManager_->SetShadowMap(dxCommon_->GetCommandList(), srvManager_);
}

/** @brief 影用の深度を描く */
void GamePlayScene::DrawShadowPass()
{
    shadowManager_->RunShadowPass(dxCommon_->GetCommandList(), modelCommon_.get());
}

/** @brief 3Dワールドと画面UIを描画する */
void GamePlayScene::Draw()
{
    if (DrawClearOverlayIfNeeded())
    {
        return;
    }

    DrawWorldAndActors();
    DrawOverlaysAndUI();
}

/** @brief Draw()の下請け クリア演出中の専用画面（ローグライト結果表示／サンドボックスのガラス割れ導入）を描画する。描画してDraw()を打ち切るべきなら true を返す */
bool GamePlayScene::DrawClearOverlayIfNeeded()
{
    // 配置物を描いた上に、画面全体を指定色の板で覆う
    auto drawClearBackground = [this](const Vector4& color) {
        GetStageEditor().DrawObjects();
        spriteCommon_->CommonDrawSettings();
        clearBgSprite_->SetColor(color);
        clearBgSprite_->Update();
        clearBgSprite_->Draw();
    };
    // クリア演出中（かつキャプチャ済み）はシーン描画をスキップ
    if (clearTriggered_ && RunData::GetInstance()->IsRunActive() && showResult_)
    {
        // ラン中のクリア: 黒い背景に結果（ランク・獲得ゴールド・スコア）を出す
        drawClearBackground({ 0.0f, 0.0f, 0.0f, 1.0f });
        const char* rank = RunData::CalcRank(peakStyle_);
        fontRenderer_.Reset();
        // 本編HUDと同じレイアウト（Resources/Config/UI/gameplay.json）の result グループで調整する
        UILayout& layout = UILayout::Get("gameplay");
        auto drawResultText = [&](const char* text, const char* key, const Vector2& position, float scale, const Vector4& color) {
            const std::string group = std::string("result.") + key;
            const Vector2 p = layout.Pos(group + "_pos", position);
            fontRenderer_.DrawString(text, p.x, p.y, layout.Float(group + "_scale", scale), layout.Color(group + "_color", color));
        };
        drawResultText("CLEAR!", "title", kClearTitlePosition, kClearTitleScale, kClearTitleColor);
        drawResultText("Style:", "style_label", kStyleLabelPosition, kStyleLabelScale, kStyleLabelColor);
        drawResultText(rank, "rank", kRankPosition, kRankScale, kRankColor);
        char goldBuf[TextBufferCapacity::kShortText];
        snprintf(goldBuf, sizeof(goldBuf), "+%dG", lastGold_);
        drawResultText(goldBuf, "gold", kGoldPosition, kGoldScale, kGoldColor);
        char scoreBuf[TextBufferCapacity::kShortText];
        snprintf(scoreBuf, sizeof(scoreBuf), "+%d pts", lastScore_);
        drawResultText(scoreBuf, "score", kScorePosition, kScoreScale, kScoreColor);
        fontRenderer_.Draw();
        return true;
    }
    if (clearTriggered_ && IsGlassShatterFlow() && !glassShatter_.NeedCapture())
    {
        // サンドボックスのクリア: 取り込んだ画面が割れて落ちる演出
        drawClearBackground({ 1.0f, 1.0f, 1.0f, 1.0f });
        glassShatter_.Apply();
        return true;
    }
    return false;
}

/** @brief Draw()の下請け 3Dワールド（地形・残像・プレイヤー/敵・パーティクル・空間歪み）を描画する */
void GamePlayScene::DrawWorldAndActors()
{
    const auto& visuals = HitEffectLibrary::GetInstance()->SceneParticles();
    renderTexture_->BeginRendering();
    renderTexture_->EndRendering();

    DrawShadowPass();
    SetupMainRenderTarget();

    spriteCommon_->CommonDrawSettings();
    renderTextureSprite_->Update();
    renderTextureSprite_->Draw();

    spriteCommon_->CommonDrawSettings();
    if (IsWaterFloor())
    {
        waterPool_->Draw(camera_.get());
    }

    SetupModelRenderState();
    skydome_->Draw();

    SetupModelRenderState();

    // HUDより前にエディタ管理の配置物（収集物・壊せる物を含む）を描画し、BaseScene側の二重描画を抑止する
    GetStageEditor().DrawObjects();
    if (!ghostTrail_.empty())
    {
        SetupModelRenderState();
        ghostObject_->SetModel(player_->GetModel()); // 覚醒フォーム切り替えに残像の見た目を追従させる
        for (const auto& g : ghostTrail_)
        {
            float alpha = (1.0f - g.age / visuals.ghostLifetime) * visuals.ghostMaxAlpha;
            ghostObject_->SetPosition(g.pos);
            ghostObject_->SetColor({ visuals.ghostColor.x, visuals.ghostColor.y, visuals.ghostColor.z, alpha });
            ghostObject_->Update();
            ghostObject_->Draw();
        }
    }

    player_->Draw();
    // weaponEnemies_/enemy_の描画自体はStageEditor所有のためGetStageEditor().DrawObjects()
    // （このすぐ上で呼び済み）が担う。ここで二重に呼ぶとブロックが二重描画されてしまう
    enemySlice_.Draw();

    pm_->Update(camera_.get());
    pm_->Draw(camera_.get());

    bladeFlash_.Draw();
    comboFeel_.DrawTrail();

    // 空間歪み（バックバッファ直描き時のみUIより先に画面をキャプチャして歪ませる）
    if (spaceWarp_.IsActive() && IsDrawingToBackBuffer())
    {
        // スコープを抜けた瞬間に必ずレンダーターゲット設定を戻す（歪み描画がRTを変えるため）
        PipelineStateGuard restoreGuard([this] {
            SetupMainRenderTarget();
        });
        spaceWarp_.CaptureAndApply();
    }
    // 締めのラジアルブラーも同じくバックバッファ直描き時だけ、UIより先に掛ける
    if (comboFeel_.HasScreenEffects() && IsDrawingToBackBuffer())
    {
        PipelineStateGuard restoreGuard([this] {
            SetupMainRenderTarget();
        });
        comboFeel_.ApplyScreenEffects();
    }
}


} // namespace engine::game
