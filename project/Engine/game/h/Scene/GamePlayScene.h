/**
 * @file GamePlayScene.h
 * @brief メインの戦闘シーン（ローグライト／サンドボックス両対応）
 */
#pragma once

// 標準ライブラリ
#include <array>
#include <deque>
#include <memory>
#include <random>
#include <string>
#include <vector>

// エンジンシステム・基盤
#include "Audio.h"
#include "BaseScene.h"
#include "Camera.h"
#include "DirectXCommon.h"
#include "ImGuiManager.h"
#include "Input.h"
#include "Model.h"
#include "ModelCommon.h"
#include "Object3dCommon.h"
#include "ShadowManager.h"
#include "Sprite.h"
#include "SpriteCommon.h"
#include "SrvManager.h"

// ゲームロジック・オブジェクト
#include "BladeFlashEffect.h"
#include "CameraShaker.h"
#include "Collision.h"
#include "EnemyEntity.h"
#include "EnemyRegistry.h"
#include "FontRenderer.h"
#include "GameTime.h"
#include "GlassShatterEffect.h"
#include "ImageFilter.h"
#include "LevelLoader.h"
#include "MeshSliceEffect.h"
#include "Object3d.h"
#include "Player.h"
#include "RenderTexture.h"
#include "SceneEditor.h"
#include "SceneShared.h"
#include "Skydome.h"
#include "SpaceDistortionEffect.h"
#include "TimeManager.h"
#include "WaterPool.h"
namespace engine::graphics {
class GrayscaleEffect;
class HsvFilter;
class ParticleManager;
}

namespace engine::game {
using engine::Audio;
using engine::Collision;
using engine::DirectXCommon;
using engine::GameTime;
using engine::Input;
using engine::TimeManager;
using engine::graphics::BladeFlashEffect;
using engine::graphics::Camera;
using engine::graphics::GlassShatterEffect;
using engine::graphics::GrayscaleEffect;
using engine::graphics::HsvFilter;
using engine::graphics::ImageFilter;
using engine::graphics::ImGuiManager;
using engine::graphics::MeshSliceEffect;
using engine::graphics::Model;
using engine::graphics::ModelCommon;
using engine::graphics::Object3d;
using engine::graphics::Object3dCommon;
using engine::graphics::ParticleManager;
using engine::graphics::RenderTexture;
using engine::graphics::ShadowManager;
using engine::graphics::Skydome;
using engine::graphics::SpaceDistortionEffect;
using engine::graphics::Sprite;
using engine::graphics::SpriteCommon;
using engine::graphics::SrvManager;

class ScoreManager;
class GamePlaySceneInitializer;

/**
 * @brief メインステージの戦闘、進行、演出、描画を統括する
 *
 * プレイヤーと敵のゲーム進行を調停し、個別システムの更新結果を描画パスへ渡す。
 */
class GamePlayScene : public BaseScene {
    friend class GamePlaySceneInitializer;

public:
    /**
     * @brief シーンで使用するゲーム実体と描画資源を初期化する
     * @param dxCommon DirectXの共通処理
     * @param input 入力管理
     * @param audio 音声管理
     */
    void Initialize(DirectXCommon* dxCommon, Input* input, Audio* audio) override;
    /** @brief シーン固有の演出資源と登録済みコールバックを破棄する */
    void Finalize() override;
    /** @brief ゲーム進行、戦闘、カメラ、演出を更新する */
    void Update() override;
    /** @brief 3Dワールドと画面UIを描画する */
    void Draw() override;
    /**
     * @brief シーン調整パネルに使用するImGui管理を設定する
     * @param imgui 使用するImGui管理
     */
    void SetImGuiManager(ImGuiManager* imgui) { imguiManager_ = imgui; }
    /**
     * @brief ポストエフェクト対応の有無を返す
     * @return 常にtrue
     */
    bool SupportsPostEffects() const override { return true; }

    /**
     * @brief ステージエディタの判定に使用するプレイヤー位置を返す
     * @return 現在のプレイヤー位置
     */
    Vector3 GetEditorPlayerPos() const override { return player_ ? player_->GetPosition() : Vector3 { }; }

