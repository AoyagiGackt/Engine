/**
 * @file TitleScene.h
 * @brief ゲームのタイトル画面シーンを管理するファイル
 */
#pragma once
#include "FloatingTitle.h"
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
#include "ParticleManager.h"
#include "ShadowManager.h"
#include "Skydome.h"
#include "Sprite.h"
#include "SpriteCommon.h"
#include "UIMenu.h"
#include "WeaponManager.h"
#include "WinApp.h"
#include <memory>
#include <random>
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
using engine::graphics::ParticleManager;
using engine::graphics::ShadowManager;
using engine::graphics::Skydome;
using engine::graphics::Sprite;
using engine::graphics::SpriteCommon;

/**
 * @brief タイトル画面のシーンクラス
 * @note ゲーム起動時に最初に表示されるシーンです
 * ユーザーの入力（スペースキーなど）を検知すると finished_ フラグを立て、
 * シーンマネージャーにシーン遷移を促します
 */
class TitleScene : public BaseScene {
public:
    /**
     * @brief シーンの初期化
     * @param dxCommon DirectX基盤のポインタ
     * @param input 入力管理のポインタ
     * @param audio 音響管理のポインタ
     * @note タイトルロゴのスプライト生成や、タイトルBGMの再生開始などを行います
     */
    void Initialize(DirectXCommon* dxCommon, Input* input, Audio* audio) override;

    /**
     * @brief シーンの終了処理
     * @note 次のシーンへ移る際のリソース解放や、タイトルBGMの停止などを行います
     */
    void Finalize() override;

    /**
     * @brief シーンの更新処理
     * @note 入力待ちのロジックを記述します特定のキーが押されたら finished_ を true にします
     */
    void Update() override;

    /**
     * @brief シーンの描画処理
     * @note タイトルロゴとかのスプライトの描画コマンドを積み込みます
     */
    void Draw() override;

    /**
     * @brief シーン終了フラグの取得
     * @return bool シーンが終了したかどうか（true: 終了して次へ / false: 継続）
     * @note 内部の finished_ フラグの状態を返します
     */
    bool IsFinished() const { return finished_; }

    /**
     * @brief デバッグ用UIマネージャーをセットする
     * @param imgui ImGuiManagerのポインタ
     */
    void SetImGuiManager(ImGuiManager* imgui) { imguiManager_ = imgui; }

private:
    // 外部から提供される基盤システム（借りてくるもの）

    /** @brief DirectX基盤のポインタ */
    DirectXCommon* dxCommon_ = nullptr;

    /** @brief 入力管理のポインタ */
    Input* input_ = nullptr;

    /** @brief 音響管理のポインタ */
    Audio* audio_ = nullptr;

    /** @brief デバッグUI用のImGuiマネージャー */
    ImGuiManager* imguiManager_ = nullptr;

    // このシーンが所有・管理するリソース

    /** @brief スプライト描画の共通設定 */
    std::unique_ptr<SpriteCommon> spriteCommon_;

    /** @brief 画面上部へ表示する金色の立体タイトル */
    FloatingTitle floatingTitle_;

    FontRenderer fontRenderer_;

    /** @brief NEW GAME / CONTINUE / TRAINING を選択するメニュー */
    UIMenu menu_;

    /** @brief シーン終了フラグ（trueになるとシーンが切り替わる） */
    bool finished_ = false;

    // ── 背景デモ（自動プレイの戦闘寸劇。武器を切り替えながらコンボを繰り返すだけの一方通行ループ） ──

    /** @brief デモ用3D共通設定一式（BattleTestScene/MapSceneと同じ最小構成） */
    std::unique_ptr<ModelCommon> modelCommon_;
    std::unique_ptr<Object3dCommon> objectCommon_;
    std::unique_ptr<ShadowManager> shadowManager_;
    std::unique_ptr<Camera> camera_;

    /** @brief デモを演じるプレイヤー本体（本編と同じPlayerクラス、Inputのアクションオーバーライドで自動操作する） */
    std::unique_ptr<Player> player_;
    ParticleManager* particleManager_ = nullptr;

