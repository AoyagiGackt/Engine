/**
 * @file SceneShared.h
 * @brief BattleTestScene / TrainingScene 間で共通の武器切替・カメラ追従・HUD描画処理
 */
#pragma once
#include "CollisionConfig.h"
#include "MakeAffine.h"
#include "PostEffectFullscreenPass.h" // IPostEffectSource
#include "WeaponManager.h"
#include <memory>
#include <string>
#include <vector>
namespace engine {
class DirectXCommon;
class Input;
}
namespace engine::graphics {
class Camera;
class ParticleManager;
class Sprite;
class SpriteCommon;
}

namespace engine::game {
class FontRenderer;
class Player;
class BulletPool;

namespace SceneShared {

    /** @brief 武器スロットHUD1枠ぶんのスプライト(枠+色アイコン) */
    struct WeaponSlotUI {
        std::unique_ptr<engine::graphics::Sprite> frame; // 枠背景
        std::unique_ptr<engine::graphics::Sprite> icon; // スタイルカラーで塗った中身
    };

    /** @brief 武器スロットUIの3Dアイコン1個分の素材（モデル・テクスチャ・表示調整値） */
    struct WeaponIconAsset {
        WeaponType type;
        std::string modelPath;
        std::string texturePath;
        float scale; ///< モデル実寸の高さ差を吸収し、見た目のアイコンサイズ(目標高さ約0.8)を揃えるための倍率
        float baseYaw; ///< モデルの正面がカメラを向くよう回す基準角度（ラジアン）。目視で調整した値
    };

    /**
     * @brief 武器スロットUIの3Dアイコン素材一覧をJSONから読み込む
     * @note ダミーの物理武器がまだ無いスタイルはResources/Config/weapon_icons.jsonに1行追記すれば自動でモデル表示に切り替わる
     * @param jsonPath アイコン素材定義のJSONパス
     * @return 読み込んだ素材一覧。ファイルが無い・読み込めない場合は既存互換の既定値を返す
     */
    std::vector<WeaponIconAsset> LoadWeaponIconAssets(const std::string& jsonPath);

    /**
     * @brief 武器スロットHUD(枠+色アイコン+常時装備銃)のスプライトを初期化して配置する
     * @param checkUnlockedForInitialColor 初期色を決める際にロック状態も見るか(未解放スロットを初手から暗く表示したいシーンでtrue)
     */
    void InitializeWeaponSlotHud(engine::graphics::SpriteCommon* spriteCommon, WeaponManager* weaponManager,
        WeaponSlotUI* slots, Vector2* slotPos, int slotCount,
        float slotSize, float slotGap, float marginX, float baseY, bool checkUnlockedForInitialColor,
        std::unique_ptr<engine::graphics::Sprite>& gunFrame, std::unique_ptr<engine::graphics::Sprite>& gunIcon, Vector2& gunPos);

    /**
     * @brief 武器スロットの枠明滅とアイコン色(未解放/選択中)、銃アイコンの回転を毎フレーム更新する
     * @param flash 全スロットを一時的に光らせる演出量。使わないシーンは0を渡す
     */
    void UpdateWeaponSlotHud(WeaponManager* weaponManager, WeaponSlotUI* slots, int slotCount,
        float pulseTimer, float flash, engine::graphics::Sprite* gunIcon, float gunIconAngle);

    /** @brief 武器スロットの枠(+常時装備銃の枠)だけを描画する3D武器モデルを枠とアイコンの間に挟みたい場合は、この後に挟んでからDrawWeaponSlotIconsAndLabelsを呼ぶ */
    void DrawWeaponSlotFrames(const WeaponSlotUI* slots, int slotCount, engine::graphics::Sprite* gunFrame);

    /** @brief 武器スロットのアイコン・未解放"?"ラベル・GUNラベルを描画する */
    void DrawWeaponSlotIconsAndLabels(const WeaponSlotUI* slots, int slotCount, const Vector2* slotPos,
        engine::graphics::Sprite* gunIcon, const Vector2& gunPos, WeaponManager* weaponManager, FontRenderer& fontRenderer,
        float slotSize);

    /** @brief 大技（フィニッシャースラッシュ）演出中の画面暗転オーバーレイスプライトを生成する */
    std::unique_ptr<engine::graphics::Sprite> CreateFinisherOverlay(engine::graphics::SpriteCommon* spriteCommon);

    /** @brief JSONファイル（[{name, texture, additive}, ...]）に従ってパーティクルグループを一括登録する */
    void CreateParticleGroupsFromJson(engine::graphics::ParticleManager* pm, const std::string& jsonPath);

    /** @brief 有効なポストエフェクトに応じたオフスクリーンRTV（未使用時はバックバッファ）を返す */
    D3D12_CPU_DESCRIPTOR_HANDLE GetActiveRTVHandle(engine::DirectXCommon* dxCommon,
        std::initializer_list<engine::graphics::IPostEffectSource*> effects);

