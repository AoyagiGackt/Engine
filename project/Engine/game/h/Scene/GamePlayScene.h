/**
 * @file GamePlayScene.h
 * @brief メインの戦闘シーン（ローグライト／サンドボックス両対応）
 */
#pragma once
#include "GamePauseController.h"
#include "HudLayer.h"

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
#include "StyleMeter.h"
#include "TimeManager.h"
#include "UIMenu.h"
#include "WaterPool.h"
namespace engine::graphics {
class GrayscaleEffect;
class HsvFilter;
class ParticleManager;
}

namespace engine::game {
using engine::AABB;
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
     * @return レベルJSONのパス（game_rules.jsonのlevelPathsから現在フロアに対応するもの）
     */
    std::string GetEditorLevelPath() const override;
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
    void SetEditorPlayerStaticVisual(const std::string& model, const std::string& tex) override
    {
        if (player_)
            player_->SetStaticVisualModel(model, tex);
    }
    std::string GetEditorPlayerStaticVisualModel() const override
    {
        return player_ ? player_->GetStaticVisualModelPath() : std::string { };
    }
    std::string GetEditorPlayerStaticVisualTexture() const override
    {
        return player_ ? player_->GetStaticVisualTexturePath() : std::string { };
    }
    /** @brief 編集中にプレイヤーの表示座標を現在位置へ同期する */
    void RefreshVisualTransformsForEditor() override;

