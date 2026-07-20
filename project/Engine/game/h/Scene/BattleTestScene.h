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
#include "ImGuiManager.h"
#include "ImageFilter.h"
#include "Input.h"
#include "KnightEnemy.h"
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
#include "StyleMeter.h"
#include "WeaponManager.h"
namespace engine::graphics {
class GrayscaleEffect;
class HsvFilter;
}

namespace engine::game {
using engine::Audio;
using engine::DirectXCommon;
using engine::Input;
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
using engine::graphics::ShadowManager;
using engine::graphics::SpaceDistortionEffect;
using engine::graphics::Sprite;
using engine::graphics::SpriteCommon;
using engine::graphics::SrvManager;

/** @brief 訓練マネキン相手にコンボ・武器を試せるデバッグ用シーン */
class BattleTestScene : public BaseScene {
public:
    void Initialize(DirectXCommon* dxCommon, Input* input, Audio* audio) override;
    void Finalize() override;
    void Update() override;
    void Draw() override;
    void SetImGuiManager(ImGuiManager* imgui) override { imguiManager_ = imgui; }
    bool SupportsPostEffects() const override { return true; }

    /** @brief ImGuiパネルからの手動テスト再生用 */
    void TriggerGlassShatterTest();

    /** @brief StageEditorのトリガー判定用（SceneManagerが毎フレーム参照する） */
    Vector3 GetEditorPlayerPos() const override { return player_ ? player_->GetPosition() : Vector3 { }; }

    const char* GetHotkeyOverlayExtra() const override { return "F3: コライダー表示"; }

    // BaseScene::Init()/Tick()からのStageEditor自動配線フック
    std::string GetEditorLevelPath() const override { return "Resources/Levels/level01.json"; }
    ModelCommon* GetEditorModelCommon() override { return modelCommon_.get(); }
    Camera* GetEditorCamera() override { return camera_.get(); }
    ParticleManager* GetEditorParticleManager() override { return pm_; }
    Vector3* GetEditorPlayerPositionRef() override { return player_ ? &player_->GetPositionRef() : nullptr; }
    /** @brief エディタ表示中（ゲームプレイ停止中）にプレイヤー/ナイトの見た目だけ追従させる */
    void RefreshVisualTransformsForEditor() override;

private:
    /** @brief Initialize()の下請け マネージャ取得・各種Common初期化・カメラ生成を行う */
    void InitializeCoreSystems();
    /** @brief Initialize()の下請け 境界ブロック・ワープポータル・ダミー用モデル・パーティクル群を初期化する */
    void InitializeStageModels();
    /** @brief Initialize()の下請け ナイト敵と訓練マネキン1体を生成する */
    void InitializeDummyEnemies();
    /** @brief Initialize()の下請け プレイヤーと弾丸プールを初期化する */
    void InitializePlayerAndBullets();
    /** @brief Initialize()の下請け HUD（覚醒ゲージ・武器スロット・スタイルメーター・フォント等）を初期化する */
    void InitializeHud();
    /** @brief Initialize()の下請け 画面演出エフェクト（ガラス割れ・刃・空間歪み・切断・世界割れ）を初期化する */
    void InitializeEffects();

    // 訓練用マネキン（動かない敵）
    struct Dummy {
        std::unique_ptr<Object3d> object;
        Vector3 pos;
        Vector3 homePos;
        float hp;
        float maxHp;
        float hitFlash = 0.0f; // 被弾時の白フラッシュタイマー
        float hpDisplay = 1.0f; // HP バー表示用（0→1 に回復する演出値）
        float knockVelX = 0.0f;
        float knockVelY = 0.0f;
        float returnTimer = 0.0f; // 最後に被弾してからの秒数（超えたら中央へ戻る）
        bool sliced = false; // 切断演出中は本体モデルを非表示にする

        // HP バー用スプライト
        std::unique_ptr<Sprite> hpBarBg;
        std::unique_ptr<Sprite> hpBarFg;
    };

    /** @brief 指定座標にヒットエフェクト（パーティクル）を生成する */
    void SpawnHitEffect(const Vector3& pos);
    /** @brief 全マネキンのHPバースプライトを現在HPに合わせて更新する */
    void UpdateHpBars();
    /** @brief 有効なポストエフェクトに応じたオフスクリーンRTV（未使用時はバックバッファ）を返す */
    D3D12_CPU_DESCRIPTOR_HANDLE GetActiveRTVHandle() const;
    /** @brief メイン描画先（GetActiveRTVHandle）とビューポート/シザーを設定する */
    void SetupMainRenderTarget();