    /**
     * @brief ステージエディタが読み書きするレベルファイルを返す
     * @return レベルJSONのパス
     */
    std::string GetEditorLevelPath() const override { return "Resources/Levels/level01.json"; }
    /**
     * @brief ステージ配置物の生成に使用するモデル共通処理を返す
     * @return シーンが所有するモデル共通処理
     */
    ModelCommon* GetEditorModelCommon() override { return modelCommon_.get(); }
    /**
     * @brief ステージエディタの表示と操作に使用するカメラを返す
     * @return シーンが所有するカメラ
     */
    Camera* GetEditorCamera() override { return camera_.get(); }
    /**
     * @brief エディタ配置敵の演出に使用するパーティクル管理を返す
     * @return 共有パーティクル管理
     */
    ParticleManager* GetEditorParticleManager() override { return pm_; }
    /**
     * @brief エディタ操作で直接更新するプレイヤー位置を返す
     * @return プレイヤー未生成時はnullptr
     */
    Vector3* GetEditorPlayerPositionRef() override { return player_ ? &player_->GetPositionRef() : nullptr; }
    int GetEditorPlayerVisualPreset() const override { return player_ ? player_->GetVisualPreset() : -1; }
    void SetEditorPlayerVisualPreset(int preset) override
    {
        if (player_)
            player_->SetVisualPreset(preset);
    }
    void SetEditorPlayerStaticVisual(const std::string& path) override
    {
        if (player_)
            player_->SetStaticVisualModel(path);
    }
    /** @brief 編集中にプレイヤーの表示座標を現在位置へ同期する */
    void RefreshVisualTransformsForEditor() override;

    /** @brief ガラス割れ演出を手動テストとして開始する */
    void TriggerGlassShatterTest();

    /**
     * @brief 追加ホットキーの案内文字列を返す
     * @return シーン調整パネルのホットキー文字列
     */
    const char* GetHotkeyOverlayExtra() const override { return "F3: シーン調整パネル"; }

private:
    /** @brief シーンが所有するゲーム実体、描画資源、演出を責務順に初期化する */
    void InitializeCoreSystems();
    // シャドウマップ描画パス
    void DrawShadowPass();
    // スタイルランクとコンボ数のUI描画
    void DrawStyleUI();
    /** @brief テストシーンと共通の武器スロットUIを初期化する */
    void InitializeWeaponSlotHud();
    /** @brief 武器スロットUIの選択状態と演出を更新する */
    void UpdateWeaponSlotHud();
    /** @brief 武器スロットUIを描画する */
    void DrawWeaponSlotHud();
    /** @brief プレイヤーの進行位置に対応する操作目標を描画する */
    void DrawStageGuide();
    /** @brief 満杯時の武器交換入力を処理する */
    void UpdateWeaponExchange();
    /** @brief 満杯時の武器交換画面を描画する */
    void DrawWeaponExchange();
    /** @brief 道中の武器敵を更新し、攻撃と武器奪取を処理する */
    void UpdateWeaponEnemies();
    /** @brief 武器固有技による進行障壁の解除を処理する */
    void UpdateWeaponGimmicks();
    /** @brief 探索用エネルギーコアの回収と表示更新を処理する */
    void UpdateEnergyCores();
    /** @brief 右上のコンボランク表示と覚醒ゲージを描画する */
    void DrawRankAndAwakenGauge();
    /** @brief 右側のスタイルコマンド一覧とコンボ進捗を描画する */
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