    /** @brief StageEditorへ配置済みの敵（武器持ち雑魚・ボス）をenemy_/weaponEnemies_へ結び付ける */
    void OnEditorLevelLoaded() override;
    /**
     * @brief StageEditorの戦闘対象一覧とweaponEnemies_を同期する（毎フレーム呼ぶ）
     * @note spawn_pointが後から出した敵を取り込み、無効化された敵を外す。既存の項目はHP・奪取状態を保つ
     */
    void SyncCombatEnemies();

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
    /** @brief InitializeCoreSystems()の下請け 描画基盤(Common類・シャドウ・カメラ)とスカイドームを初期化する */
    void InitializeRenderFoundation();
    /** @brief InitializeCoreSystems()の下請け ステージ実体(プレイヤー/敵等)とスコアシステムを初期化する */
    void InitializeStageActorsAndScore();
    /** @brief InitializeCoreSystems()の下請け レンダーテクスチャとクリア演出用オーバーレイスプライトを初期化する */
    void InitializeRenderTargetsAndOverlays();
    /** @brief InitializeCoreSystems()の下請け パーティクル・水面・武器スロットHUDを初期化する */
    void InitializeParticlesWaterAndHud();
    /** @brief InitializeCoreSystems()の下請け 残像用オブジェクト・シーンエディタ・画面演出エフェクトを初期化する */
    void InitializeGhostEditorAndEffects();
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
    /** @brief 一時停止メニュー（RESUME/音量/タイトルへ戻る）の初期化 */
    void SetupPauseMenu();
    /** @brief 一時停止中の入力処理（ESCで再開、カーソル移動、音量調整、タイトルへ戻る） */
    void UpdatePauseMenu();
    /** @brief 一時停止中の暗転オーバーレイとメニューを描画する */
    void DrawPauseOverlay();
    /** @brief 道中の武器敵を更新し、攻撃と武器奪取を処理する */
    void UpdateWeaponEnemies();
    /** @brief レベルに配置された壊せる物（kind=="breakable"）のヒット判定・爆発ダメージ・演出を処理する */
    void UpdateExplosiveBarrels();
    /** @brief 現在のフロアが水面演出の対象か（game_rules.jsonのwaterFloor） */
    bool IsWaterFloor() const;
    /** @brief 左上の武器スロット一覧と操作ヒントを描画する（BattleTestSceneと同じ体裁） */
    void DrawWeaponListPanel();
    /** @brief ボス・道中の武器敵の頭上HPバーの位置と色を更新する（BattleTestSceneのダミーHPバーと同じ体裁） */
    void UpdateEnemyHpBars();
    /** @brief 左下のプレイヤーHPゲージの位置と色を更新する */
    void UpdatePlayerHpBar();
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
    /** @brief Shift長押しで最寄りの敵をロックオンし、その方向へ向かせる（BattleTestSceneと同じ規約） */
    void UpdateTargetLock();
    // カメラ追従・シェイクの更新
    void UpdateCamera();
    // スタイルメーターとUI状態の更新
    void UpdateStyleAndUI(float dt);
    /** @brief UpdateStyleAndUI()の下請け 敵の当たり判定AABBを現在位置から算出する */
    AABB GetEnemyHitBox() const;
    /** @brief UpdateStyleAndUI()の下請け 近接コンボのヒット判定とスタイル加点・属性追撃を処理する */
    void ApplyMeleeComboStyleHit(const AABB& enemyAABB);
    /** @brief UpdateStyleAndUI()の下請け 武器固有技（SPACE）のヒット判定とスタイル加点を処理する */
    void ApplyWeaponSkillStyleHit(const AABB& enemyAABB);
    /** @brief UpdateStyleAndUI()の下請け 銃コンボのヒット判定とスタイル加点を処理する */
    void ApplyGunShotStyleHit(const AABB& enemyAABB);
    /** @brief UpdateStyleAndUI()の下請け ダガー スティンガー刺突のスタイル加点を処理する */
    void ApplyDaggerStingerStyleBonus();
    /** @brief UpdateStyleAndUI()の下請け 覚醒乱舞ラッシュのヒット判定とスタイル加点を処理する */
    void ApplyRampageStyleHit(const AABB& enemyAABB);
    /** @brief UpdateStyleAndUI()の下請け スタイルメーターの時間経過による減衰を処理する */
    void DecayStyleMeter(float dt);
    /** @brief UpdateStyleAndUI()の下請け 回避の連打ペナルティを処理する（ジャスト回避の加点はTryJustDodge側） */
    void UpdateDodgeStyle();
    /** @brief 覚醒中の武器倍率とジャスト回避直後の強化窓を合わせた、現在の攻撃ダメージ倍率 */
    float CurrentDamageMult() const;
    /**
     * @brief 固有技の判定半径を返す（覚醒中の武器倍率と、習得済みボス技による叩きつけ強化を含む）
     * @param baseRadius 通常時の判定半径
     * @param slam 叩きつけ系（大剣/ハンマー）の技か
     */
    float SkillRadiusFor(float baseRadius, bool slam) const;
    /** @brief 叩きつけ系固有技が習得済みボス技で強化されているか */
    bool HasBossSlamTechnique() const;
    /**
     * @brief 近接敵の攻撃発生フレームに前方判定を出し、命中ならプレイヤーへダメージを与える
     * @param attacker 攻撃した敵
     * @note 回避中ならジャスト回避として処理し、ダメージは入らない
     */
    void ApplyEnemyMeleeSwing(EnemyEntity* attacker);
    /**
     * @brief 敵の攻撃がプレイヤーに届いた瞬間、回避中ならジャスト回避として成立させる
     * @param hitPos 演出を出す位置（弾や着弾点）
     * @return 回避中で攻撃を無効化した場合はtrue（呼び出し側はダメージ処理を行わない）
     * @note 1回の回避につき加点・演出は1回だけ。2発目以降は無効化のみ行う
     */
    bool TryJustDodge(const Vector3& hitPos);
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
    /** @brief UpdateParticles()の下請け ボスの予告円→着弾ダメージのAoEスラム攻撃を処理する */
    void UpdateBossSlamAttack(float dt);
    /** @brief UpdateParticles()の下請け 格闘/射撃/瞬歩/覚醒ゲージ・覚醒発動・ランク上昇のスタイル演出パーティクルを更新する */
    void UpdateStyleTechniqueParticles(float dt);
    /** @brief UpdateStyleTechniqueParticles()の下請け 格闘コンボヒット時の斬撃・属性パーティクルを発生させる */
    void EmitComboHitParticles(const Vector3& ppos);
    /**
     * @brief 敵に攻撃が当たった位置へ星・リング・火花を出す（どの攻撃が敵に入ったかを敵側で見せる）
     * @param enemyPos 当たった敵の中心位置
     * @param color 演出の色（武器の属性色など）
     * @param strength 演出の大きさ倍率（1.0が通常ヒット。強い技ほど大きく）
     */
    void EmitEnemyHitEffect(const Vector3& enemyPos, const Vector4& color, float strength,
        int extraBurstCount = 0, float ringRadius = 0.0f);
    /** @brief 武器属性ごとに形・運動の異なる追加命中演出を出す */
    void EmitElementalHitEffect(const WeaponData& weapon, const Vector3& enemyPos, int comboStep);
    /** @brief 敵が予備動作に入った瞬間に警告リングを出す（攻撃が来ることを事前に伝え、回避を狙えるようにする） */
    void EmitEnemyTelegraphCue(const EnemyEntity* enemy);
    /** @brief UpdateStyleTechniqueParticles()の下請け 銃発射時の弾煙パーティクルを発生させる */
    void EmitGunFireParticles(const Vector3& ppos);
    /** @brief UpdateStyleTechniqueParticles()の下請け 瞬歩トレイル・覚醒ゲージ加算時のパーティクルを発生させる */
    void EmitBlinkAndGaugeParticles(const Vector3& ppos);
    /** @brief UpdateStyleTechniqueParticles()の下請け 武器固有技（ダガーのスティンガー以外）発動時に技ごとの見た目を出す */
    void EmitWeaponSkillCastParticles(const Vector3& ppos);
    /** @brief UpdateStyleTechniqueParticles()の下請け 覚醒中の継続オーラと発動瞬間の衝撃波を発生させる */
    void EmitAwakenParticles(const Vector3& ppos, float dt);
    /** @brief UpdateStyleTechniqueParticles()の下請け styleRankHud_のランクが上がった瞬間にリング・火花・カメラシェイク・画面フラッシュを出す */
    void EmitStyleRankUpParticles(const Vector3& ppos);
    /** @brief 近接コンボのモーション中、装備武器の色で手元にトレイル残像を発生させ続ける */
    void UpdateWeaponTrail();
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
    HudLayer hud_;

