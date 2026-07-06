/**
 * @file GamePlayScene.h
 * @brief メインの戦闘シーン（ローグライト／サンドボックス両対応）
 */
#pragma once

 // --- 標準ライブラリ ---
#include <deque>
#include <memory>
#include <random>
#include <string>
#include <vector>

// --- エンジンシステム・基盤 ---
#include "Audio.h"
#include "BaseScene.h"
#include "Camera.h"
#include "DirectXCommon.h"
#include "GameObject.h"
#include "ImGuiManager.h"
#include "Input.h"
#include "Model.h"
#include "ModelCommon.h"
#include "Object3dCommon.h"
#include "ShadowManager.h"
#include "Sprite.h"
#include "SpriteCommon.h"
#include "SrvManager.h"

// --- ゲームロジック・オブジェクト ---
#include "CameraShaker.h"
#include "Collision.h"
#include "EnemyEntity.h"
#include "FontRenderer.h"
#include "LevelLoader.h"
#include "Object3d.h"
#include "Player.h"
#include "TimeManager.h"
#include "WaterPool.h"
#include "GameTime.h"
#include "Skydome.h"
#include "GlassShatterEffect.h"
#include "ImageFilter.h"
#include "RenderTexture.h"
#include "SceneEditor.h"
#include "SceneShared.h"
namespace engine::graphics {
class GrayscaleEffect;
class HsvFilter;
class ParticleManager;
}

namespace engine::game {
using engine::Audio;
using engine::graphics::Camera;
using engine::DirectXCommon;
using engine::GameObject;
using engine::graphics::ImGuiManager;
using engine::Input;
using engine::graphics::Model;
using engine::graphics::ModelCommon;
using engine::graphics::Object3dCommon;
using engine::graphics::ShadowManager;
using engine::graphics::Sprite;
using engine::graphics::SpriteCommon;
using engine::graphics::SrvManager;
using engine::Collision;
using engine::graphics::Object3d;
using engine::TimeManager;
using engine::GameTime;
using engine::graphics::Skydome;
using engine::graphics::GlassShatterEffect;
using engine::graphics::ImageFilter;
using engine::graphics::RenderTexture;
using engine::graphics::GrayscaleEffect;
using engine::graphics::HsvFilter;
using engine::graphics::ParticleManager;

class ScoreManager;

class GamePlayScene : public BaseScene{
public:
    void Initialize(DirectXCommon* dxCommon,Input* input,Audio* audio) override;
    void Finalize() override;
    void Update() override;
    void Draw() override;
    void SetImGuiManager(ImGuiManager* imgui){ imguiManager_ = imgui; }
    bool SupportsPostEffects() const override { return true; }

    /// @brief ImGuiパネルからの手動テスト再生用
    void TriggerGlassShatterTest();

private:
    // シャドウマップ描画パス
    void DrawShadowPass();
    // スタイルランクとコンボ数のUI描画
    void DrawStyleUI();
    /// @brief 右上のコンボランク表示と覚醒ゲージを描画する
    void DrawRankAndAwakenGauge();
    /// @brief 右側のスタイルコマンド一覧とコンボ進捗を描画する
    void DrawStyleCommands();
    // ローグライトのHP/Gold/フロア情報HUD描画
    void DrawRogueliteHUD();
    // モデル共通描画設定（PSO/ルートシグネチャ）の適用
    void SetupModelRenderState();
    // ポストエフェクト適用中かで描画先RTVを切り替える
    D3D12_CPU_DESCRIPTOR_HANDLE GetActiveRTVHandle() const;
    // メインレンダーターゲットのセットアップ
    void SetupMainRenderTarget();
    // カメラ位置・回転の移動平均によるスムージング
    void UpdateCameraSmoothing();
    // SceneEditor用の編集コンテキストを構築
    SceneEditor::EditContext BuildEditContext();