    /** @brief プレイヤーを更新し、カメラをプレイヤーに追従させる */
    void UpdatePlayerAndCamera();
    /** @brief 影・境界ブロック・ワープポータルの演出を更新する */
    void UpdateEnvironment();
    /** @brief 格闘/射撃/乱舞/弾丸の当たり判定を処理する命中があれば true を返す */
    bool UpdateCombat();
    /** @brief ダミー1体のAABBを返す（当たり判定用） */
    static AABB DummyBounds(const Dummy& d);
    /** @brief 覚醒/アックス怒り状態を反映した現在の攻撃力倍率を返す */
    float ComputeAttackMult() const;
    /** @brief 格闘コンボのヒット判定を処理する命中があれば true を返す */
    bool UpdateMeleeComboHit();
    /** @brief 武器固有技（Space キー）のヒット判定を処理する命中があれば true を返す */
    bool UpdateWeaponSkillHits();
    /** @brief 射撃コンボのヒットスキャン判定を処理する命中があれば true を返す */
    bool UpdateGunShotHit();
    /** @brief 覚醒乱舞のヒット判定を処理する命中があれば true を返す */
    bool UpdateRampageHit();
    /** @brief フィニッシャースラッシュの発動演出をトリガーする */
    void TriggerFinisherSlash();
    /** @brief スペースキー スピン連射の発射処理を行う */
    void UpdateSpinShotFire();
    /** @brief 弾丸の移動と衝突判定を処理する命中があれば true を返す */
    bool UpdateBulletHits();
    /** @brief フィニッシャースラッシュ演出（斬撃線を1本ずつ表示→本命ヒット）を更新する本命ヒットの瞬間なら true を返す */
    bool UpdateFinisherSlash();
    /** @brief UpdateFinisherSlash()の下請け 斬撃線を1本表示し、全マネキンに拘束ヒットを与える */
    void UpdateFinisherSlashLine();
    /** @brief UpdateFinisherSlash()の下請け 解放の瞬間に距離を問わず全マネキンへヒットを適用する */
    void ApplyFinisherReleaseHits();
    /** @brief UpdateFinisherSlash()の下請け 解放演出（斬撃線・画面フラッシュ・刃の放出・ダミー切断）を再生する */
    void PlayFinisherReleaseEffects();
    /** @brief UpdateFinisherSlash()の下請け プレイヤー位置を画面UVへ投影し世界割れ演出を開始する */
    void StartFinisherShatterImpact();
    /** @brief ダミー1体への近接ヒット処理（ノックバック・打ち上げ・演出・スタイル加点） */
    void ApplyMeleeHitToDummy(Dummy& d, const MeleeAttackDef* atk, float atkMult);
    /** @brief ダミーのノックバック物理とHPバー表示を更新する */
    void UpdateDummies();
    /** @brief ナイト敵への攻撃判定・撃破後の武器奪取入力を処理する */
    void UpdateKnightEnemy();
    /**
     * @brief StageEditorで新規配置したナイト（GetStageEditor().GetKnights()）への攻撃判定
     * @note AI/重力自体はBaseScene::Tick()がUpdate()の後にGetStageEditor().UpdateObjects()で回すので、
     * ここでは当たり判定とTakeDamageだけを行うロックオン・撃破後の武器奪取はknight_専用のまま対応していない
     */
    void UpdatePlacedKnights();
    /** @brief Shiftキーでのロックオン対象の選択・切り替え・自動解除を処理する */
    void UpdateTargetLock();
    /** @brief 武器スロットUIのスプライトを初期化する */
    void InitializeWeaponSlotHud();
    /** @brief 武器スロットUI（枠・アイコン・光る演出）を毎フレーム更新する */
    void UpdateWeaponSlotHud();
    /** @brief 武器スロットUIを描画する */
    void DrawWeaponSlotHud();
    /** @brief HUD全体（武器一覧・操作説明・コンボランク・覚醒ゲージ）を描画する */
    void DrawHud(bool nearReturnPortal);
    /** @brief 武器一覧と戻りポータルのラベルを描画する */
    void DrawWeaponHud(bool nearReturnPortal);
    /** @brief F3で表示切替。プレイヤー/ダミー/ナイト/solidブロックの当たり判定をワイヤーフレームで描く */
    void DrawColliderDebug();

    DirectXCommon* dxCommon_ = nullptr;
    Input* input_ = nullptr;
    Audio* audio_ = nullptr;
    ImGuiManager* imguiManager_ = nullptr;
    SrvManager* srvManager_ = nullptr;
    ParticleManager* pm_ = nullptr;

    GrayscaleEffect* grayscaleEffect_ = nullptr;
    ImageFilter* imageFilter_ = nullptr;
    HsvFilter* hsvFilter_ = nullptr;

    GlassShatterEffect glassShatter_;
    std::unique_ptr<Sprite> glassShatterBgSprite_;

    /** @brief 解放時に暗転+斬撃線ごと凍った画面を砕いて素の世界を見せる演出 */
    GlassShatterEffect finisherShatter_;