    std::unique_ptr<ModelCommon> modelCommon_;
    std::unique_ptr<Object3dCommon> objectCommon_;
    std::unique_ptr<ShadowManager> shadowManager_;
    std::unique_ptr<Camera> camera_;

    std::unique_ptr<Skydome> skydome_;
    Model* modelSkydome_ = nullptr; // 実体はModelManagerが所有・共有する

    std::unique_ptr<Player> player_;
    // ボス敵。実体はStageEditorの配置物(kind=="enemy_basic", isStageBoss=true)が所有し、
    // OnEditorLevelLoaded()でポインタだけを受け取る（非所有）
    EnemyEntity* enemy_ = nullptr;
    WeaponType bossWeaponType_ = WeaponType::Sword; ///< ボスの配置物に設定された武器種別（撃破後にこれを奪う）
    // ボス頭上のHPバー（BattleTestSceneのDummy::hpBarBg/Fgと同じ体裁、GamePlayScene初期化時に一度だけ生成）
    std::unique_ptr<Sprite> bossHpBarBg_;
    std::unique_ptr<Sprite> bossHpBarFg_;

    // プレイヤーHPゲージ（左下、数値表示に加えて一目で残量が分かるようにするバー）
    std::unique_ptr<Sprite> playerHpBarBg_;
    std::unique_ptr<Sprite> playerHpBarFg_;

