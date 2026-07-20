/**
 * @file BaseScene.h
 * @brief 全てのシーンクラスの親となる基底クラスを定義するファイル
 */
#pragma once
#include "Audio.h"
#include "DirectXCommon.h"
#include "ImGuiManager.h"
#include "Input.h"
#include "Vector3.h"
#include <memory>
#include <string>
namespace engine::graphics {
class ModelCommon;
class Camera;
class ParticleManager;
class SpriteCommon;
}
namespace engine::game {
using engine::Audio;
using engine::DirectXCommon;
using engine::Input;
using engine::graphics::ImGuiManager;

class StageEditor;

/**
 * @brief 全てのシーンの抽象基底クラス
 * @note 各具体的なシーン（TitleScene 等）はこのクラスを継承して実装
 * 共通のインターフェースを提供することで、SceneManager による一括管理を可能に
 */
class BaseScene {
public:
    /**
     * @brief コンストラクタ（stageEditor_の生成をBaseScene.cpp側に閉じ込めるため明示宣言）
     * @note StageEditorはここではforward宣言のみ（class StageEditor;）。
     * StageEditorの中身（unique_ptr<KnightEnemy>/<EnemyEntity>）が、BaseSceneを使う
     * 無関係な翻訳単位（SceneManager.cpp等）にまで漏れ出してEnemyEntity等の完全な定義を
     * 要求してしまう（C2027）のを防ぐため、BaseScene自身もStageEditorをポインタで持つ
     */
    BaseScene();

    /**
     * @brief 仮想デストラクタ（定義はBaseScene.cpp。理由はコンストラクタのコメント参照）
     */
    virtual ~BaseScene();

    /**
     * @brief SceneManagerが呼ぶ初期化の入口（非virtual）
     * @note Initialize()（派生の本来の初期化）を呼んだ後、GetEditorLevelPath()等のフックを見て
     * 必要ならStageEditorのOpen()/RegisterExternalEntity()を自動で行う。
     * 派生シーンはこれまで通りInitialize()をoverrideするだけでよい（エディタ不要ならフックは無視してよい）
     */
    void Init(DirectXCommon* dxCommon, Input* input, Audio* audio);

    /**
     * @brief SceneManagerが毎フレーム呼ぶ更新の入口（非virtual）
     * @note StageEditorがF2で表示中なら、派生のUpdate()を呼ばずに一時停止状態にする
     * （UpdateObjects()＋RefreshVisualTransformsForEditor()のみ）。非表示ならUpdate()を呼んだ後
     * UpdateObjects()を呼ぶ（オブジェクトの親子追従・enemy系AIの本更新は毎フレーム必要なため）
     */
    void Tick();

    /**
     * @brief シーンの初期化（各シーンが実装）
     * @param dxCommon DirectX基盤のポインタ
     * @param input 入力管理のポインタ
     * @param audio 音響管理のポインタ
     * @note シーン開始時に一度だけ呼ばれ、必要なリソースのロードなどを行います
     * @note SceneManagerから直接は呼ばれない（Init()経由）。呼び出しは変わらずInit()からのみ
     */
    virtual void Initialize(DirectXCommon* dxCommon, Input* input, Audio* audio) = 0;

    /**
     * @brief シーンの更新処理（各シーンが実装）
     * @note 毎フレーム呼び出され、ゲームロジック（移動、衝突判定等）の更新を行います
     * @note SceneManagerから直接は呼ばれない（Tick()経由、エディタ表示中はスキップされる）
     */
    virtual void Update() = 0;

    /**
     * @brief SceneManagerが呼ぶ描画の入口（非virtual）
     * @note 派生のDraw()を呼んだ直後に、StageEditorの配置物をフレーム最後に上乗せで自動描画する
     * （GetStageEditor().DrawObjects()は自己完結型＝呼び出し位置のPSO状態に依存しない）。
     * その代償として配置物はシャドウパス／ポストエフェクトの対象外になり、HUDテキスト等の
     * 2Dスプライトより後に描かれる＝画面手前のUIパネルにブロックが重なって隠す形になり得る
     * （途中の正しい位置で影・エフェクト込み、かつHUDより手前で描きたい場合は、
     * Draw()内のHUD/フォント描画より前で自分でDrawObjects()を呼ぶこと。呼べばここでの自動呼び出しは
     * 自動でスキップされる＝StageEditor::WasObjectsDrawnThisFrame()）
     */
    void Render();

    /**
     * @brief シーンの描画処理（各シーンが実装）
     * @note 毎フレーム呼び出され、描画コマンドの積み込みを行います
     * @note SceneManagerから直接は呼ばれない（Render()経由）
     */
    virtual void Draw() = 0;

    /**
     * @brief シーンの終了処理
     * @note シーン切り替え時に呼ばれ、リソースの解放や後片付けを行います
     */
    virtual void Finalize() = 0;

