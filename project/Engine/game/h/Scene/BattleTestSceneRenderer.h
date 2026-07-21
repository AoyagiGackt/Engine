/**
 * @file BattleTestSceneRenderer.h
 * @brief 訓練シーン固有の描画順序と画面演出を構成するファイル
 */
#pragma once

namespace engine::game {

class BattleTestScene;

/**
 * @brief 訓練シーンの3D描画、HUD、キャプチャ型画面演出を統括する
 *
 * BattleTestSceneから描画パスの責務を分離し、更新処理がレンダーターゲットの
 * 遷移順序へ依存しない構造を保つ。
 */
class BattleTestSceneRenderer {
public:
    /**
     * @brief 訓練シーンの現在状態を描画する
     * @param scene 描画対象の訓練シーン
     */
    static void Draw(BattleTestScene& scene);
};

} // namespace engine::game
