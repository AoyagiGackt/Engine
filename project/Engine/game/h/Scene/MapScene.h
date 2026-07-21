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

private:
    /** @brief タイトルバーとHP/ゴールドを描画する */
    void DrawHeader(RunData* rd);
    /** @brief フロアごとのマップノードを描画する選択中ノードの種類を返す */
    RunData::NodeType DrawFloorNodes(int curFloor);
    /** @brief 選択中ノードの説明パネル（右側）を描画する */
    void DrawSelectedNodeInfo(int curFloor, RunData::NodeType hoveredNode);
    /** @brief 取得済みスキル一覧を描画する */
    void DrawSkillList(RunData* rd);

    DirectXCommon* dxCommon_ = nullptr;
    Input* input_ = nullptr;
    Audio* audio_ = nullptr;
    ImGuiManager* imguiManager_ = nullptr;

    std::unique_ptr<SpriteCommon> spriteCommon_;
    std::unique_ptr<Sprite> bgSprite_; // 黒背景
    std::unique_ptr<Sprite> nodeSprite_; // ノードボックス（都度色変え）
    std::unique_ptr<Sprite> groundSprite_;

    std::unique_ptr<ModelCommon> modelCommon_;
    std::unique_ptr<Object3dCommon> objectCommon_;
    std::unique_ptr<ShadowManager> shadowManager_;
    std::unique_ptr<Camera> camera_;
    std::unique_ptr<Player> player_;
    std::unique_ptr<Model> blockModel_;
    std::unique_ptr<Model> cityModel_;
    std::vector<std::unique_ptr<Object3d>> groundBlocks_;
    std::vector<std::unique_ptr<Object3d>> portalObjects_;
    std::vector<std::unique_ptr<Object3d>> cityObjects_;

    FontRenderer fontRenderer_;

    // マップ定義  floors_[floor][col] = NodeType
    std::vector<std::vector<RunData::NodeType>> floors_;

    // 選択状態
    int selectedCol_ = 0;
};

} // namespace engine::game