    // クリア演出（結果表示）の状態更新。表示中ならtrue
    bool UpdateClearState();
    // 戦闘ロジック全体の更新
    void UpdateCombat();
    // 攻撃ヒット判定・ダメージ処理などの戦闘イベント更新
    void UpdateCombatEvents();
    // カメラ追従・シェイクの更新
    void UpdateCamera();
    // スタイルメーターとUI状態の更新
    void UpdateStyleAndUI(float dt);
    // パーティクルの更新
    void UpdateParticles(float dt);
    // フィニッシャースラッシュ演出（斬撃線を1本ずつ表示→本命ヒット）の更新
    void UpdateFinisherSlash(float dt);
    // 敵撃破などのクリア条件判定
    void CheckClearCondition();

    /// @brief ガラス割れ演出をサンドボックス扱いで再生すべきか（非ラン中、またはデバッグテスト再生中）
    bool IsGlassShatterFlow() const;

    DirectXCommon* dxCommon_    = nullptr;
    Input*         input_       = nullptr;
    Audio*         audio_       = nullptr;
    ImGuiManager*  imguiManager_ = nullptr;

    ScoreManager*    scoreManager_    = nullptr;
    SrvManager*      srvManager_      = nullptr;
    GrayscaleEffect* grayscaleEffect_ = nullptr;
    ImageFilter*     imageFilter_     = nullptr;
    HsvFilter*       hsvFilter_       = nullptr;
    ParticleManager* pm_              = nullptr;

    std::unique_ptr<SpriteCommon>   spriteCommon_;
    std::unique_ptr<ModelCommon>    modelCommon_;
    std::unique_ptr<Object3dCommon> objectCommon_;
    std::unique_ptr<ShadowManager>  shadowManager_;
    std::unique_ptr<Camera>         camera_;

    std::unique_ptr<Skydome> skydome_;
    std::unique_ptr<Model>   modelSkydome_;

    std::vector<std::unique_ptr<GameObject>> gameObjects_;
    LevelSpawnResult                         levelSpawn_;
    std::vector<std::unique_ptr<Object3d>>   borderBlocks_;

    std::unique_ptr<Player>      player_;
    std::unique_ptr<EnemyEntity> enemy_;

    GameTime gameTime_;

    Vector4 skyColor_      = { 1.0f, 1.0f, 1.0f, 1.0f };
    float   skyRotOffsetY_ = 0.0f;

    std::unique_ptr<RenderTexture> renderTexture_;
    std::unique_ptr<Sprite>        renderTextureSprite_;

    // カメラスムージング
    Vector3 cameraTargetPos_ = { 14.5f, 6.0f, -30.0f };
    Vector3 cameraTargetRot_ = { 0.0f, 0.0f, 0.0f };
    std::deque<Vector3> cameraPosHistory_;
    std::deque<Vector3> cameraRotHistory_;
    int cameraSmoothFrames_ = 1;

    SceneEditor sceneEditor_;

    float        hitCooldown_ = 0.0f;
    std::mt19937 rng_{ std::random_device{}() };

    static constexpr float kGhostLifetime = 0.3f;

    struct GhostEntry { Vector3 pos; float age; };
    std::deque<GhostEntry>    ghostTrail_;
    std::unique_ptr<Object3d> ghostObject_;
    float                     ghostSpawnTimer_ = 0.0f;

    float auraTimer_  = 0.0f;
    float styleMeter_ = 0.0f;
    float peakStyle_  = 0.0f;

    // フィニッシャースラッシュ演出の進行状態
    bool  finisherActive_    = false;
    int   finisherLineIdx_   = 0;
    float finisherBeatTimer_ = 0.0f;

    /** @brief 大技演出中の画面暗転オーバーレイ */
    std::unique_ptr<Sprite> finisherOverlay_;

    bool  showResult_  = false;
    float resultTimer_ = 0.0f;
    int   lastGold_    = 0;

    FontRenderer fontRenderer_;
    CameraShaker cameraShaker_;

    GlassShatterEffect glassShatter_;
    bool clearTriggered_        = false;
    bool requestClear_          = false;
    bool glassShatterDebugTest_ = false;

    std::unique_ptr<Sprite>   clearBgSprite_;
    std::unique_ptr<WaterPool> waterPool_;
};

} // namespace engine::game
