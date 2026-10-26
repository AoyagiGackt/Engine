/**
 * @file GamePlayScene.h
 * @brief メインの戦闘シーン（ローグライト／サンドボックス両対応）
 */
#pragma once
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
#include "ComboFeelDirector.h"
#include "ShotTargetPicker.h"
#include "CameraShaker.h"
#include "Collision.h"
#include "ElementEffect.h"
#include "EnemyEntity.h"
#include "EnemyRegistry.h"
#include "FontRenderer.h"
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
#include "ClearFinish.h"
#include "DebugButton.h"
#include "PlayerDefeat.h"
#include "StageIntro.h"
#include "ResultBackdrop.h"
#include "StyleMeter.h"
#include "TimeManager.h"
#include "ComboMotionEditor.h"
#include "PauseMenu.h"
#include "UIMenu.h"
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
     * @brief ポストエフェクト対応の有無を返す
     * @return 常にtrue
     */
    bool SupportsPostEffects() const override {
        return true;
    }

    /** @brief エディタから操作するプレイヤーを返す */
    Player* GetEditorPlayer() const override {
        return player_.get();
    }

    /**
     * @brief ステージエディタが読み書きするレベルファイルを返す
     * @return レベルJSONのパス（game_rules.jsonのlevelPathsから現在フロアに対応するもの）
     */
    std::string GetEditorLevelPath() const override;
    /**
     * @brief ステージ配置物の生成に使用するモデル共通処理を返す
     * @return シーンが所有するモデル共通処理
     */
    ModelCommon* GetEditorModelCommon() override {
        return modelCommon_.get();
    }
    /**
     * @brief ステージエディタの表示と操作に使用するカメラを返す
     * @return シーンが所有するカメラ
     */
    Camera* GetEditorCamera() override {
        return camera_.get();
    }
    /**
     * @brief エディタ配置敵の演出に使用するパーティクル管理を返す
     * @return 共有パーティクル管理
     */
    ParticleManager* GetEditorParticleManager() override {
        return pm_;
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

    /** @brief クリアの決め演出を確認用に始める（剣か銃かはランダム。ランの結果やセーブには反映しない） */
    void TriggerGlassShatterTest();
    /** @brief デバッグ用にその場でクリアする（進行度を進め、マップで解放演出を見せる） */
    void TriggerDebugClear();
    /** @brief ボスを倒さずにクリアの決め演出を始める（previewOnly なら進行とセーブに反映しない） */
    void StartDebugClear(bool previewOnly);

    /**
     * @brief 追加ホットキーの案内文字列を返す
     * @return シーン調整パネルのホットキー文字列
     */
    const char* GetHotkeyOverlayExtra() const override {
        return "F3: シーン調整パネル\nF6: コンボ演出の調整\nF7: モーションエディタ";
    }

private:
    static constexpr const char* kSceneName = "GAMEPLAY"; ///< scene_flow.json のキー（SceneFactory の登録名と同じ）

    /** @brief シーンが所有するゲーム実体、描画資源、演出を責務順に初期化する */
    void InitializeCoreSystems();
    /** @brief InitializeCoreSystems()の下請け 描画基盤(Common類・シャドウ・カメラ)とスカイドームを初期化する */
    void InitializeRenderFoundation();
    /** @brief InitializeCoreSystems()の下請け レンダーテクスチャとクリア演出用オーバーレイスプライトを初期化する */
    void InitializeRenderTargetsAndOverlays();
    /** @brief InitializeCoreSystems()の下請け パーティクル・水面・武器スロットHUDを初期化する */
    void InitializeParticlesAndHud();
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
    /** @brief 一時停止メニューの初期化（退出項目はステージセレクトへ戻る） */
    void SetupPauseMenu();
    /** @brief 道中の武器敵を更新し、攻撃と武器奪取を処理する */
    void UpdateWeaponEnemies();
    /** @brief 武器を奪った結果に応じた後処理（新しく増えたら通知、持っている武器なら覚醒ゲージへ） */
    void ApplyWeaponAcquireResult(WeaponManager::AcquireResult result);
    /** @brief レベルに配置された壊せる物（kind=="breakable"）のヒット判定・爆発ダメージ・演出を処理する */
    void UpdateExplosiveBarrels();
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
    /** @brief いまの描画先がバックバッファか（ポストエフェクトを使わずに直接画面へ描いているか） */
    bool IsDrawingToBackBuffer() const;
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
    /** @brief 当たり判定ブロック全体を囲むステージの範囲 */
    struct StageBounds {
        float left;
        float right;
        float bottom;
        float top;
        bool fromLevel; ///< レベルのブロックから求めたか（false ならブロックが無く既定の広さ）
    };
    StageBounds GetStageBounds();
    /** @brief ステージ開始演出を始める（最初にレベルを読み込んだ時に1回） */
    void StartStageIntro();
    /** @brief 開始演出の間の1フレーム（操作・HUD・敵を止め、カメラだけ演出に合わせて動かす） */
    void UpdateStageIntro();
    /** @brief プレイヤーのやられ演出を始める（ラン中にHPが尽きた時に1回） */
    void StartPlayerDefeat();
    /** @brief やられ演出の間の1フレーム（操作・HUD・敵を止め、カメラだけ演出に合わせて動かし、見せ終えたら選ばれた行き先（同じステージ／マップ）へ） */
    void UpdatePlayerDefeat();
    /** @brief 敵（ボスと道中の武器敵）の見た目の行列だけを今のカメラで計算し直す */
    void RefreshEnemyVisualTransforms();
    /** @brief 立っている敵（倒れた敵と消えた敵は除く）の位置を集める */
    std::vector<Vector3> CollectStandingEnemyPositions() const;
    /** @brief カメラに映っている、まだ倒れていない敵（ステージの主の敵と道中の敵）を集める（フィニッシャーの当たる相手） */
    std::vector<EnemyEntity*> CollectOnScreenEnemies() const;
    /** @brief クリアの決め演出を始める（ボスを倒した最後の一撃が銃なら弾痕、それ以外なら斬線） */
    void StartClearFinish();
    /** @brief クリアの結果画面（黒い背景に CLEAR!。withRunResult ならランク・獲得ゴールド・スコアも）を描く */
    void DrawClearResultScreen(bool withRunResult);
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
    /**
     * @brief 敵の当たり判定AABBを指定位置から算出する（ボス・道中の敵で共通の大きさ）
     * @param position 敵の位置
     */
    static AABB EnemyHitBoxAt(const Vector3& position);
    /**
     * @brief 空中の近接判定を、上下は中心から指定の高さに、左右は指定の幅だけ広げる（高さが合わずに空振りするのを減らす）
     * @param range 広げる近接判定
     * @param center プレイヤーの位置
     * @param halfHeight 上下に取る高さ（中心から片側）
     * @param widthPadding 左右に足す幅（片側）
     */
    static void WidenAirMeleeRange(AABB& range, const Vector3& center, float halfHeight, float widthPadding);
    /**
     * @brief 固有技の判定範囲（プレイヤーを中心にした横長の箱）を返す
     * @param center プレイヤーの位置
     * @param radius 左右の半径
     */
    static AABB SkillRangeAround(const Vector3& center, float radius);
    /** @brief UpdateStyleAndUI()の下請け 近接コンボのヒット判定とスタイル加点・属性追撃を処理する */
    void ApplyMeleeComboStyleHit(const AABB& enemyAABB);
    /** @brief UpdateStyleAndUI()の下請け 武器固有技（SPACE）のヒット判定とスタイル加点を処理する */
    void ApplyWeaponSkillStyleHit(const AABB& enemyAABB);
    /** @brief UpdateStyleAndUI()の下請け 銃コンボのヒット判定とスタイル加点を処理する */
    void ApplyGunShotStyleHit();
    /** @brief UpdateStyleAndUI()の下請け 覚醒乱舞ラッシュのヒット判定とスタイル加点を処理する */
    void ApplyRampageStyleHit(const AABB& enemyAABB);
    /** @brief UpdateStyleAndUI()の下請け スタイルメーターの時間経過による減衰を処理する */
    void DecayStyleMeter(float dt);
    /** @brief 現在のスタイルランクが高いほど多く覚醒ゲージを溜める（攻撃ヒット・ジャスト回避用） */
    void ChargeAwakenGaugeByStyle(float amount);
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
    /**
     * @brief 近接ヒットで敵に与える被弾硬直の秒数（次の段が出るまで拘束できる長さ）
     * @param attack ヒットした段（nullptrなら既定値）
     */
    float MeleeHitstunSeconds(const MeleeAttackDef* attack) const;
    /** @brief 銃の疲労の添字（近接武器の後ろ） */
    int GunFatigueIndex() const;
    /** @brief 手に持っている近接武器の疲労の添字（素手なら-1） */
    int HeldMeleeFatigueIndex() const;
    /** @brief 指定した武器の疲労から、今のダメージ倍率を返す（1.0=万全〜weaponFatigueMinDamageMult） */
    float WeaponFatigueDamageMult(int fatigueIndex) const;
    /** @brief このフレームに当てた武器を記録する（疲労の加算はUpdateWeaponFatigue()でまとめて1回だけ行う） */
    void RegisterFatigueHit(int fatigueIndex, float amount);
    /** @brief 当てた武器へ疲労を足し、それ以外の武器の疲労を抜く（ヒット判定の後に毎フレーム呼ぶ） */
    void UpdateWeaponFatigue();
    /** @brief 疲労で効きが落ちた命中の弾けを、威力に合わせて小さく灰色に寄せる */
    void ApplyFatigueToHitEffect(float damageMult, Vector4& color, float& strength) const;
    /** @brief 同じ武器で当て続けて効きが落ちている時に、持ち替えを促す警告を出す */
    void DrawWeaponFatigueWarning();
    /**
     * @brief 近接ヒットのヒットストップとカメラ揺れを要求する（途中の段は控えめ、締めだけ大きく）
     * @param attack        ヒットした段（nullptrなら既定値）
     * @param finisher      コンボの締めか
     * @param defaultFrames 段定義がない時のヒットストップフレーム数
     */
    /**
     * @brief 近接が当たった時の、武器ごとの演出と音（hit_effects.json の weapon_hit_<hitStyle>、締めは weapon_finisher_<hitStyle>）
     * @param position 当たった敵の位置
     * @param finisher 締めの一撃か
     */
    void EmitWeaponStyleHit(const Vector3& position, bool finisher);
    void RequestMeleeHitStopAndShake(const MeleeAttackDef* attack, bool finisher, int defaultFrames);
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
    /** @brief 連続ヒット数に応じた命中演出の盛り度合い（0.0=初撃〜1.0=上限） */
    float HitEscalationRatio() const;
    /** @brief 武器属性ごとに形・運動の異なる追加命中演出を出す */
    void EmitElementalHitEffect(const WeaponData& weapon, const Vector3& enemyPos, int comboStep);
    /**
     * @brief 属性名ごとの追加命中演出を出す（近接・固有技・銃で共通）
     * @param element  属性名（Water/Fire/Lightning/Ice/Gravity/Blood/Void/Wind、それ以外は何も出さない）
     * @param color    属性色
     * @param enemyPos 命中した敵の位置
     * @param scale    演出の大きさ倍率（連続ヒットによる盛りはこの上に掛かる）
     */
    void EmitElementalHitEffect(const std::string& element, const Vector4& color, const Vector3& enemyPos, float scale);
    /** @brief 近接モーション中、武器の先から属性ごとの粒子を零す（火の粉・水滴・冷気など） */
    void EmitWeaponElementAura(const std::string& element, const Vector4& color, const Vector3& weaponPos);
    /** @brief 属性エフェクトに渡す粒子の発生先・乱数・プレイヤーの向きをまとめる */
    ElementEffectContext ElementContext();
    /** @brief 敵が予備動作に入った瞬間に警告リングを出す（攻撃が来ることを事前に伝え、回避を狙えるようにする） */
    void EmitEnemyTelegraphCue(const EnemyEntity* enemy);
    /** @brief UpdateStyleTechniqueParticles()の下請け 銃発射時の弾煙パーティクルを発生させる */
    void EmitGunFireParticles(const Vector3& ppos);
    /** @brief UpdateStyleTechniqueParticles()の下請け 武器固有技（ダガーのスティンガー以外）発動時に技ごとの見た目を出す */
    void EmitWeaponSkillCastParticles(const Vector3& ppos);
    /**
     * @brief 固有技が決まった瞬間の共通演出（閃光・停止・揺れ・刃の明滅・大きな弾け）を当たり外れに関係なく出す
     * @param center    演出の中心（技の着弾点）
     * @param color     武器の属性色
     * @param slashLine 進行方向へ画面を横切る一閃を引くか（居合系の突進技用）
     */
    void EmitWeaponSkillImpact(const Vector3& center, const Vector4& color, bool slashLine);
    /** @brief UpdateStyleTechniqueParticles()の下請け 覚醒中の継続オーラと発動瞬間の衝撃波を発生させる */
    void EmitAwakenParticles(const Vector3& ppos, float dt);
    /** @brief UpdateStyleTechniqueParticles()の下請け styleRankHud_のランクが上がった瞬間にリング・火花・カメラシェイク・画面フラッシュを出す */
    void EmitStyleRankUpParticles(const Vector3& ppos);
    /** @brief 近接コンボのモーション中、装備武器の色で手元にトレイル残像を発生させ続ける */
    void UpdateWeaponTrail();
    /** @brief 刃の軌跡・締め演出・F6調整パネルを更新する @param gameAdvanced このフレームにプレイヤーが動いたか */
    void UpdateComboFeel(bool gameAdvanced);
    /** @brief 撃ったフレームに、ボス・道中の敵・壊せる物から銃口に一番近い1体を射撃の的に選ぶ */
    void BuildGunShotTarget();
    // フィニッシャースラッシュ演出（斬撃線を1本ずつ表示→本命ヒット）の更新
    void UpdateFinisherSlash(float dt);
    // 敵撃破などのクリア条件判定
    void CheckClearCondition();

    /** @brief ラン外（エディタのテストプレイなど）のクリアか（結果画面は CLEAR! だけにして、終わったらクリア画面へ） */
    bool IsSandboxClear() const;
    /** @brief ラン中のクリアの結果（ランク・ゴールド・スコア）を求め、apply なら進行とセーブにも反映する（確認用の再生は false） */
    void RecordRunClear(bool apply);

    /** @brief Draw()の下請け クリア演出中の専用画面（ローグライト結果表示／サンドボックスのガラス割れ導入）を描画する。描画してDraw()を打ち切るべきなら true を返す */
    bool DrawClearOverlayIfNeeded();
    /** @brief Draw()の下請け 3Dワールド（地形・残像・プレイヤー/敵・パーティクル・空間歪み）を描画する */
    void DrawWorldAndActors();
    /** @brief Draw()の下請け エディタUI・フィニッシャー演出・フォントなど2D上乗せ描画をまとめて行う */
    void DrawOverlaysAndUI();
    /** @brief HPバー・ランク・武器欄・エディタ配置UIなどHUDのスプライトを描く（一時停止中は呼ばない） */
    void DrawHudSprites();

    DirectXCommon* dxCommon_ = nullptr;
    Input* input_ = nullptr;
    Audio* audio_ = nullptr;

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

    std::unique_ptr<Player> player_;
    // ボス敵。実体はStageEditorの配置物(kind=="enemy_basic", isStageBoss=true)が所有し、
    // OnEditorLevelLoaded()でポインタだけを受け取る（非所有）
    EnemyEntity* enemy_ = nullptr;
    WeaponType bossWeaponType_ = WeaponType::kSword; ///< ボスの配置物に設定された武器種別（撃破後にこれを奪う）
    // ボス頭上のHPバー（BattleTestSceneのDummy::hpBarBg/Fgと同じ体裁、GamePlayScene初期化時に一度だけ生成）
    bool hudReady_ = false; ///< 通常HUDの位置と色を更新し終えたか（済むまで描画しない）
    std::unique_ptr<Sprite> bossHpBarBg_;
    std::unique_ptr<Sprite> bossHpBarFg_;

    // プレイヤーHPゲージ（左下、数値表示に加えて一目で残量が分かるようにするバー）
    std::unique_ptr<Sprite> playerHpBarBg_;
    std::unique_ptr<Sprite> playerHpBarFg_;

    /** @brief 道中に配置された武器持ち敵1体分の状態（撃破後、Jキーで吸収して武器を奪取する） */
    struct WeaponEnemyEntry {
        EnemyEntity* enemy = nullptr; // 非所有。StageEditorの配置物が実体を所有する
        WeaponType weaponType = WeaponType::kSword;
        bool hasWeapon = true; ///< falseなら武器を持たないモンスター（倒すと消えて覚醒ゲージになる）
        float vanishTimer = 0.0f; ///< モンスターが倒れてから消えるまでの残り秒数
        bool weaponAcquired = false; ///< 武器を奪い終えた（モンスターは消え終えた）
        bool absorbing = false;
        bool defeatEffectEmitted = false;
        float absorbTimer = 0.0f;
        std::unique_ptr<Sprite> hpBarBg; // OnEditorLevelLoaded()で生成（レベル再読込のたびに作り直す）
        std::unique_ptr<Sprite> hpBarFg;
    };
    std::vector<WeaponEnemyEntry> weaponEnemies_;

    /** @brief 道中の武器敵1体への命中1回ぶんの内訳（何で当てたかと、そこから決まる反応・演出の強さ） */
    struct WeaponEnemyHit {
        bool hit = false; ///< 何かが当たった
        bool melee = false; ///< 近接コンボで当てた
        bool gun = false; ///< 銃（スピン連射を含む）で当てた
        bool skill = false; ///< 武器固有技で当てた
        bool fired = false; ///< このフレームに撃った弾で当てた
        bool extraHit = false; ///< 覚醒中の多段ヒットの2発目以降
        bool comboFinisher = false; ///< コンボの締めの段
        bool blowAway = false; ///< 大きく吹き飛ばす
        const MeleeAttackDef* attack = nullptr; ///< 当てた近接の段（無ければ nullptr）
        float fatigueMult = 1.0f; ///< 武器の疲労によるダメージ倍率
    };
    /**
     * @brief 道中の武器敵に、このフレームの近接・射撃・固有技のどれが当たったかを調べる
     * @param entry 調べる武器敵
     * @param enemyBounds 武器敵の当たり判定
     */
    WeaponEnemyHit DetectWeaponEnemyHit(const WeaponEnemyEntry& entry, const AABB& enemyBounds) const;
    /**
     * @brief 命中した武器敵にダメージ・吹き飛び・のけぞりを与え、演出を出す
     * @param entry 当たった武器敵
     * @param hit DetectWeaponEnemyHit() の結果（反応の強さを書き足す）
     */
    void ApplyWeaponEnemyHit(WeaponEnemyEntry& entry, WeaponEnemyHit& hit);
    /** @brief 命中時のダメージ量を武器・段・固有技から計算する */
    int ComputeWeaponEnemyDamage(const WeaponEnemyHit& hit) const;
    /**
     * @brief 命中時のパーティクル・属性演出・ヒットストップを出す
     * @param enemyPos 当たった敵の位置
     * @param hit 命中の内訳
     */
    void EmitWeaponEnemyHitEffects(const Vector3& enemyPos, const WeaponEnemyHit& hit);
    /** @brief 武器を持たないモンスターが倒れたら、少し待って弾けて消し、覚醒ゲージにする */
    void UpdateMonsterVanish(WeaponEnemyEntry& entry);
    /** @brief 倒れた武器持ち敵の武器を、奪取ボタンで吸い寄せて手に入れる */
    void UpdateWeaponSteal(WeaponEnemyEntry& entry);

    /** @brief ロックオン中の敵（ボスも道中の敵も同じ扱い。ロックオン長押し中は最寄りの敵を自動追従し、離すとnullptr） */
    const EnemyEntity* lockedEnemy_ = nullptr;

    // 収集物（pickup）と壊せる物（breakable）はレベルJSONの配置物としてStageEditorが所有する。
    // ここでは壊せる物の脈動表示に使うタイマーだけを持つ
    float explosiveBarrelPulse_ = 0.0f;


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

    /** @brief 覚醒中の残像トレイル1コマ分の位置と経過秒数（設定された寿命を超えたら消える） */
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
    StageIntro stageIntro_; ///< ステージ開始演出（Resources/Config/stage_intro.json）
    bool stageIntroStarted_ = false; ///< エディタでレベルを読み直しても演出をやり直さないための印
    PlayerDefeat playerDefeat_; ///< プレイヤーのやられ演出（Resources/Config/player_defeat.json）
    DebugButton debugGameOverButton_; ///< やられ演出をすぐ見る確認用ボタン（game_rules.json の debugGameOverButton で出し入れ）
    DebugButton debugClearButton_; ///< クリアの決め演出をすぐ見る確認用ボタン（game_rules.json の debugClearButton で出し入れ）
    int lastTechniqueId_ = -1;
    int repeatedTechniqueCount_ = 0;
    /** @brief 武器ごとの疲労（0〜1）。添字は近接武器のWeaponManager::GetList()順、末尾が銃 */
    std::vector<float> weaponFatigue_;
    int lastFatigueIndex_ = -1; ///< 最後に当てた武器の添字（この武器だけは疲労が抜けない）
    int pendingFatigueIndex_ = -1; ///< このフレームに当てた武器の添字（複数の敵に同時に当てても1回ぶんにする）
    float pendingFatigueAmount_ = 0.0f;
    float fatigueIdleTimer_ = 0.0f; ///< 最後に何かへ当ててからの経過秒数
    /** @brief 途切れずに当て続けたヒット数（雑魚・ボス・銃・近接を問わない。命中演出を段々派手にする） */
    int hitEscalationCount_ = 0;
    float hitEscalationTimer_ = 0.0f; ///< 連続ヒットが途切れるまでの残り秒数

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
    /** @brief 近接コンボの手応え演出（刃の軌跡・締めの重さ・振りの向きの斬撃線、F6で調整） */
    ComboFeelDirector comboFeel_;
    /** @brief 射撃が当たる相手（銃口に一番近い1体。弾が敵を貫通しないようにする） */
    ShotTargetPicker gunShotPicker_;

    /** @brief 敵中心の空間歪み（レンズ歪み+色収差） */
    SpaceDistortionEffect spaceWarp_;

    bool showResult_ = false; ///< フロアクリアの結果（ゴールド・スコア・セーブ）を反映し終えたか
    int lastGold_ = 0;
    int lastScore_ = 0; ///< 直近のフロアクリアで加算したスコア（結果表示用）

    FontRenderer fontRenderer_;
    CameraShaker cameraShaker_;

    GlassShatterEffect glassShatter_;

    /** @brief 解放時に暗転+斬撃線ごと凍った画面を砕いて素の世界を見せる演出 */
    GlassShatterEffect finisherShatter_;

    bool clearTriggered_ = false;
    bool weaponStealTriggered_ = false;
    bool pendingWeaponMandatory_ = false; ///< 交換待ちの武器が破棄できないボス武器か
    bool mainEnemyDefeatEffectEmitted_ = false;
    bool mainWeaponAbsorbing_ = false;
    float mainWeaponAbsorbTimer_ = 0.0f;
    bool requestClear_ = false;
    bool glassShatterDebugTest_ = false; ///< 確認用の再生か（結果は見せるが、進行とセーブに反映しない）
    bool bossFinishedByGun_ = false; ///< ボスを倒した最後の一撃が銃だったか（クリアの決め演出の見せ方を変える）
    ClearFinish clearFinish_; ///< クリアの決め演出（Resources/Config/clear_finish.json）

    ResultBackdrop resultBackdrop_; ///< クリアの結果画面の背景（ランクの色の帯・透かし・漂う破片）

    // 一時停止メニュー（Pauseキーで開閉、ゲームプレイ中限定。クリア演出中・武器交換中は開けない）
    PauseMenu pauseMenu_;
    ComboMotionEditor motionEditor_; ///< F7 で開く、コンボの段ごとのモーションの調整（開いている間はゲームを止める）
};

} // namespace engine::game
