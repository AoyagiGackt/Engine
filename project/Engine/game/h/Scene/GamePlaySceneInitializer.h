/**
 * @file GamePlaySceneInitializer.h
 * @brief メインステージのゲーム実体と進行ギミックを構築するファイル
 */
#pragma once

namespace engine::game {

class GamePlayScene;

/**
 * @brief メインステージに必要なプレイヤー、敵、収集物、障壁を構築する
 *
 * シーンのフレーム制御から初期配置の生成責務を分離し、ステージ構成の変更が
 * 更新処理や描画処理へ波及しないようにする。
 */
class GamePlaySceneInitializer {
public:
    /**
     * @brief レベルデータとラン情報からゲーム実体を構築する
     * @param scene 構築した実体を所有するゲームプレイシーン
     */
    static void InitializeStageActors(GamePlayScene& scene);
};

} // namespace engine::game