    // クリア演出（結果表示）の状態更新表示中ならtrue
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
    /** @brief UpdateParticles()の下請け 着地ほこりとジャンプ煙のパーティクルを更新する */
    void UpdateLandingAndJumpDustParticles();
    /** @brief UpdateParticles()の下請け 横移動・空中時の残像トレイルをスポーン・経年・削除する */
    void UpdateGhostTrail(float dt);
    /** @brief UpdateParticles()の下請け プレイヤーと敵の接触ヒット判定・ダメージ・演出を処理する */
    void UpdatePlayerEnemyContactHit(float dt);
    /** @brief UpdateParticles()の下請け 敵の弾の発射・飛翔・プレイヤーへの命中時のダメージ・無敵開始・演出を処理する */
    void UpdateEnemyAttackOnPlayer(float dt);
    /** @brief UpdateParticles()の下請け 格闘/射撃/瞬歩/覚醒ゲージ・覚醒発動・ランク上昇のスタイル演出パーティクルを更新する */
    void UpdateStyleTechniqueParticles(float dt);
    // フィニッシャースラッシュ演出（斬撃線を1本ずつ表示→本命ヒット）の更新
    void UpdateFinisherSlash(float dt);
    // 敵撃破などのクリア条件判定
    void CheckClearCondition();

    /** @brief ガラス割れ演出をサンドボックス扱いで再生すべきか（非ラン中、またはデバッグテスト再生中） */
    bool IsGlassShatterFlow() const;

    /** @brief Draw()の下請け クリア演出中の専用画面（ローグライト結果表示／サンドボックスのガラス割れ導入）を描画する。描画してDraw()を打ち切るべきなら true を返す */
    bool DrawClearOverlayIfNeeded();
    /** @brief Draw()の下請け 3Dワールド（地形・残像・プレイヤー/敵・パーティクル・空間歪み）を描画する */
    void DrawWorldAndActors();
    /** @brief Draw()の下請け エディタUI・フィニッシャー演出・フォントなど2D上乗せ描画をまとめて行う */
    void DrawOverlaysAndUI();

    DirectXCommon* dxCommon_ = nullptr;
    Input* input_ = nullptr;
    Audio* audio_ = nullptr;
    ImGuiManager* imguiManager_ = nullptr;

    ScoreManager* scoreManager_ = nullptr;
    SrvManager* srvManager_ = nullptr;
    GrayscaleEffect* grayscaleEffect_ = nullptr;
    ImageFilter* imageFilter_ = nullptr;
    HsvFilter* hsvFilter_ = nullptr;
    ParticleManager* pm_ = nullptr;

    std::unique_ptr<SpriteCommon> spriteCommon_;
    std::unique_ptr<Sprite> awakenGaugeBg_;
    std::unique_ptr<Sprite> awakenGaugeFg_;
    static constexpr int kWeaponSlotCount = 7;
    std::array<SceneShared::WeaponSlotUI, kWeaponSlotCount> weaponSlots_;
    std::array<Vector2, kWeaponSlotCount> weaponSlotPos_;
    std::unique_ptr<Sprite> gunFrame_;
    std::unique_ptr<Sprite> gunIcon_;
    Vector2 gunPos_ = { };
    float weaponSlotPulse_ = 0.0f;
    float gunIconAngle_ = 0.0f;
    std::unique_ptr<ModelCommon> modelCommon_;
    std::unique_ptr<Object3dCommon> objectCommon_;
    std::unique_ptr<ShadowManager> shadowManager_;
    std::unique_ptr<Camera> camera_;

    std::unique_ptr<Skydome> skydome_;
    std::unique_ptr<Model> modelSkydome_;

    std::unique_ptr<Player> player_;
    std::unique_ptr<EnemyEntity> enemy_;

    /** @brief 道中に配置された武器持ち敵1体分の状態（撃破後、Jキーで吸収して武器を奪取する） */
    struct WeaponEnemyEntry {
        std::unique_ptr<EnemyEntity> enemy;
        WeaponType weaponType = WeaponType::Sword;
        bool weaponAcquired = false;
        bool absorbing = false;
        float absorbTimer = 0.0f;
    };
    std::vector<WeaponEnemyEntry> weaponEnemies_;

    std::unique_ptr<Model> gimmickBlockModel_;
    std::unique_ptr<Object3d> swordGate_;
    std::unique_ptr<Object3d> spearGate_;
    bool swordGateActive_ = true;
    bool spearGateActive_ = true;

