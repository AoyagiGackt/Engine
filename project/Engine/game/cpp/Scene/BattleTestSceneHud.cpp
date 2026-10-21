/**
 * @file BattleTestSceneHud.cpp
 * @brief BattleTestSceneの武器スロットHUD（アイコン切替演出・描画）を実装するファイル
 * @note BattleTestScene の武器スロットHUDだけを BattleTestScene.cpp から分けて置いている（クラスは BattleTestScene のまま）
 */
#include "BattleTestScene.h"
#include "WeaponSlotHud.h"
#include "AwakenGaugeHud.h"
#include "AudioBridge.h"
#include "BattleTestSceneRenderer.h"
#include "Collision.h"
#include "DiagnosticsDraw.h"
#include "GameConstants.h"
#include "GrayscaleEffect.h"
#include "HsvFilter.h"
#include "ImGuiControl.h"
#include "JsonHelper.h"
#include "ModelManager.h"
#include "PipelineStateGuard.h"
#include "PlayerBridge.h"
#include "PostEffectRenderTarget.h"
#include "SceneManager.h"
#include "ScreenFlash.h"
#include "SlashMark.h"
#include "StageEditor.h"
#include "TimeManager.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>
using namespace engine::graphics;

namespace engine::game {
/** @brief 武器スロットUIのスプライトを初期化する */
void BattleTestScene::InitializeWeaponSlotHud()
{
    hud_.Add(std::make_unique<AwakenGaugeHud>());
    hud_.Add(std::make_unique<WeaponSlotHud>());
    hud_.Initialize({ *spriteCommon_, *modelCommon_, *dxCommon_, *weaponManager_ });
    UpdateWeaponSlotHud();
}

/** @brief 武器スロットUI（枠・アイコン・光る演出）を毎フレーム更新する */
void BattleTestScene::UpdateWeaponSlotHud()
{
    HudFrame frame { *camera_, GameConstants::kFrameDeltaTime, player_->GetAwakenGauge(), player_->IsAwakened(), warpPulseTimer_ };
    hud_.Update(frame);
}

/** @brief 武器スロットUIを描画する */
void BattleTestScene::DrawWeaponSlotHud()
{
    hud_.Draw();
}

} // namespace engine::game