    /**
     * @brief 派生シーンと共通エディタを決められた順序で終了する
     * @note SceneManagerだけが呼び、共通エディタの参照を解放してから派生シーンを終了する
     */
    void Shutdown();

    /**
     * @brief シーン終了フラグの取得
     * @return bool シーンが終了したかどうか（true: 終了 / false: 継続）
     * @note この関数が true を返すと、SceneManager は次のシーンへの遷移処理を開始します
     */
    virtual bool IsFinished() const { return false; }

    /** @brief ImGuiManagerを受け取る（デバッグUIが必要なシーンのみオーバーライド） */
    virtual void SetImGuiManager(ImGuiManager*) { }

    /**
     * @brief グレースケール／イメージフィルター／HSVフィルターのオフスクリーンRTVリダイレクトに対応しているか
     * @note 対応シーンのみ Draw() 内で GetActiveRTVHandle() 相当の切り替えを行っている
     *       未対応シーンで Game.cpp 側が BeginScene/EndScene/Apply を呼ぶと、
     *       何も描かれていないオフスクリーンテクスチャでバックバッファが上書きされ、
     *       画面から絵が消えてしまうため、対応シーンのみ true を返すこと
     */
    virtual bool SupportsPostEffects() const { return false; }

    /**
     * @brief 全シーン共通のステージエディタ（F2）SceneManagerが毎フレームUpdate()を呼ぶ
     * @note 呼び出し側はStageEditorの実体を使うため"StageEditor.h"を自分でインクルードすること
     * （ここではforward宣言のみで完結させ、他ヘッダーへの伝播を断つため）
     */
    StageEditor& GetStageEditor() { return *stageEditor_; }

    /**
     * @brief エディタのトリガー判定に使うプレイヤー位置
     * @note プレイヤーが存在するシーンだけオーバーライドすること既定は原点
     */
    virtual Vector3 GetEditorPlayerPos() const { return { }; }

    /**
     * @brief ホットキーオーバーレイ（画面左下のキー一覧）に足すシーン固有の行
     * @return 追加行が無ければnullptr（既定）。例: "F3: シーン調整パネル"
     */
    virtual const char* GetHotkeyOverlayExtra() const { return nullptr; }

    // ここから下はStageEditorの自動配線用フック。既定値のままなら何もしない（安全）

    /**
     * @brief Init()が自動でOpen()するレベルJSONのパス
     * @return 空文字なら自動Open()しない（既定）。ファイルが無ければ空のレベルとして起動する
     */
    virtual std::string GetEditorLevelPath() const { return { }; }

    /** @brief Open()に渡すModelCommon既定nullptrだとOpen()自体をスキップする */
    virtual engine::graphics::ModelCommon* GetEditorModelCommon() { return nullptr; }

    /** @brief Open()に渡すCamera既定nullptrだとOpen()自体をスキップする */
    virtual engine::graphics::Camera* GetEditorCamera() { return nullptr; }

    /** @brief enemy_knight配置のAI/演出に使うParticleManager無ければnullptrでよい */
    virtual engine::graphics::ParticleManager* GetEditorParticleManager() { return nullptr; }

    /**
     * @brief Init()時に"Player"としてRegisterExternalEntity()する対象の可変位置参照
     * @return nullptrなら登録しない（既定）
     */
    virtual Vector3* GetEditorPlayerPositionRef() { return nullptr; }

    /**
     * @brief エディタ表示中（ゲームプレイ停止中）に代わりに呼ばれる
     * @note player_->RefreshVisualTransforms()等、pos_を直接ドラッグされる可能性がある
     * 実体の見た目だけをその場で再計算するためのフック（AI/タイマーは進めないこと）
     */
    virtual void RefreshVisualTransformsForEditor() { }

protected:
    /**
     * @brief 各シーンのInitialize()冒頭で重複しがちな共通初期化をまとめて行う
     * @param dxCommon Initialize()が受け取ったDirectX基盤のポインタ
     * @param input Initialize()が受け取った入力管理のポインタ
     * @param audio Initialize()が受け取った音響管理のポインタ
     * @param outDxCommon 呼び出し側のdxCommon_メンバへの参照（代入先）
     * @param outInput 呼び出し側のinput_メンバへの参照（代入先）
     * @param outAudio 呼び出し側のaudio_メンバへの参照（代入先）
     * @return dxCommonで初期化済みのSpriteCommon（呼び出し側のspriteCommon_メンバに格納する）
     * @note dxCommon_/input_/audio_の保持とSpriteCommonの構築はほぼ全シーンで同一のため、ここに集約する
     */
    std::unique_ptr<engine::graphics::SpriteCommon> InitializeCommonResources(
        DirectXCommon* dxCommon, Input* input, Audio* audio,
        DirectXCommon*& outDxCommon, Input*& outInput, Audio*& outAudio);

private:
    std::unique_ptr<StageEditor> stageEditor_;
};

} // namespace engine::game