    /** @brief 探索用エネルギーコア1個の状態（プレイヤーが触れると回収され、覚醒ゲージが増える） */
    struct EnergyCoreEntry {
        std::unique_ptr<Object3d> object;
        Vector3 position = { };
        bool collected = false;
    };
    std::unique_ptr<Model> energyCoreModel_;
    std::vector<EnergyCoreEntry> energyCores_;
    float energyCorePulse_ = 0.0f;
    int collectedEnergyCores_ = 0;

    GameTime gameTime_;

    Vector4 skyColor_ = { 1.0f, 1.0f, 1.0f, 1.0f };
    float skyRotOffsetY_ = 0.0f;

    std::unique_ptr<RenderTexture> renderTexture_;
    std::unique_ptr<Sprite> renderTextureSprite_;

    // カメラスムージング
    Vector3 cameraTargetPos_ = { 14.5f, 6.0f, -24.0f };
    Vector3 cameraTargetRot_ = { 0.0f, 0.0f, 0.0f };
    std::deque<Vector3> cameraPosHistory_;
    std::deque<Vector3> cameraRotHistory_;
    int cameraSmoothFrames_ = 1;

    SceneEditor sceneEditor_;

    float hitCooldown_ = 0.0f;
    std::mt19937 rng_ { std::random_device { }() };

    static constexpr float kGhostLifetime = 0.3f;

    // 敵の遠隔攻撃弾同時に1発のみ（攻撃間隔がAABBチェック不要な程度に長いため、複数弾のプールは不要）
    static constexpr float kEnemyBulletSpeed = 9.0f;
    static constexpr float kEnemyBulletLifetime = 1.2f;
    Vector3 enemyBulletPos_ = { };
    Vector3 enemyBulletVel_ = { };
    float enemyBulletTimer_ = 0.0f;
    bool enemyBulletActive_ = false;

    /** @brief 覚醒中の残像トレイル1コマ分の位置と経過秒数（kGhostLifetimeを超えたら消える） */
    struct GhostEntry {
        Vector3 pos;
        float age;
    };
    std::deque<GhostEntry> ghostTrail_;
    std::unique_ptr<Object3d> ghostObject_;
    float ghostSpawnTimer_ = 0.0f;

    float auraTimer_ = 0.0f;
    float styleMeter_ = 0.0f;
    float peakStyle_ = 0.0f;
    int prevStyleTier_ = 0;
    int lastTechniqueId_ = -1;
    int repeatedTechniqueCount_ = 0;

    // フィニッシャースラッシュ演出の進行状態
    bool finisherActive_ = false;
    int finisherLineIdx_ = 0;
    float finisherBeatTimer_ = 0.0f;

    /** @brief 大技演出中の画面暗転オーバーレイ */
    std::unique_ptr<Sprite> finisherOverlay_;

    /** @brief 解放時に敵本体を切断破片へ差し替える演出 */
    MeshSliceEffect enemySlice_;

    /** @brief 空間に走るガラス質の刃パーティクル */
    BladeFlashEffect bladeFlash_;

    /** @brief 敵中心の空間歪み（レンズ歪み+色収差） */
    SpaceDistortionEffect spaceWarp_;

    bool showResult_ = false;
    float resultTimer_ = 0.0f;
    int lastGold_ = 0;

    FontRenderer fontRenderer_;
    CameraShaker cameraShaker_;

    GlassShatterEffect glassShatter_;

    /** @brief 解放時に暗転+斬撃線ごと凍った画面を砕いて素の世界を見せる演出 */
    GlassShatterEffect finisherShatter_;

    bool clearTriggered_ = false;
    bool weaponStealTriggered_ = false;
    bool mainWeaponAbsorbing_ = false;
    float mainWeaponAbsorbTimer_ = 0.0f;
    bool requestClear_ = false;
    bool glassShatterDebugTest_ = false;

    std::unique_ptr<Sprite> clearBgSprite_;
    std::unique_ptr<WaterPool> waterPool_;
};

} // namespace engine::game
