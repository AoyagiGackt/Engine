/**
 * @file GamePlayScene.h
 * @brief ゲームプレイ本編のシーンロジックを管理するファイル
 *
 * このシーンは「ゲームを実際に遊ぶ画面」を担当する。
 * Initialize（開始時の初期設定）→ Update（毎フレーム更新）→ Draw（毎フレーム描画）
 * → Finalize（終了時の後片付け）の順に呼ばれる。
 */
#pragma once

 // --- 標準ライブラリ ---
#include <deque>
#include <memory>
#include <string>
#include <vector>

// --- エンジンシステム・基盤 ---
#include "Audio.h"
#include "BaseScene.h"
#include "Camera.h"
#include "DirectXCommon.h"
#include "GameObject.h"
#include "ImGuiManager.h"
#include "Input.h"
#include "Model.h"
#include "ModelCommon.h"
#include "Object3dCommon.h"
#include "ShadowManager.h"
#include "Sprite.h"
#include "SpriteCommon.h"
#include "SrvManager.h"

// --- ゲームロジック・オブジェクト ---
#include "Object3d.h"
#include "GameTime.h"
#include "MapChipField.h"
#include "Skydome.h"
#include "GlassShatterEffect.h"
#include "ImageFilter.h"
#include "RenderTexture.h"
#include "SceneEditor.h"

// Initialize で一度取得してキャッシュするシステムポインタ（実体はシングルトンが管理）
// ※ ヘッダに #include を書くとコンパイルが遅くなるため、前方宣言で済ませている
class GrayscaleEffect;
class HsvFilter;
class ImageFilter;
class ScoreManager;

/**
 * @brief ゲームプレイ本編のシーンクラス
 *
 * BaseScene を継承しており、SceneManager から
 * Initialize / Update / Draw / Finalize が呼ばれる。
 */
class GamePlayScene : public BaseScene{
public:
    // --- 基本関数 ---
    // ゲームが始まるときに1度だけ呼ばれる。オブジェクト生成・初期設定をここでやる
    void Initialize(DirectXCommon* dxCommon,Input* input,Audio* audio) override;

    // ゲームが終わるとき（シーン切替含む）に1度だけ呼ばれる。メモリの解放などをここでやる
    void Finalize() override;

    // 毎フレーム呼ばれる。ゲームの状態を1フレーム分進める
    void Update() override;

    // 毎フレーム呼ばれる。画面に絵を描く
    void Draw() override;

    // --- パブリックメソッド ---
    // 外部から ImGuiManager を受け取って保持する（ImGui はデバッグ用の GUI ライブラリ）
    void SetImGuiManager(ImGuiManager* imgui){ imguiManager_ = imgui; }

private:
    // --- 内部処理関数 ---

    // シャドウマップ（影の元になる深度画像）の描画パスのみを実行する
    void DrawShadowPass();

    // アクティブなポストエフェクトの描画先 RTV を返す（なければバックバッファ）
    D3D12_CPU_DESCRIPTOR_HANDLE GetActiveRTVHandle() const;

    // アクティブなポストエフェクトを適用する
    void ApplyActiveFilter();

    // メインの描画先 RTV・ビューポート・シザーをセットアップする
    void SetupMainRenderTarget();

    // カメラ位置の「ぬるぬる補間」を毎フレーム処理する
    // 過去数フレームの位置の平均をとることで、急な動きを滑らかにする（ボックスフィルタ）
    void UpdateCameraSmoothing();

    // SceneEditor（ImGuiデバッグUI）に渡す「現在のシーンのデータ一覧」を組み立てる
    SceneEditor::EditContext BuildEditContext();

    // =================================================
    // メンバ変数
    // =================================================

    // --- 外部システムポインタ（注入済み）---
    // Initialize の引数で受け取って保持する。delete しない（所有権は呼び出し元）
    DirectXCommon* dxCommon_    = nullptr; // DirectX12 の中核。GPU命令を出す窓口
    Input*         input_       = nullptr; // キーボード・コントローラー入力
    Audio*         audio_       = nullptr; // BGM・SE の再生
    ImGuiManager*  imguiManager_ = nullptr; // デバッグ用のGUI（開発中だけ表示）

    // --- キャッシュ済みシステムポインタ（Initialize で取得、生存期間はシステムが保証）---
    // 毎フレーム GetInstance() を呼ぶとコストがかかるので、起動時に1度だけ取得して保存しておく
    ScoreManager*    scoreManager_    = nullptr; // スコアの計算・保存
    SrvManager*      srvManager_      = nullptr; // GPU のテクスチャ用メモリ（ディスクリプタヒープ）管理
    GrayscaleEffect* grayscaleEffect_ = nullptr; // 画面をグレースケールにするポストエフェクト
    ImageFilter*     imageFilter_     = nullptr; // 画像フィルタ全般のポストエフェクト
    HsvFilter*       hsvFilter_       = nullptr; // 色相・彩度・明度を調整するポストエフェクト

