/**
 * @file GamePauseController.h
 * @brief プレイ中とポーズ中の入力方針を状態オブジェクトで切り替えるクラスを定義するファイル
 */
#pragma once
namespace engine::game {
/** @brief プレイ・ポーズの入力方針を状態オブジェクトへ委譲する。 */
class GamePauseController {
public:
    enum class UpdateMode { Play, Menu, SkipFrame };
    GamePauseController();
    UpdateMode Advance(bool togglePressed);
    void Resume();
    bool IsPaused() const;
private:
    class State;
    class Playing;
    class Paused;
    const State* state_;
};
}
