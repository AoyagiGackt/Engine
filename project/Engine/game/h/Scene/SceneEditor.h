/**
 * @file SceneEditor.h
 * @brief ゲーム実行中にパラメータをリアルタイムで調整できるデバッグエディタ
 *
 * ImGui（開発者向け GUI ライブラリ）を使って、ゲームを動かしながら
 * カメラ位置・オブジェクトの色・パーティクルの数などを即座に変更できる
 * USE_IMGUI ビルドでのみ動作し、リリースビルドでは何もしない
 *
 * State パターンを使っている
 * → Hierarchy パネルでオブジェクトをクリックすると選択状態が変わり、
 *   Inspector パネルに表示される内容が切り替わる
 *   Camera を選んだら Camera の設定画面Ring を選んだら Ring の設定画面という仕組み
 */
#pragma once
#include "Camera.h"
#include "Cylinder.h"
#include "Ring.h"
#include "SkinnedObject3d.h"
#include "Skydome.h"
#include "Sprite.h"
#include "SpriteCommon.h"
#include <deque>
#include <memory>
#include <string>
#include <vector>
namespace engine {
class Input;
}
namespace engine::game {
using engine::graphics::Camera;
using engine::graphics::Cylinder;
using engine::graphics::Ring;
using engine::graphics::SkinnedObject3d;
using engine::graphics::Skydome;
using engine::graphics::Sprite;
using engine::graphics::SpriteCommon;

/** @brief シーン内オブジェクトのライブ調整、選択状態、設定保存を管理する */
class SceneEditor {
public:
    // 現在何が選択されているか
    // Hierarchy パネルで選んだオブジェクトの種類を表す
    enum class Selection {
        None, // 何も選んでいない
        Camera, // カメラ
        UIElement, // UI スプライト
        Ring, // ドーナツ型メッシュ
        Cylinder, // 円柱メッシュ
        Skydome, // 天球（背景）
        Human, // スキンメッシュキャラクター
        WhiteParticles // 白い浮遊パーティクル
    };

    // エディタが編集できるシーンデータへの参照
    // GamePlayScene から受け取るゲームの今の状態をまとめた構造体
    // ポインタで渡しているので、エディタで値を変えると即座にゲーム側にも反映される
    /** @brief 編集対象となるGamePlayScene側の実体・パラメータへのポインタをまとめた参照コンテキスト */
    struct EditContext {
        // オブジェクト本体へのポインタ（エディタがメソッドを呼ぶのに使う）
        Camera* camera = nullptr;
        Ring* ring = nullptr;
        Cylinder* cylinder = nullptr;
        Skydome* skydome = nullptr; // nullptr = 無効（スカイドームなし）
        SkinnedObject3d* human = nullptr;
        SpriteCommon* spriteCommon = nullptr; // UI スプライト生成に必要

        // カメラの目標位置・角度へのポインタ（スムージングの目標値）
        Vector3* cameraTargetPos = nullptr;
        Vector3* cameraTargetRot = nullptr;
        int* cameraSmoothFrames = nullptr; // 平均を取るフレーム数
        std::deque<Vector3>* cameraPosHistory = nullptr; // 過去の位置履歴
        std::deque<Vector3>* cameraRotHistory = nullptr; // 過去の角度履歴

        // Ring の各パラメータへのポインタ
        Vector3* ringPosition = nullptr;
        Vector3* ringRotation = nullptr;
        Vector4* ringColor = nullptr; // RGBA（各成分 0.0〜1.0）
        float* ringScale = nullptr;
        float* ringInnerRadius = nullptr; // 穴の半径
        float* ringOuterRadius = nullptr; // 外側の半径

        // Cylinder の各パラメータへのポインタ
        Vector3* cylinderPosition = nullptr;
        Vector3* cylinderRotation = nullptr;
        Vector4* cylinderColor = nullptr;
        float* cylinderScale = nullptr;
        float* cylinderTopRadius = nullptr; // 上面の半径
        float* cylinderBottomRadius = nullptr; // 下面の半径
        float* cylinderHeight = nullptr; // 高さ
        float* cylinderAlphaRef = nullptr; // 透明度の閾値

