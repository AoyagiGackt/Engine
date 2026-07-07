/**
 * @file BattleTestScene.h
 * @brief 訓練マネキンを相手にコンボや武器を試せるデバッグ用バトルシーン
 */
#pragma once
#include <array>
#include <memory>
#include <vector>

#include "Audio.h"
#include "BaseScene.h"
#include "BladeFlashEffect.h"
#include "BulletPool.h"
#include "Camera.h"
#include "DirectXCommon.h"
#include "FontRenderer.h"
#include "GlassShatterEffect.h"
#include "ImageFilter.h"
#include "ImGuiManager.h"
#include "Input.h"
#include "MeshSliceEffect.h"
#include "Model.h"
#include "ModelCommon.h"
#include "Object3d.h"
#include "Object3dCommon.h"
#include "ParticleManager.h"
#include "Player.h"
#include "SceneShared.h"
#include "ShadowManager.h"
#include "SpaceDistortionEffect.h"
#include "Sprite.h"
#include "SpriteCommon.h"
#include "SrvManager.h"
#include "WeaponManager.h"
namespace engine::graphics {
class GrayscaleEffect;
class HsvFilter;
}

namespace engine::game {
using engine::Audio;
using engine::graphics::BladeFlashEffect;
using engine::graphics::Camera;
using engine::DirectXCommon;
using engine::graphics::GlassShatterEffect;
using engine::graphics::ImageFilter;
using engine::graphics::GrayscaleEffect;
using engine::graphics::HsvFilter;
using engine::graphics::ImGuiManager;
using engine::Input;
using engine::graphics::MeshSliceEffect;
using engine::graphics::Model;
using engine::graphics::ModelCommon;
using engine::graphics::Object3d;
using engine::graphics::Object3dCommon;
using engine::graphics::ParticleManager;
using engine::graphics::ShadowManager;
using engine::graphics::SpaceDistortionEffect;
using engine::graphics::Sprite;
using engine::graphics::SpriteCommon;
using engine::graphics::SrvManager;

/// @brief 訓練マネキン相手にコンボ・武器を試せるデバッグ用シーン
class BattleTestScene : public BaseScene {
public:
    void Initialize(DirectXCommon* dxCommon, Input* input, Audio* audio) override;
    void Finalize() override;
    void Update() override;
    void Draw() override;
    void SetImGuiManager(ImGuiManager* imgui) override { imguiManager_ = imgui; }
    bool SupportsPostEffects() const override { return true; }

    /// @brief ImGuiパネルからの手動テスト再生用
    void TriggerGlassShatterTest();

private:
    // 訓練用マネキン（動かない敵）
    struct Dummy {
        std::unique_ptr<Object3d> object;
        Vector3  pos;
        Vector3  homePos;
        float    hp;
        float    maxHp;
        float    hitFlash    = 0.0f; // 被弾時の白フラッシュタイマー
        float    hpDisplay_  = 1.0f; // HP バー表示用（0→1 に回復する演出値）
        float    knockVelX   = 0.0f;
        float    knockVelY   = 0.0f;
        float    returnTimer = 0.0f; // 最後に被弾してからの秒数（超えたら中央へ戻る）
        bool     sliced      = false; // 切断演出中は本体モデルを非表示にする

        // HP バー用スプライト
        std::unique_ptr<Sprite> hpBarBg;
        std::unique_ptr<Sprite> hpBarFg;
    };

    /// @brief 指定座標にヒットエフェクト（パーティクル）を生成する
    void SpawnHitEffect(const Vector3& pos);
    /// @brief 全マネキンのHPバースプライトを現在HPに合わせて更新する
    void UpdateHpBars();
    /// @brief 有効なポストエフェクトに応じたオフスクリーンRTV（未使用時はバックバッファ）を返す
    D3D12_CPU_DESCRIPTOR_HANDLE GetActiveRTVHandle() const;
    /// @brief メイン描画先（GetActiveRTVHandle）とビューポート/シザーを設定する
    void SetupMainRenderTarget();

