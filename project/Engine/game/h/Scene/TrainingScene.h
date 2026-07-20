/**
 * @file TrainingScene.h
 * @brief アクション操作を自由に練習できるトレーニングシーンを定義するファイル
 */
#pragma once
#include <memory>
#include <vector>

#include "Audio.h"
#include "BaseScene.h"
#include "BulletPool.h"
#include "Camera.h"
#include "DirectXCommon.h"
#include "FontRenderer.h"
#include "GpuProfiler.h"
#include "ImGuiManager.h"
#include "Input.h"
#include "Model.h"
#include "ModelCommon.h"
#include "Object3d.h"
#include "Object3dCommon.h"
#include "Player.h"
#include "SceneShared.h"
#include "ShadowManager.h"
#include "Sprite.h"
#include "SpriteCommon.h"
#include "SrvManager.h"
#include "WeaponManager.h"
#ifdef _DEBUG
#include "GraphRuntime.h"
#include "GraphTypes.h"
#endif
namespace engine::game {
using engine::Audio;
using engine::DirectXCommon;
using engine::Input;
using engine::graphics::Camera;
using engine::graphics::GpuProfiler;
using engine::graphics::ImGuiManager;
using engine::graphics::Model;
using engine::graphics::ModelCommon;
using engine::graphics::Object3d;
using engine::graphics::Object3dCommon;
using engine::graphics::ShadowManager;
using engine::graphics::Sprite;
using engine::graphics::SpriteCommon;
using engine::graphics::SrvManager;

/**
 * @brief アクション操作の練習用シーン
 * @note ランデータを消費せずにプレイヤー操作を試せる
 * Backspace でタイトルまたはマップシーンへ戻る
 */
class TrainingScene : public BaseScene {
public:
    /** @brief シーンの初期化プレイヤー・ステージ・UI を構築する */
    void Initialize(DirectXCommon* dxCommon, Input* input, Audio* audio) override;
    /** @brief リソースを解放する */
    void Finalize() override;
    /** @brief 入力・物理・パーティクルを毎フレーム更新する */
    void Update() override;
    /** @brief ステージとプレイヤーを描画する */
    void Draw() override;
    /** @brief ImGui マネージャーを設定する */
    void SetImGuiManager(ImGuiManager* imgui) override { imguiManager_ = imgui; }

    /** @brief StageEditorのトリガー判定用（SceneManagerが毎フレーム参照する） */
    Vector3 GetEditorPlayerPos() const override { return player_ ? player_->GetPosition() : Vector3 { }; }

    // BaseScene::Init()/Tick()からのStageEditor自動配線フック
    std::string GetEditorLevelPath() const override { return "Resources/Levels/training.json"; }
    ModelCommon* GetEditorModelCommon() override { return modelCommon_.get(); }
    Camera* GetEditorCamera() override { return camera_.get(); }
    Vector3* GetEditorPlayerPositionRef() override { return player_ ? &player_->GetPositionRef() : nullptr; }
    /** @brief エディタ表示中（ゲームプレイ停止中）にプレイヤーの見た目だけ追従させる */
    void RefreshVisualTransformsForEditor() override;

private:
    /** @brief プレイヤーとスピン連射弾を更新する */
    void UpdatePlayerAndBullets();
    /** @brief カメラ追従、影、境界ブロック、PBRデモ、ワープポータル演出を更新する */
    void UpdateCameraAndEnvironment();
    /** @brief HUD全体（武器一覧・デバッグ情報・操作説明・覚醒ゲージ）を描画する */
    void DrawHud(bool nearWarpPortal);
    /** @brief 武器一覧とワープポータルのラベルを描画する */
    void DrawWeaponHud(bool nearWarpPortal);
    /** @brief デバッグ情報（FPS、PBRマテリアルエディタ、プロファイラ）を描画する */
    void DrawDebugHud();

    DirectXCommon* dxCommon_ = nullptr;
    Input* input_ = nullptr;
    Audio* audio_ = nullptr;
    ImGuiManager* imguiManager_ = nullptr;
    SrvManager* srvManager_ = nullptr;

    std::unique_ptr<SpriteCommon> spriteCommon_;
    std::unique_ptr<ModelCommon> modelCommon_;
    std::unique_ptr<Object3dCommon> objectCommon_;
    std::unique_ptr<ShadowManager> shadowManager_;
    std::unique_ptr<Camera> camera_;

    // 境界ブロック
    std::unique_ptr<Model> modelBlock_;
    std::vector<std::unique_ptr<Object3d>> borderBlocks_;
    std::unique_ptr<Model> cityBackgroundModel_;
    std::vector<std::unique_ptr<Object3d>> cityBackgroundObjects_;

    // ワープポータル（テストステージへ）
    std::vector<std::unique_ptr<Object3d>> warpPortalBlocks_;

    // プレイヤー
    std::unique_ptr<Player> player_;

    // 弾丸（スピン連射）
    BulletPool bulletPool_;

    // 武器選択
    WeaponManager* weaponManager_ = nullptr;
    float weaponCycleTimer_ = 0.0f;

    // ワープ演出タイマー（近づいたら点滅）
    float warpPulseTimer_ = 0.0f;

    // 覚醒ゲージ UI
    std::unique_ptr<Sprite> awakenGaugeBg_;
    std::unique_ptr<Sprite> awakenGaugeFg_;

    FontRenderer fontRenderer_;

#ifdef _DEBUG
    // ビジュアルスクリプティングVMの動作確認用（Resources/Graphs/test_graph.jsonを読み込んで実行する）
    // エディタ本体（imgui-node-editor等でグラフを組むUI）はまだ無く、VM単体の動作検証用
    GraphDesc testGraph_;
    GraphRuntime testGraphRuntime_;
#endif
};

} // namespace engine::game