    /** @brief 道中に配置された武器持ち敵1体分の状態（撃破後、Jキーで吸収して武器を奪取する） */
    struct WeaponEnemyEntry {
        EnemyEntity* enemy = nullptr; // 非所有。StageEditorの配置物が実体を所有する
        WeaponType weaponType = WeaponType::Sword;
        bool weaponAcquired = false;
        bool absorbing = false;
        bool defeatEffectEmitted = false;
        float absorbTimer = 0.0f;
        std::unique_ptr<Sprite> hpBarBg; // OnEditorLevelLoaded()で生成（レベル再読込のたびに作り直す）
        std::unique_ptr<Sprite> hpBarFg;
    };
    std::vector<WeaponEnemyEntry> weaponEnemies_;

    /** @brief ロックオン中の対象種別（BattleTestSceneと同じくShift長押し中は最寄りの敵を自動追従する） */
    enum class LockTargetKind { None,
        MainEnemy,
        WeaponEnemy };
    LockTargetKind lockedKind_ = LockTargetKind::None;
    size_t lockedWeaponEnemyIndex_ = 0;

    // 収集物（pickup）と壊せる物（breakable）はレベルJSONの配置物としてStageEditorが所有する。
    // ここでは壊せる物の脈動表示に使うタイマーだけを持つ
    float explosiveBarrelPulse_ = 0.0f;

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

    // 敵の遠隔攻撃弾ボスと道中の武器持ち敵が共用する（発射時にプレイヤーが射程内にいる敵だけ撃つ）
    // 弾速・寿命・射程はResources/Config/enemy_params.jsonのbullet節（EnemyTuning::Bullet()）で持つ

    /** @brief 敵の遠隔攻撃弾1発分の状態 */
    struct EnemyBullet {
        Vector3 pos = { };
        Vector3 vel = { };
        float timer = 0.0f;
        int damage = 0;
    };
    std::vector<EnemyBullet> enemyBullets_;

    // ボスの予告円→着弾のAoEスラム攻撃(戦闘演出ギミック)
    // 間隔・予告秒数・半径・ダメージはenemy_params.jsonのbossSlam節（EnemyTuning::BossSlam()）で持つ
    bool bossSlamWarningActive_ = false;
    float bossSlamTimer_ = 0.0f; ///< OnEditorLevelLoaded()でEnemyTuning::BossSlam().intervalから初期化する
    float bossSlamWarningTimer_ = 0.0f;
    Vector3 bossSlamTargetPos_ = { };
    // 着弾地点に出す予告円（円形グロー画像を着弾範囲の見かけ半径まで拡大し、赤く点滅させる）
    std::unique_ptr<Sprite> bossSlamWarningSprite_;

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
    float floorElapsedSeconds_ = 0.0f; ///< このフロアに入ってからの経過秒数（OnEditorLevelLoaded()でリセット、プレイログ用）
    /** @brief 右上のスタイリッシュランクHUD（採点はstyleMeter_側で行い、表示だけこれに委ねる） */
    StyleMeter styleRankHud_;
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
    bool mainEnemyDefeatEffectEmitted_ = false;
    bool mainWeaponAbsorbing_ = false;
    float mainWeaponAbsorbTimer_ = 0.0f;
    bool requestClear_ = false;
    bool glassShatterDebugTest_ = false;

    std::unique_ptr<Sprite> clearBgSprite_;
    std::unique_ptr<WaterPool> waterPool_;

    // 一時停止メニュー（ESCで開閉、ゲームプレイ中限定。クリア演出中・武器交換中は開けない）
    GamePauseController pauseController_;
    UIMenu pauseMenu_;
    std::unique_ptr<Sprite> pauseOverlay_;
    static constexpr int kPauseRowResume = 0;
    static constexpr int kPauseRowBgmVolume = 1;
    static constexpr int kPauseRowSeVolume = 2;
    static constexpr int kPauseRowQuit = 3;
};

} // namespace engine::game
