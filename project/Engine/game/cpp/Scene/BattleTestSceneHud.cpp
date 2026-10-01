/**
 * @file BattleTestSceneHud.cpp
 * @brief BattleTestSceneの武器スロットHUD（アイコン切替演出・描画）を実装するファイル
 * @note BattleTestScene.cppからの分割ファイルクラス自体はBattleTestSceneのまま、定義の置き場所だけを分けている
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
using namespace engine;
using namespace engine::graphics;
using namespace engine::game;

void BattleTestScene::InitializeWeaponSlotHud()
{
    hud_.Add(std::make_unique<AwakenGaugeHud>());
    hud_.Add(std::make_unique<WeaponSlotHud>());
    hud_.Initialize({ *spriteCommon_, *modelCommon_, *dxCommon_, *weaponManager_ });
    UpdateWeaponSlotHud();
}

void BattleTestScene::UpdateWeaponSlotHud()
{
    HudFrame frame { *camera_, GameConstants::kFrameDeltaTime, player_->GetAwakenGauge(), player_->IsAwakened(), warpPulseTimer_ };
    hud_.Update(frame);
}

void BattleTestScene::DrawWeaponSlotHud()
{
    hud_.Draw();
}