    /** @brief 空間に走るガラス質の刃パーティクル */
    BladeFlashEffect bladeFlash_;
    /** @brief プレイヤー中心の空間歪み（レンズ歪み+色収差） */
    SpaceDistortionEffect spaceWarp_;
    /** @brief 解放時にダミーを切断破片へ差し替える演出 */
    MeshSliceEffect dummySlice_;

    std::unique_ptr<SpriteCommon> spriteCommon_;
    std::unique_ptr<ModelCommon> modelCommon_;
    std::unique_ptr<Object3dCommon> objectCommon_;
    std::unique_ptr<ShadowManager> shadowManager_;
    std::unique_ptr<Camera> camera_;

    // 境界ブロック（level01.json から読み込む。本番ステージと共通の形状）
    std::unique_ptr<Model> modelBlock_;
    std::unique_ptr<Model> cityBackgroundModel_;
    std::vector<std::unique_ptr<Object3d>> cityBackgroundObjects_;

    // ワープポータル（トレーニングルームへ戻る）
    std::vector<std::unique_ptr<Object3d>> warpPortalBlocks_;

    // プレイヤー
    std::unique_ptr<Player> player_;

    // 訓練マネキン
    std::unique_ptr<Model> modelDummy_;
    std::vector<Dummy> dummies_;

    // 剣を持つナイト敵（撃破→凍結→武器奪取のお試し実装）
    std::unique_ptr<KnightEnemy> knight_;

    // ロックオン（Shiftキーで生存中の敵を巡回選択乱舞/コンボの誘導先に使う）
    enum class LockTargetKind { None,
        Dummy,
        Knight };
    LockTargetKind lockedKind_ = LockTargetKind::None;
    size_t lockedDummyIndex_ = 0;

    // 武器
    WeaponManager* weaponManager_ = nullptr;
    float weaponCycleTimer_ = 0.0f;

    // 弾丸
    BulletPool bulletPool_;

    // ワープ演出
    float warpPulseTimer_ = 0.0f;

    // スタイリッシュランク（右上表示。実際に命中した時だけ加点、同じ技の連発は減衰）
    StyleMeter styleMeter_;

    // 覚醒ゲージ UI
    std::unique_ptr<Sprite> awakenGaugeBg_;
    std::unique_ptr<Sprite> awakenGaugeFg_;

    // 武器スロットUI（画面左下。使用中の枠が光る。テストシーンなので全武器ぶん並べる）
    struct WeaponSlotUI {
        std::unique_ptr<Sprite> frame; // 枠背景
        std::unique_ptr<Sprite> icon; // スタイルカラーで塗った中身
    };
    static constexpr int kWeaponSlotCount = 7;
    static constexpr float kSlotFlashDuration = 0.35f;
    std::array<WeaponSlotUI, kWeaponSlotCount> weaponSlots_;
    std::array<Vector2, kWeaponSlotCount> weaponSlotPos_;

    // 各スロットは色付き四角の代わりに実物の3Dモデルをゆっくり回転させて表示する
    // カメラは回転しないため、カメラ位置からのワールドオフセットで画面左下に固定表示する
    struct WeaponIcon3D {
        std::unique_ptr<Model> model;
        std::unique_ptr<Object3d> object;
        int slotIndex = -1; // weaponManager_ のリスト内で対応する武器が何番目か（無ければ-1）
        float wobbleTime = 0.0f; // 揺れのタイマー（フルスピンだと必ず背面を向く瞬間が来るので往復にする）
        float scale = 0.2f; // モデルごとの実寸差を吸収し、見た目のアイコンサイズを揃える倍率
        float baseYaw = 0.0f; // モデルの正面がカメラを向くよう調整する基準角度（要目視調整）
    };
    std::array<WeaponIcon3D, kWeaponSlotCount> weaponIcons3D_;

    float slotPulseTimer_ = 0.0f; // 使用中スロットの明滅位相
    float slotFlashTimer_ = 0.0f; // 武器奪取時に全スロットをパッと光らせる残り時間

    // 常時装備の拳銃アイコン（4スロットとは別枠、アイドル時にゆっくり回転する）
    std::unique_ptr<Sprite> gunFrame_;
    std::unique_ptr<Sprite> gunIcon_;
    Vector2 gunPos_ = { };
    float gunIconAngle_ = 0.0f;

    // フィニッシャースラッシュ演出の進行状態
    bool finisherActive_ = false;
    int finisherLineIdx_ = 0;
    float finisherBeatTimer_ = 0.0f;

    /** @brief 大技演出中の画面暗転オーバーレイ */
    std::unique_ptr<Sprite> finisherOverlay_;

    // F3で切り替える当たり判定デバッグ表示（ステージエディタの表示状態とは独立）
    bool showColliders_ = false;

    FontRenderer fontRenderer_;
};

} // namespace engine::game
