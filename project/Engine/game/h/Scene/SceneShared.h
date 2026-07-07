/**
 * @file SceneShared.h
 * @brief BattleTestScene / TrainingScene 間で共通の武器切替・カメラ追従・HUD描画処理
 */
#pragma once
#include "MakeAffine.h"
#include "PostEffectFullscreenPass.h" // IPostEffectSource
#include "WeaponManager.h"
#include <memory>
#include <string>
namespace engine { class DirectXCommon; class Input; }
namespace engine::graphics { class Camera; class ParticleManager; class Sprite; class SpriteCommon; }

namespace engine::game {
class FontRenderer;

namespace SceneShared {

/// @brief 大技（フィニッシャースラッシュ）演出中の画面暗転オーバーレイスプライトを生成する
std::unique_ptr<engine::graphics::Sprite> CreateFinisherOverlay(engine::graphics::SpriteCommon* spriteCommon);

/// @brief JSONファイル（[{name, texture, additive}, ...]）に従ってパーティクルグループを一括登録する
void CreateParticleGroupsFromJson(engine::graphics::ParticleManager* pm, const std::string& jsonPath);

/// @brief 有効なポストエフェクトに応じたオフスクリーンRTV（未使用時はバックバッファ）を返す
D3D12_CPU_DESCRIPTOR_HANDLE GetActiveRTVHandle(engine::DirectXCommon* dxCommon,
    std::initializer_list<engine::graphics::IPostEffectSource*> effects);

/// @brief メイン描画先（GetActiveRTVHandle）とビューポート/シザーを設定する
void SetupMainRenderTarget(engine::DirectXCommon* dxCommon,
    std::initializer_list<engine::graphics::IPostEffectSource*> effects);

/// @brief フィニッシャー発動時の溜めエフェクト（周囲から中心へ収束する光粒＋小リング）を放出する
void EmitFinisherCharge(engine::graphics::ParticleManager* pm,
    const std::string& ringGroup, const std::string& sparkGroup, const Vector3& pos);

/// @brief 斬撃線1本ごとの煌めき（斬線に沿った光粒＋中心の閃光）を放出する
/// @param slashGroup 斬撃の残光グループ名空文字列なら残光はスキップする
void EmitFinisherSlashLine(engine::graphics::ParticleManager* pm,
    const std::string& slashGroup, const std::string& sparkGroup,
    const Vector3& center, float angle, float halfLength);

/// @brief 解放の瞬間の炸裂エフェクト（多重リング＋放射火花＋上昇する余韻）を放出する
void EmitFinisherRelease(engine::graphics::ParticleManager* pm,
    const std::string& ringGroup, const std::string& sparkGroup, const Vector3& pos);

/// @brief ワールド座標の2点をスクリーン座標へ変換して斬撃線をスポーンする
/// @param thickness 線の太さ（ピクセル）
void SpawnSlashMarkWorld(const Vector2& start, const Vector2& end, float camX, float camY,
    const Vector4& color, float thickness, float duration);

/// @brief 武器切替入力（Q/E、数字キー）を処理するweaponCycleTimer は呼び出し側が保持するクールダウン
void UpdateWeaponCycle(engine::Input* input, WeaponManager* weaponManager, float& weaponCycleTimer);

/// @brief ワールド座標をスクリーン座標に変換する（カメラ位置基準）
void WorldToScreen(float worldX, float worldY, float camX, float camY, float& outX, float& outY);

/// @brief プレイヤーにカメラを追従させる（ステージ境界クランプ込み）
void UpdateCameraFollow(engine::graphics::Camera* camera, const Vector3& playerPos);

/// @brief 近接判定 + ENTER キーでのシーン遷移を行うポータル処理近接中なら true を返す
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
