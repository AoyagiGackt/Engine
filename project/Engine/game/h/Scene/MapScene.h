/**
 * @file MapScene.h
 * @brief ローグライトのフロア選択マップシーンを定義するファイル
 */
#pragma once
#include "Audio.h"
#include "BaseScene.h"
#include "Camera.h"
#include "DebugButton.h"
#include "DirectXCommon.h"
#include "FontRenderer.h"
#include "ImGuiManager.h"
#include "Input.h"
#include "Model.h"
#include "ModelCommon.h"
#include "Object3d.h"
#include "Object3dCommon.h"
#include "Player.h"
#include "RunData.h"
#include "ShadowManager.h"
#include "Sprite.h"
#include "SpriteCommon.h"
#include "StageLiberation.h"
#include <memory>
#include <vector>
namespace engine::game {
using engine::Audio;
using engine::DirectXCommon;
using engine::Input;
using engine::graphics::Camera;
using engine::graphics::ImGuiManager;
using engine::graphics::Model;
using engine::graphics::ModelCommon;
using engine::graphics::Object3d;
using engine::graphics::Object3dCommon;
using engine::graphics::ShadowManager;
using engine::graphics::Sprite;
using engine::graphics::SpriteCommon;

/**
 * @brief プレイヤーを動かして入口を選ぶステージハブシーン
 */
class MapScene : public BaseScene {
public:
    /** @brief シーンの初期化スプライト・フォント・マップ定義を構築する */
    void Initialize(DirectXCommon* dxCommon, Input* input, Audio* audio) override;
    /** @brief リソースを解放する */
    void Finalize() override;
    /** @brief 入力によるノード選択と遷移判定を更新する */
    void Update() override;
    /** @brief マップノードと現在選択状態を描画する */
    void Draw() override;

    // 床と背景のビルは Resources/Levels/map.json に置き、F2のステージエディタで編集する
    std::string GetEditorLevelPath() const override {
        return "Resources/Levels/map.json";
    }
    /** @brief エディタで使うモデルの共通設定を返す */
    engine::graphics::ModelCommon* GetEditorModelCommon() override {
        return modelCommon_.get();
    }
    /** @brief エディタで使うカメラを返す */
    engine::graphics::Camera* GetEditorCamera() override {
        return camera_.get();
    }
    /** @brief エディタ表示中（移動停止中）もカメラ移動に合わせてプレイヤーと入口ポータルの行列だけ更新する */
    void RefreshVisualTransformsForEditor() override;

private:
    /** @brief Initialize()の下請け 背景・ノード・地面のUIスプライトを初期化する */
    void InitializeUiSprites();
    /** @brief Initialize()の下請け 描画基盤(Common類・シャドウ・カメラ)とプレイヤーを初期化する */
    void InitializeRenderFoundationAndPlayer();
    /** @brief Initialize()の下請け 地面ブロック・ステージ入口ポータル・街並み背景を初期化する */
    void InitializeStageObjects();
    /** @brief Initialize()の下請け フロア構成を組み、現在のフロアに応じた開始位置へプレイヤーを置く(クリア済みならCLEARへ遷移) */
    void InitializeFloorsAndStartPosition();
    /** @brief Initialize()の下請け ステージをクリアして戻ってきた時だけ、クリアした入口の解放演出を始める */
    void StartLiberationIfPending();
    /** @brief クリアした入口の番号を渡して解放演出を始める（次の入口が無ければ光るだけ） */
    void StartLiberation(int clearedIndex);

    /** @brief Update()の下請け 入力でプレイヤーを動かし、通路の端で止めた位置を返す */
    Vector3 MovePlayer();
    /** @brief Update()の下請け 解放演出を進め、その間はプレイヤーをその場で待機させた位置を返す */
    Vector3 HoldPlayerForLiberation();
    /** @brief Update()の下請け 入口の位置・大きさ・色を更新する（選択中・クリア済み・まだ行けない入口、解放演出の光） */
    void UpdatePortals(int curFloor);

    /** @brief Draw()の下請け 街並み・地面ブロックのシャドウマップパスを描画する */
    void DrawShadowPass();
    /** @brief Draw()の下請け 背景・街並み・地面ブロック・入口ポータル・プレイヤーを3D描画する */
    void DrawWorld();
    /** @brief Draw()の下請け 各ステージ入口ポータルの上に番号ラベルとENTER案内を描画する */
    void DrawStagePortalLabels(int floor);
    /** @brief 操作説明の後ろに暗い板を敷く（明るい背景でも読めるように） */
    void DrawHintPanel(const std::wstring& text, const Vector2& position, float scale);
    /** @brief 選択中ノードの説明パネル（右側）を描画する */
    void DrawSelectedNodeInfo(int curFloor, RunData::NodeType hoveredNode);

    /** @brief 入口の出入りを始める */
    void StartPortalMotion(int index, bool entering);
    /** @brief 入口の出入りを入力なしで進める */
    Vector3 UpdatePortalMotion();
    enum class PortalMotion { kNone, kExiting, kEntering, kTransitioning };
    PortalMotion portalMotion_ = PortalMotion::kNone;
    int motionPortalIndex_ = -1;
    float portalMotionTimer_ = 0.0f;
    Vector3 portalMotionStart_ = {};
    bool playerVisible_ = true;

    DirectXCommon* dxCommon_ = nullptr;
    Input* input_ = nullptr;
    Audio* audio_ = nullptr;

    std::unique_ptr<SpriteCommon> spriteCommon_;
    std::unique_ptr<Sprite> bgSprite_; // 黒背景
    std::unique_ptr<Sprite> nodeSprite_; // 選択中ステージの説明パネル

    std::unique_ptr<ModelCommon> modelCommon_;
    std::unique_ptr<Object3dCommon> objectCommon_;
    std::unique_ptr<ShadowManager> shadowManager_;
    std::unique_ptr<Camera> camera_;
    std::unique_ptr<Player> player_;
    Model* blockModel_ = nullptr; // 入口ポータルの見た目（実体はModelManagerが所有・共有する）
    std::vector<std::unique_ptr<Object3d>> portalObjects_;

    FontRenderer fontRenderer_;
    StageLiberation liberation_; ///< クリアして戻ってきた時の解放演出
    DebugButton debugLiberationButton_; ///< 解放演出をすぐ見る確認用ボタン（game_rules.json の debugLiberationButton で出し入れ）

    // マップ定義  floors_[floor][col] = NodeType
    std::vector<std::vector<RunData::NodeType>> floors_;
    // このマップを開いた時点の入口配置を保持する
    std::vector<float> stageWorldX_;

    // 選択状態
    int selectedCol_ = 0;
};

} // namespace engine::game
