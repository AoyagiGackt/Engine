/**
 * @file GamePauseController.h
 * @brief プレイ中とポーズ中の入力方針を状態オブジェクトで切り替えるクラスを定義するファイル
 */
#pragma once
namespace engine::game {

/** @brief プレイ・ポーズの入力方針を状態オブジェクトへ委譲する。 */
class GamePauseController {
public:
    /** @brief このフレームにシーンが何を更新するか */
    enum class UpdateMode {
        kPlay,      ///< ゲームを進める
        kMenu,      ///< ポーズメニューだけ操作する
        kSkipFrame, ///< ポーズを開いたフレーム（どちらも操作しない）
    };

    GamePauseController();

    /**
     * @brief 開閉入力を受けて状態を進める
     * @param togglePressed 開閉ボタンが押されたか
     */
    UpdateMode Advance(bool togglePressed);

    /** @brief ポーズを解除してプレイ中に戻す */
    void Resume();

    bool IsPaused() const;

private:
    class State;
    class Playing;
    class Paused;
    const State* state_;
};

} // namespace engine::game