        // Skydome の各パラメータへのポインタ
        Vector4* skyColor = nullptr;
        float* skyRotOffsetY = nullptr; // Y 軸回転のオフセット角度

        // Human の各パラメータへのポインタ
        Vector3* humanPosition = nullptr;
        Vector3* humanRotation = nullptr;
        Vector3* humanScale = nullptr;
        float* humanAnimSpeed = nullptr; // アニメーション再生速度（1.0 = 等速）
        bool* showSkeleton = nullptr; // ボーンをデバッグ表示するか

        // 白パーティクルの各パラメータへのポインタ
        Vector3* whiteParticlePos = nullptr; // 散布の中心
        Vector4* whiteParticleColor = nullptr;
        float* whiteParticleScale = nullptr;
        int* whiteParticleCount = nullptr; // 粒の個数

        // ゲーム内時刻（読み取り専用の値コピー表示目的）
        int gameHour = 0;
        int gameMinute = 0;

        // ImGui の "Game Clear" ボタン用リクエストフラグ
        // true にすると GamePlayScene 側でガラス割れ演出を経由してシーン遷移する
        bool* requestClear = nullptr;
    };

    // Hierarchy に追加した UI スプライト1つ分のデータ
    /** @brief エディタのHierarchyへ追加したUIスプライト1つぶんの名前・実体・テクスチャパス */
    struct UIEntry {
        std::string name; // エディタ上での名前（例  "HP Bar"）
        std::unique_ptr<Sprite> sprite; // 実際のスプライトオブジェクト
        std::string texPath; // テクスチャファイルのパス
    };

    // ImGui パネルのレイアウト定数（画面上の位置・サイズ、単位はピクセル）
    static constexpr float kHierarchyX = 0.0f, kHierarchyY = 0.0f; // 左上に配置
    static constexpr float kHierarchyW = 220.0f, kHierarchyH = 400.0f;
    static constexpr float kInspectorX = 1060.0f, kInspectorY = 0.0f; // 右上に配置
    static constexpr float kInspectorW = 220.0f, kInspectorH = 500.0f;
    static constexpr float kSceneCtrlX = 0.0f, kSceneCtrlY = 400.0f; // 左下に配置
    static constexpr float kSceneCtrlW = 220.0f, kSceneCtrlH = 320.0f;
    static constexpr float kCamCtrlX = 400.0f, kCamCtrlY = 0.0f; // 上中央に配置
    static constexpr float kCamCtrlW = 300.0f, kCamCtrlH = 155.0f;

    // 外部から呼ぶメソッド

    /**
     * @brief 毎フレーム呼ぶF3でパネルの表示/非表示を切り替える（既定は非表示）
     * @param input F3キー判定用（nullptrならトグル操作は無視される）
     * @note ライブ調整用パネルなので、F1/F2のエディタと違い表示中もゲームは止めない
     */
    void Update(const EditContext& ctx, engine::Input* input);

    /** @brief 調整パネルが表示中か返す @return 表示中の場合はtrue */
    bool IsVisible() const { return visible_; }

    /** @brief 保存済みのカメラとUI設定を読み込む @param ctx 読み込み結果を反映する編集対象 */
    void LoadAll(const EditContext& ctx);

    // Hierarchy に追加した UI スプライト一覧を返す（GamePlayScene が描画のために使う）
    /** @brief 編集可能なUI要素一覧を返す @return UI要素一覧の参照 */
    std::vector<UIEntry>& GetUIElements() { return uiElements_; }
    /** @brief 読み取り専用のUI要素一覧を返す @return UI要素一覧の参照 */
    const std::vector<UIEntry>& GetUIElements() const { return uiElements_; }

    // 現在の選択状態を返す
    /** @brief 現在の選択種別を返す @return 選択種別 */
    Selection GetSelection() const { return selection_; }

private:
    // State パターンの基底クラス
    // 今何を選んでいるかによって Inspector パネルの内容を切り替える仕組み
    // 新しいオブジェクト種別を増やしたいときは、このクラスを継承して RenderInspector を実装する
    /** @brief 選択種別に対応する詳細表示を抽象化するState */
    class IEditorState {
    public:
        virtual ~IEditorState() = default;
        // Inspector パネルに表示する ImGui コントロールを描画する
        virtual void RenderInspector(const EditContext& ctx, SceneEditor& editor) = 0;
    };

