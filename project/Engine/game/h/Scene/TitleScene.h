/**
 * @file TitleScene.h
 * @brief ゲームのタイトル画面シーンを管理するファイル
 */
#pragma once
#include "FloatingTitle.h"
#include "Audio.h"
#include "BaseScene.h"
#include "Camera.h"
#include "CommandListView.h"
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
    bool IsFinished() const {
        return finished_;
    }


    // 背景デモの床とビルは Resources/Levels/title.json に置き、F2のステージエディタで編集する
    std::string GetEditorLevelPath() const override {
        return "Resources/Levels/title.json";
    }
    /** @brief エディタで使うモデルの共通設定を返す */
    engine::graphics::ModelCommon* GetEditorModelCommon() override {
        return modelCommon_.get();
    }
    /** @brief エディタで使うカメラを返す */
    engine::graphics::Camera* GetEditorCamera() override {
        return camera_.get();
    }
    /** @brief エディタ表示中（デモ停止中）もカメラ移動に合わせてデモの見た目の行列だけ更新する */
    void RefreshVisualTransformsForEditor() override;

private:
    /** @brief タイトルのメニューの1ページ（メイン / 設定 / コマンド表） */
    class IMenuPage {
    public:
        /** @brief 派生クラスを基底クラスから正しく破棄できるようにする仮想デストラクタ */
        virtual ~IMenuPage() = default;
        /** @brief 入力を受け付ける（ページの切り替えもここで行う） */
        virtual void Update(TitleScene& scene) const = 0;
        /** @brief メニューや文字を積む */
        virtual void Draw(TitleScene& scene) const = 0;
    };
    class MainMenuPage;
    class OptionsMenuPage;
    class CommandListPage;
    static const IMenuPage& MainMenu();
    static const IMenuPage& OptionsMenu();
    static const IMenuPage& CommandListMenu();

    /** @brief NEW GAME等の選択と決定（OPTIONSでは画面を切り替えず設定メニューを開く） */
    void UpdateMainMenu();
    /** @brief 設定メニューの音量調整と、BACK/Backspaceでメインメニューへ戻る処理 */
    void UpdateOptionsMenu();
    /** @brief 設定メニューの音量行の右側に現在の音量を百分率で描く */
    void DrawVolumeValue(int row, float volume);

    // 外部から提供される基盤システム（借りてくるもの）

    /** @brief DirectX基盤のポインタ */
    DirectXCommon* dxCommon_ = nullptr;

    /** @brief 入力管理のポインタ */
    Input* input_ = nullptr;

    /** @brief 音響管理のポインタ */
    Audio* audio_ = nullptr;

    /** @brief デバッグUI用のImGuiマネージャー */

    // このシーンが所有・管理するリソース

    /** @brief スプライト描画の共通設定 */
    std::unique_ptr<SpriteCommon> spriteCommon_;

    /** @brief 画面上部へ表示する金色の立体タイトル */
    FloatingTitle floatingTitle_;

    FontRenderer fontRenderer_;

    /** @brief NEW GAME / CONTINUE / TRAINING / OPTIONS を選択するメニュー */
    UIMenu menu_;

    /** @brief OPTIONSを選んだときにメインメニューと入れ替えて出す BGM / SE / COMMAND LIST / BACK のメニュー */
    UIMenu optionsMenu_;

    /** @brief 設定メニューの COMMAND LIST から開くコマンド表 */
    CommandListView commandList_;

    /** @brief 表示中のメニューのページ */
    const IMenuPage* menuPage_ = nullptr;

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

    /** @brief 背景の天球（GamePlaySceneと同じモデル、カメラに追従する） */
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

    /** @brief 現在狙っている的（demoDummies_内）のインデックス。-1 なら狙っていない */
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
    /** @brief 的の復帰位置・攻撃間隔のばらつきと演出に使う乱数 */
    std::mt19937 demoRng_ { std::random_device {}() };
    /** @brief 次に切り替える武器のインデックス（weaponManager_->GetList()内） */
    int demoWeaponIndex_ = 0;

    /** @brief 背景デモの初期化（3D共通設定・プレイヤー・足場ブロック・武器全解放） */
    void InitializeDemo();
    /** @brief 背景デモを1フレーム進める（Inputのアクションオーバーライドでプレイヤーを自動操作する） */
    void UpdateDemo();

    /** @brief デモ1フレームぶんの判断材料と、プレイヤーに押させるボタン（定義は TitleSceneDemo.cpp） */
    struct DemoFrame;
    /**
     * @brief 倒された的のうち復帰時間を過ぎたものを、プレイヤーの左右どちらかに出し直す
     * @param arenaLeft 復帰できる左端
     * @param arenaRight 復帰できる右端
     */
    void RespawnDefeatedDummies(float arenaLeft, float arenaRight);
    /**
     * @brief 生きている的をそれぞれの速度でプレイヤーへ近づけ、間合いに入ったら攻撃動作を始めさせる
     * @param playerX プレイヤーのX座標
     */
    void UpdateDummyBehavior(float playerX);
    /**
     * @brief 最も近い生きている的を狙いに決める
     * @param playerX プレイヤーのX座標
     * @return 狙う的。生きている的がいなければ nullptr
     */
    DemoDummy* SelectNearestDummy(float playerX);
    /** @brief 敵の攻撃の回避・攻撃間の後退・道中のジャンプや回避を決める */
    void DecideDemoMovement(DemoFrame& frame);
    /** @brief 間合いに入った的へコンボを振り、命中演出を出し、最後の段で撃破する */
    void UpdateDemoAttack(DemoFrame& frame);
    /** @brief 攻撃1段ぶんの命中演出を出す */
    void EmitDemoHitEffects(const DemoFrame& frame);
    /** @brief 狙っていた的を撃破して退場させ、次の的と次の武器へ切り替える */
    void DefeatDemoTarget(DemoFrame& frame);
    /** @brief 決めたボタンを Input のアクションオーバーライドへ書き込む */
    void ApplyDemoInput(DemoFrame& frame);
    /** @brief 的の上下の揺れ・被弾時の点滅・向き・位置をモデルへ反映する */
    void UpdateDummyVisuals();
    /** @brief 装備中の武器が常に出している軌跡を出す */
    void EmitDemoIdleTrail();
    /** @brief 背景デモのシャドウパス描画 */
    void DrawDemoShadowPass();
    /** @brief 背景デモの本描画（3Dワールド） */
    void DrawDemoWorld();
};

} // namespace engine::game
