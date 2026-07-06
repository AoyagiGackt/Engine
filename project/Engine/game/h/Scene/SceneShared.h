/**
 * @file SceneShared.h
 * @brief BattleTestScene / TrainingScene 間で共通の武器切替・カメラ追従・HUD描画処理
 */
#pragma once
#include "MakeAffine.h"
#include "WeaponManager.h"
#include <memory>
namespace engine { class Input; }
namespace engine::graphics { class Camera; class Sprite; class SpriteCommon; }

namespace engine::game {
class FontRenderer;

namespace SceneShared {

/// @brief 大技（フィニッシャースラッシュ）演出中の画面暗転オーバーレイスプライトを生成する
std::unique_ptr<engine::graphics::Sprite> CreateFinisherOverlay(engine::graphics::SpriteCommon* spriteCommon);

/// @brief 武器切替入力（Q/E、数字キー）を処理する。weaponCycleTimer は呼び出し側が保持するクールダウン
void UpdateWeaponCycle(engine::Input* input, WeaponManager* weaponManager, float& weaponCycleTimer);

/// @brief ワールド座標をスクリーン座標に変換する（カメラ位置基準）
void WorldToScreen(float worldX, float worldY, float camX, float camY, float& outX, float& outY);

/// @brief プレイヤーにカメラを追従させる（ステージ境界クランプ込み）
void UpdateCameraFollow(engine::graphics::Camera* camera, const Vector3& playerPos);

/// @brief 近接判定 + ENTER キーでのシーン遷移を行うポータル処理。近接中なら true を返す
bool UpdatePortalTransition(engine::Input* input, const Vector3& playerPos,
    float portalX, float proximity, const char* targetSceneName);

/// @brief 武器一覧HUD（ヘッダー・リスト・Q/E切替ヒント）を描画し、次に描画すべきY座標を返す
float DrawWeaponListHud(FontRenderer& fontRenderer, WeaponManager* weaponManager, const wchar_t* headerText);

/// @brief 右側の操作説明パネルを描画する
void DrawControlsHud(FontRenderer& fontRenderer, const wchar_t* portalActionLabel);

/// @brief 覚醒ゲージUIを描画する
void DrawAwakenGaugeHud(FontRenderer& fontRenderer, engine::graphics::Sprite* bgSprite, engine::graphics::Sprite* fgSprite,
    float gauge, bool awakened, float pulseTimer);

} // namespace SceneShared
} // namespace engine::game
