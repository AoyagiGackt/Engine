/**
 * @file MapScene.h
 * @brief ローグライトのフロア選択マップシーンを定義するファイル
 */
#pragma once
#include "Audio.h"
#include "BaseScene.h"
#include "Camera.h"
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
    /** @brief ImGui マネージャーを設定する */
    void SetImGuiManager(ImGuiManager* imgui) override { imguiManager_ = imgui; }

    // 床と背景のビルは Resources/Levels/map.json に置き、F2のステージエディタで編集する
    std::string GetEditorLevelPath() const override { return "Resources/Levels/map.json"; }
    engine::graphics::ModelCommon* GetEditorModelCommon() override { return modelCommon_.get(); }
    engine::graphics::Camera* GetEditorCamera() override { return camera_.get(); }
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

    /** @brief Draw()の下請け 街並み・地面ブロックのシャドウマップパスを描画する */
    void DrawShadowPass();
    /** @brief Draw()の下請け 背景・街並み・地面ブロック・入口ポータル・プレイヤーを3D描画する */
    void DrawWorld();
    /** @brief Draw()の下請け 各ステージ入口ポータルの上に番号ラベルとENTER案内を描画する */
    void DrawStagePortalLabels(int floor);
    /** @brief 選択中ノードの説明パネル（右側）を描画する */
    void DrawSelectedNodeInfo(int curFloor, RunData::NodeType hoveredNode);

    DirectXCommon* dxCommon_ = nullptr;
    Input* input_ = nullptr;
    Audio* audio_ = nullptr;
    ImGuiManager* imguiManager_ = nullptr;

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

    // マップ定義  floors_[floor][col] = NodeType
    std::vector<std::vector<RunData::NodeType>> floors_;

    // 選択状態
    int selectedCol_ = 0;
};

} // namespace engine::game
