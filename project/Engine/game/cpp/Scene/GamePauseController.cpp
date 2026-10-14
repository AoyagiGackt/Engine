/**
 * @file GamePauseController.cpp
 * @brief プレイ中とポーズ中の入力方針を状態オブジェクトで切り替えるクラス（GamePauseController）の実装
 */
#include "GamePauseController.h"
using namespace engine::game;

// ══════════════════════════════════════════════════════
// 状態（プレイ中 / ポーズ中）
// ══════════════════════════════════════════════════════

/** @brief プレイ中・ポーズ中それぞれの開閉の扱い */
class GamePauseController::State {
public:
    virtual ~State() = default;
    /** @brief 開閉入力を受けて状態を進め、このフレームの更新方針を返す */
    virtual UpdateMode Advance(GamePauseController& controller, bool toggle) const = 0;
    virtual bool IsPaused() const = 0;
};

class GamePauseController::Playing final : public State {
public:
    static const Playing& Instance()
    {
        static const Playing state;
        return state;
    }

    UpdateMode Advance(GamePauseController& controller, bool toggle) const override;
    bool IsPaused() const override { return false; }
};

class GamePauseController::Paused final : public State {
public:
    static const Paused& Instance()
    {
        static const Paused state;
        return state;
    }

    UpdateMode Advance(GamePauseController& controller, bool toggle) const override;
    bool IsPaused() const override { return true; }
};

GamePauseController::UpdateMode GamePauseController::Playing::Advance(GamePauseController& controller, bool toggle) const
{
    if (!toggle) {
        return UpdateMode::Play;
    }
    controller.state_ = &Paused::Instance();
    // 開いたフレームはメニュー入力を受けない（開くのに使ったボタンで項目を決定しないため）
    return UpdateMode::SkipFrame;
}

GamePauseController::UpdateMode GamePauseController::Paused::Advance(GamePauseController& controller, bool toggle) const
{
    if (!toggle) {
        return UpdateMode::Menu;
    }
    controller.Resume();
    return UpdateMode::Play;
}

// ══════════════════════════════════════════════════════
// GamePauseController
// ══════════════════════════════════════════════════════

GamePauseController::GamePauseController()
    : state_(&Playing::Instance())
{
}

GamePauseController::UpdateMode GamePauseController::Advance(bool toggle)
{
    return state_->Advance(*this, toggle);
}

void GamePauseController::Resume()
{
    state_ = &Playing::Instance();
}

bool GamePauseController::IsPaused() const
{
    return state_->IsPaused();
}
