/**
 * @file StageEditorViewport.h
 * @brief ステージエディタの中央ビューと編集カメラ操作を管理するファイル
 */
#pragma once
#include "Vector3.h"

namespace engine {
class Input;
}
namespace engine::graphics {
class Camera;
}

namespace engine::game {

/**
 * @brief ステージエディタのビュー領域判定と自由カメラ操作を担当するクラス
 *
 * パネル配置やマウス座標変換をStageEditor本体から分離し、
 * 選択処理と描画対象の管理へ影響を与えずにビュー操作を変更できるようにする。
 */
class StageEditorViewport {
public:
    /** @brief 操作対象のカメラを設定する */
    void SetCamera(engine::graphics::Camera* camera) { camera_ = camera; }

    /** @brief 保持している参照とドラッグ状態を破棄する */
    void Reset();

    /**
     * @brief キーボード入力で編集カメラを移動する
     * @param input 入力状態を管理するオブジェクト
     * @param deltaTime 実時間のフレーム間隔  単位は秒
     * @param focusMode パネルを隠した画面優先モードか
     */
    void UpdateCamera(engine::Input* input, float deltaTime, bool focusMode);

    /**
     * @brief マウス座標が操作可能な中央ビュー内にあるか返す
     * @param mouseX ウィンドウ左端からのマウス座標  単位はピクセル
     * @param mouseY ウィンドウ上端からのマウス座標  単位はピクセル
     * @param focusMode パネルを隠した画面優先モードか
     * @return 中央ビュー内ならtrueを返す
     */
    bool Contains(float mouseX, float mouseY, bool focusMode) const;

    /**
     * @brief スクリーン座標をゲーム平面上のワールド座標へ変換する
     * @param mouseX ウィンドウ左端からのマウス座標  単位はピクセル
     * @param mouseY ウィンドウ上端からのマウス座標  単位はピクセル
     * @param outWorld 変換したワールド座標の格納先
     * @return ゲーム平面と交差した場合はtrueを返す
     */
    bool ScreenToGround(float mouseX, float mouseY, Vector3& outWorld) const;

private:
    engine::graphics::Camera* camera_ = nullptr;
};

} // namespace engine::game