    /** @brief 足場・左右の壁の飾りブロック（無いとプレイヤーが宙に浮いて見える） */
    Model* groundModel_ = nullptr;
    std::vector<std::unique_ptr<Object3d>> groundBlocks_;

    /** @brief 奥に置く背景のビル（TrainingScene/MapSceneと同じ、地面と壁だけだと寂しいための奥行き演出） */
    Model* cityModel_ = nullptr;
    std::vector<std::unique_ptr<Object3d>> cityObjects_;

    /** @brief 背景の天球（GamePlaySceneと同じモデル、カメラに追従する） */
    Model* modelSkydome_ = nullptr;
    std::unique_ptr<Skydome> skydome_;

    /** @brief プレイヤーが殴りながら進んでいく的（BattleTestSceneのDummyと同じスライム）。1体ずつ順番に近づいて倒す */
    struct DemoDummy {
        std::unique_ptr<Object3d> object;
        float x = 0.0f;
        float baseY = 0.0f;
        float currentY = 0.0f;
        float bobPhase = 0.0f;
        float bobSpeed = 1.0f;
        float moveSpeed = 1.0f;
        float attackCooldown = 0.0f;
        float attackAnimTimer = 0.0f;
        float lungeOffset = 0.0f;
        float respawnTimer = 0.0f;
        bool airborne = false;
        bool defeated = false;
        float hitFlashTimer = 0.0f;
    };
    Model* dummyModel_ = nullptr;
    std::vector<DemoDummy> demoDummies_;

    /** @brief デモ開始前の武器解放状況（デモではUnlockAll()するため、終了時に必ず復元する） */
    WeaponManager::Snapshot weaponManagerSnapshot_;

    /** @brief 現在狙っている的（demoDummies_内）のインデックス。全滅したらステージの最初からやり直す */
    int demoTargetIndex_ = 0;
    /** @brief 狙っている的の間合いに入ってからの経過秒（攻撃パルスのタイミングに使う） */
    float demoAttackTimer_ = 0.0f;
    /** @brief 次の的へ向かっている時間（道中のジャンプ・回避演出に使う） */
    float demoTravelTimer_ = 0.0f;
    /** @brief 現在の移動区間でジャンプまたは回避を実行済みか */
    bool demoMoveTrickUsed_ = false;
    /** @brief 攻撃間に短く後退してから再接近する演出の残り時間 */
    float demoBackstepTimer_ = 0.0f;
    float demoEnemyDodgeCooldown_ = 0.0f;
    /** @brief 急な回避や踏み込みでも画面が跳ねないよう補間したカメラX座標 */
    float demoCameraX_ = 0.0f;
    /** @brief 敵配置・武器・コンボ選択を周回ごとに変える乱数 */
    std::mt19937 demoRng_ { std::random_device {}() };
    /** @brief 現在の敵へ入れる攻撃数（敵ごとに2～4段で変化） */
    int demoAttackCount_ = 3;
    /** @brief 現在の敵への締めを武器固有技にするか */
    bool demoUseSkillFinisher_ = false;
    /** @brief 次に切り替える武器のインデックス（weaponManager_->GetList()内） */
    int demoWeaponIndex_ = 0;
    /** @brief 周回ごとに変えるジャンプ/固有技の有無・回避の判定に使う周回数 */
    int demoCycleIndex_ = 0;

    /** @brief 背景デモの初期化（3D共通設定・プレイヤー・足場ブロック・武器全解放） */
    void InitializeDemo();
    /** @brief 背景デモを1フレーム進める（Inputのアクションオーバーライドでプレイヤーを自動操作する） */
    void UpdateDemo();
    /** @brief 背景デモのシャドウパス描画 */
    void DrawDemoShadowPass();
    /** @brief 背景デモの本描画（3Dワールド） */
    void DrawDemoWorld();
};

} // namespace engine::game