    // 各オブジェクト種別に対応する State クラス（IEditorState の具体的な実装）
    /** @brief 未選択時の案内を表示するState */
    class NoneState : public IEditorState {
    public:
        void RenderInspector(const EditContext&, SceneEditor&) override;
    };
    /** @brief カメラ設定を編集するState */
    class CameraState : public IEditorState {
    public:
        void RenderInspector(const EditContext& ctx, SceneEditor& editor) override;
    };
    /** @brief リング設定を編集するState */
    class RingState : public IEditorState {
    public:
        void RenderInspector(const EditContext& ctx, SceneEditor& editor) override;
    };
    /** @brief 円柱設定を編集するState */
    class CylinderState : public IEditorState {
    public:
        void RenderInspector(const EditContext& ctx, SceneEditor& editor) override;
    };
    /** @brief 天球設定を編集するState */
    class SkydomeState : public IEditorState {
    public:
        void RenderInspector(const EditContext& ctx, SceneEditor& editor) override;
    };
    /** @brief キャラクター表示設定を編集するState */
    class HumanState : public IEditorState {
    public:
        void RenderInspector(const EditContext& ctx, SceneEditor& editor) override;
    };
    /** @brief パーティクル設定を編集するState */
    class ParticlesState : public IEditorState {
    public:
        void RenderInspector(const EditContext& ctx, SceneEditor& editor) override;
    };
    /** @brief UI要素の配置と表示を編集するState */
    class UIElementState : public IEditorState {
    public:
        void RenderInspector(const EditContext& ctx, SceneEditor& editor) override;
    };

    // 選択状態を変え、対応する State クラスに切り替える
    void ChangeState(Selection sel);

    // 各 ImGui パネルの描画メソッド
    void RenderHierarchy(const EditContext& ctx); // 左 オブジェクト一覧
    void RenderInspector(const EditContext& ctx); // 右 選択中オブジェクトのプロパティ
    void RenderSceneControls(const EditContext& ctx); // 左下 スコア・ゲーム時刻・シーン操作
    void RenderCameraControl(const EditContext& ctx); // 上中央 カメラ位置の簡易操作

    // JSON ファイルへの保存・読み込み
    // "Resources/editor_camera.json" にカメラのパラメータを保存/読込する
    /**
     * @brief カメラの目標位置・角度・スムージングフレーム数をJSONへ書き出す
     * @param ctx カメラパラメータへのポインタを持つ編集対象
     */
    void SaveCameraParams(const EditContext& ctx);
    /**
     * @brief 保存済みのカメラ目標位置・角度・スムージングフレーム数をJSONから読み込む（ファイルが無ければ何もしない）
     * @param ctx 読み込み結果を書き込む編集対象
     */
    void LoadCameraParams(const EditContext& ctx);

    // "Resources/editor_ui.json" に UI スプライトのレイアウトを保存/読込する
    /** @brief uiElements_の各スプライトの位置・サイズ・回転・色・テクスチャをJSONへ書き出す */
    void SaveUILayout();
    /**
     * @brief 保存済みのUIスプライト一覧をJSONから読み込み、uiElements_を再構築する
     * @param ctx スプライト生成に使うSpriteCommon等を持つ編集対象
     */
    void LoadUILayout(const EditContext& ctx);

    // 状態管理
    bool visible_ = false; // F3で表示/非表示（既定は非表示にして画面を占有しない）
    Selection selection_ = Selection::None; // 現在選んでいるオブジェクト種別
    int selectionIndex_ = -1; // UIElement リスト内のインデックス（-1 = 未選択）
    std::unique_ptr<IEditorState> currentState_; // 現在アクティブな State

    std::vector<UIEntry> uiElements_; // Hierarchy に追加した UI スプライト一覧
    float savedTimer_ = 0.0f; // "Saved!" メッセージを表示するための残り時間
};

} // namespace engine::game