    /** @brief メイン描画先（GetActiveRTVHandle）とビューポート/シザーを設定する */
    void SetupMainRenderTarget(engine::DirectXCommon* dxCommon,
        std::initializer_list<engine::graphics::IPostEffectSource*> effects);

    /** @brief フィニッシャー発動時の溜めエフェクト（周囲から中心へ収束する光粒＋小リング）を放出する */
    void EmitFinisherCharge(engine::graphics::ParticleManager* pm,
        const std::string& ringGroup, const std::string& sparkGroup, const Vector3& pos);

    /** @brief 斬撃線1本ごとの煌めき（斬線に沿った光粒＋中心の閃光）を放出する */
    /// @param slashGroup 斬撃の残光グループ名空文字列なら残光はスキップする
    void EmitFinisherSlashLine(engine::graphics::ParticleManager* pm,
        const std::string& slashGroup, const std::string& sparkGroup,
        const Vector3& center, float angle, float halfLength);

    /** @brief 解放の瞬間の炸裂エフェクト（多重リング＋放射火花＋上昇する余韻）を放出する */
    void EmitFinisherRelease(engine::graphics::ParticleManager* pm,
        const std::string& ringGroup, const std::string& sparkGroup, const Vector3& pos);

    /** @brief ワールド座標の2点をスクリーン座標へ変換して斬撃線をスポーンする */
    /// @param thickness 線の太さ（ピクセル）
    void SpawnSlashMarkWorld(const Vector2& start, const Vector2& end, float camX, float camY,
        const Vector4& color, float thickness, float duration);

    /** @brief スペースキーのスピン連射弾を発射する（逆さ時は下方向中心に5方向ばらまき、通常時は向いている方向へ1発）JustSpinShot()でなければ何もしない */
    void UpdateSpinShotFire(Player* player, BulletPool& bulletPool);

    /** @brief 武器切替入力（Q/E、数字キー）を処理するweaponCycleTimer は呼び出し側が保持するクールダウン */
    void UpdateWeaponCycle(engine::Input* input, WeaponManager* weaponManager,
        float& weaponCycleTimer, bool cycleAllUnlocked = false);

    /**
     * @brief 向いている方向に厚く、背後は控えめな攻撃判定AABBを作る（近接・射撃共通）
     * @note 左右対称のAABBだと武器射程ぶん背後まで届いてしまい明らかに遠いのに当たる原因になる
     */
    engine::AABB MakeDirectionalRange(const Vector3& playerPos, float dirX, float frontRange, float backRange);
    /** @brief 高さの異なる敵へ誤命中しない射撃用の横長判定を生成する */
    engine::AABB MakeDirectionalShotRange(const Vector3& playerPos, float dirX, float frontRange, float backRange);

    /** @brief ワールド座標をスクリーン座標に変換する（カメラ位置基準） */
    void WorldToScreen(float worldX, float worldY, float camX, float camY, float& outX, float& outY);

    /**
     * @brief プレイヤーにカメラを追従させる（ステージ境界クランプ込み）
     * @param lockTarget ロックオン中の対象位置（非nullptrならカメラを対象側へ少しだけ寄せ、ロック中だと分かるようにする）
     */
    void UpdateCameraFollow(engine::graphics::Camera* camera, const Vector3& playerPos,
        const std::vector<engine::AABB>& stageSolids, const Vector3* lockTarget = nullptr);

    /** @brief 近接判定 + ENTER キーでのシーン遷移を行うポータル処理近接中なら true を返す */
    bool UpdatePortalTransition(engine::Input* input, const Vector3& playerPos,
        float portalX, float proximity, const char* targetSceneName);

    /**
     * @brief 武器一覧HUD（ヘッダー・リスト・Q/E切替ヒント）を描画し、次に描画すべきY座標を返す
     * @param anchor 描画開始位置（スクリーンpx）ステージエディタのhud_anchor("hud_anchor_weapon_list")で編集する
     */
    float DrawWeaponListHud(FontRenderer& fontRenderer, WeaponManager* weaponManager, const wchar_t* headerText, const Vector2& anchor);

    /**
     * @brief 右側の操作説明パネルを描画する
     * @param anchor 描画開始位置（スクリーンpx）ステージエディタのhud_anchor("hud_anchor_controls")で編集する
     */
    void DrawControlsHud(FontRenderer& fontRenderer, const Vector2& anchor, const wchar_t* portalActionLabel);

    /** @brief 覚醒ゲージUIを描画する */
    void DrawAwakenGaugeHud(FontRenderer& fontRenderer, engine::graphics::Sprite* bgSprite, engine::graphics::Sprite* fgSprite,
        float gauge, bool awakened, float pulseTimer);

} // namespace SceneShared
} // namespace engine::game
