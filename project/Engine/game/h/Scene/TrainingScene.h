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
#include "ImGuiManager.h"
#include "Input.h"
#include "Model.h"
#include "ModelCommon.h"
#include "Object3d.h"
#include "Object3dCommon.h"
#include "Player.h"
#include "ShadowManager.h"
#include "Sprite.h"
#include "SpriteCommon.h"
#include "SrvManager.h"
#include "WeaponManager.h"
#include "GpuProfiler.h"
namespace engine::game {
using engine::Audio;
using engine::graphics::Camera;
using engine::DirectXCommon;
using engine::graphics::ImGuiManager;
using engine::Input;
using engine::graphics::Model;
using engine::graphics::ModelCommon;
using engine::graphics::Object3d;
using engine::graphics::Object3dCommon;
using engine::graphics::ShadowManager;
using engine::graphics::Sprite;
using engine::graphics::SpriteCommon;
using engine::graphics::SrvManager;
using engine::graphics::GpuProfiler;

/**
 * @brief アクション操作の練習用シーン
 * @note ランデータを消費せずにプレイヤー操作を試せる。
 * Backspace でタイトルまたはマップシーンへ戻る
 */
class TrainingScene : public BaseScene {
public:
    /** @brief シーンの初期化。プレイヤー・ステージ・UI を構築する */
    void Initialize(DirectXCommon* dxCommon, Input* input, Audio* audio) override;
    /** @brief リソースを解放する */
    void Finalize() override;
    /** @brief 入力・物理・パーティクルを毎フレーム更新する */
    void Update() override;
    /** @brief ステージとプレイヤーを描画する */
    void Draw() override;
    /** @brief ImGui マネージャーを設定する */
    void SetImGuiManager(ImGuiManager* imgui) override { imguiManager_ = imgui; }

private:
    DirectXCommon* dxCommon_     = nullptr;
    Input*         input_        = nullptr;
    Audio*         audio_        = nullptr;
    ImGuiManager*  imguiManager_ = nullptr;
    SrvManager*    srvManager_   = nullptr;

    std::unique_ptr<SpriteCommon>    spriteCommon_;
    std::unique_ptr<ModelCommon>     modelCommon_;
    std::unique_ptr<Object3dCommon>  objectCommon_;
    std::unique_ptr<ShadowManager>   shadowManager_;
    std::unique_ptr<Camera>          camera_;

    // 境界ブロック
    std::unique_ptr<Model>                  modelBlock_;
    std::vector<std::unique_ptr<Object3d>>  borderBlocks_;

    // ワープポータル（テストステージへ）
    std::vector<std::unique_ptr<Object3d>>  warpPortalBlocks_;

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

    // PBR デモブロック（3 種：非金属 / 鏡面金属 / ラフ金属）
    std::unique_ptr<Object3d> pbrDemoBlocks_[3];
    float pbrMetallic_[3]  = { 0.0f,  0.95f, 0.80f };
    float pbrRoughness_[3] = { 0.90f, 0.05f, 0.60f };
};

} // namespace engine::game