    // --- 描画・共通基盤リソース ---
    // unique_ptr = 「このクラスだけが所有者」という意味。delete も自動でやってくれる
    std::unique_ptr<SpriteCommon>  spriteCommon_;  // 2Dスプライト描画に共通する設定（シェーダーなど）
    std::unique_ptr<ModelCommon>   modelCommon_;   // 3Dモデル描画に共通する設定
    std::unique_ptr<Object3dCommon> objectCommon_; // 3Dオブジェクト描画に共通する設定（ライトなど）
    std::unique_ptr<ShadowManager>  shadowManager_; // 影描画の管理（シャドウマップ方式）
    std::unique_ptr<Camera>         camera_;        // 視点となるカメラ

    // --- 天球 ---
    std::unique_ptr<Skydome> skydome_;
    std::unique_ptr<Model>   modelSkydome_;

    // --- ゲームオブジェクト群 ---
    std::vector<std::unique_ptr<GameObject>> gameObjects_; // 汎用ゲームオブジェクトのリスト
    std::unique_ptr<MapChipField> mapField_;               // マップチップ（タイル）フィールド

    // --- 画面を囲むブロック ---
    std::unique_ptr<Model>                   modelBlock_;
    std::vector<std::unique_ptr<Object3d>>   borderBlocks_;

    // --- プレイヤー ---
    std::unique_ptr<Model>    modelPlayer_;
    std::unique_ptr<Object3d> player_;
    Vector3 playerPos_        = { 8.0f, 0.4f, 0.0f };
    float   playerSpeed_      = 0.15f;
    float   playerVelocityY_  = 0.0f;
    bool    playerOnGround_   = true;

    static constexpr float kGroundY_    = 0.4f;   // 床ブロック上面 + プレイヤー半径
    static constexpr float kCeilingY_  = 12.0f;  // 天井クランプ上限
    static constexpr float kPlayerMinX_ = 3.0f;  // 左端クランプ
    static constexpr float kPlayerMaxX_ = 27.0f; // 右端クランプ
    static constexpr float kGravity_   = 0.012f;  // 毎フレームの重力加速度
    static constexpr float kJumpPower_ = 0.4f;    // ジャンプ初速

    // --- 敵 ---
    std::unique_ptr<Model>    modelEnemy_;
    std::unique_ptr<Object3d> enemy_;
    Vector3 enemyPos_ = { 22.0f, 0.4f, 0.0f };

    // --- 進行・状態管理 ---
    GameTime gameTime_; // ゲーム内時刻（時・分）を管理する

    // --- Skydome パラメータ ---
    Vector4 skyColor_      = { 1.0f, 1.0f, 1.0f, 1.0f }; // 天球の色（RGBA）
    float   skyRotOffsetY_ = 0.0f;                         // 天球を Y 軸方向に回転させるオフセット

    // --- オフスクリーンレンダリング ---
    // 画面に直接描かずに「一旦テクスチャに描いて、それを画面に貼る」仕組み
    // ポストエフェクト（グレースケールなど）をかけるために使う
    std::unique_ptr<RenderTexture> renderTexture_;       // テクスチャとして使えるレンダーターゲット
    std::unique_ptr<Sprite>        renderTextureSprite_; // 上記をそのまま画面に貼るためのスプライト

    // --- Camera Box Filter Smoothing ---
    // カメラが急に動いたとき、過去数フレームの位置を平均して滑らかにする仕組み
    Vector3 cameraTargetPos_ = { 14.5f, 6.0f, -30.0f }; // カメラが向かう目標位置
    Vector3 cameraTargetRot_ = { 0.0f, 0.0f, 0.0f };    // カメラが向かう目標角度
    std::deque<Vector3> cameraPosHistory_; // 過去の位置履歴（先頭から古い順）
    std::deque<Vector3> cameraRotHistory_; // 過去の角度履歴
    int cameraSmoothFrames_ = 1;           // 平均をとるフレーム数（1なら補間なし、大きいほど滑らか）

    // --- デバッグ・エディタ ---
    // ImGui を使ってゲーム実行中にパラメータをリアルタイムで調整できるエディタ
    SceneEditor sceneEditor_;

    // --- クリア演出 ---
    GlassShatterEffect glassShatter_;
    bool clearTriggered_ = false; // ガラス割れ開始済みフラグ（二重起動防止）
    bool requestClear_   = false; // ImGui ボタンからのクリアリクエスト

    // ガラスが割れている間（約1.6秒）に背後へ表示する白背景
    std::unique_ptr<Sprite> clearBgSprite_;
};