    /// @brief プレイヤーを更新し、カメラをプレイヤーに追従させる
    void UpdatePlayerAndCamera();
    /// @brief 影・境界ブロック・ワープポータルの演出を更新する
    void UpdateEnvironment();
    /// @brief 格闘/射撃/乱舞/弾丸の当たり判定を処理する命中があれば true を返す
    bool UpdateCombat();
    /// @brief フィニッシャースラッシュ演出（斬撃線を1本ずつ表示→本命ヒット）を更新する本命ヒットの瞬間なら true を返す
    bool UpdateFinisherSlash();
    /// @brief コンボランク表示のカウント・フェードを更新する
    void UpdateComboRank(bool hitConfirmed);
    /// @brief ダミーのノックバック物理とHPバー表示を更新する
    void UpdateDummies();
    /// @brief HUD全体（武器一覧・操作説明・コンボランク・覚醒ゲージ）を描画する
    void DrawHud(bool nearReturnPortal);
    /// @brief 武器一覧と戻りポータルのラベルを描画する
    void DrawWeaponHud(bool nearReturnPortal);
    /// @brief 画面中央のコンボランク表示を描画する
    void DrawComboRankHud();

    DirectXCommon* dxCommon_     = nullptr;
    Input*         input_        = nullptr;
    Audio*         audio_        = nullptr;
    ImGuiManager*  imguiManager_ = nullptr;
    SrvManager*    srvManager_   = nullptr;
    ParticleManager* pm_         = nullptr;

    GrayscaleEffect* grayscaleEffect_ = nullptr;
    ImageFilter*     imageFilter_     = nullptr;
    HsvFilter*       hsvFilter_       = nullptr;

    GlassShatterEffect       glassShatter_;
    std::unique_ptr<Sprite>  glassShatterBgSprite_;

    /** @brief 解放時に「暗転+斬撃線ごと凍った画面」を砕いて素の世界を見せる演出 */
    GlassShatterEffect finisherShatter_;

    /** @brief 空間に走るガラス質の刃パーティクル */
    BladeFlashEffect bladeFlash_;
    /** @brief プレイヤー中心の空間歪み（レンズ歪み+色収差） */
    SpaceDistortionEffect spaceWarp_;
    /** @brief 解放時にダミーを切断破片へ差し替える演出 */
    MeshSliceEffect dummySlice_;

    std::unique_ptr<SpriteCommon>    spriteCommon_;
    std::unique_ptr<ModelCommon>     modelCommon_;
    std::unique_ptr<Object3dCommon>  objectCommon_;
    std::unique_ptr<ShadowManager>   shadowManager_;
    std::unique_ptr<Camera>          camera_;

    // 境界ブロック
    std::unique_ptr<Model>                  modelBlock_;
    std::vector<std::unique_ptr<Object3d>>  borderBlocks_;

    // ワープポータル（トレーニングルームへ戻る）
    std::vector<std::unique_ptr<Object3d>>  warpPortalBlocks_;

    // プレイヤー
    std::unique_ptr<Player> player_;

    // 訓練マネキン
    std::unique_ptr<Model>   modelDummy_;
    std::vector<Dummy>       dummies_;

    // 武器
    WeaponManager* weaponManager_ = nullptr;
    float attackCooldown_    = 0.0f;
    float weaponCycleTimer_  = 0.0f;

    // 弾丸
    BulletPool bulletPool_;

    // ワープ演出
    float warpPulseTimer_ = 0.0f;

    // コンボランク（実際にダミーへ命中した時だけ加算）
    int   trComboCount_ = 0;
    int   trMaxCombo_   = 0;
    float trComboTimer_ = 0.0f;
    float trRankAlpha_  = 0.0f;

    // 覚醒ゲージ UI
    std::unique_ptr<Sprite> awakenGaugeBg_;
    std::unique_ptr<Sprite> awakenGaugeFg_;

    // フィニッシャースラッシュ演出の進行状態
    bool  finisherActive_    = false;
    int   finisherLineIdx_   = 0;
    float finisherBeatTimer_ = 0.0f;

    /** @brief 大技演出中の画面暗転オーバーレイ */
    std::unique_ptr<Sprite> finisherOverlay_;

    FontRenderer fontRenderer_;
};

} // namespace engine::game
