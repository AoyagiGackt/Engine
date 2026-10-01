#include "GamePauseController.h"
namespace engine::game {
class GamePauseController::State {
public:
    virtual ~State() = default;
    virtual UpdateMode Advance(GamePauseController&, bool) const = 0;
    virtual bool IsPaused() const = 0;
};
class GamePauseController::Playing final : public State {
public:
    static const Playing& Instance() { static Playing state; return state; }
    UpdateMode Advance(GamePauseController&, bool) const override;
    bool IsPaused() const override { return false; }
};
class GamePauseController::Paused final : public State {
public:
    static const Paused& Instance() { static Paused state; return state; }
    UpdateMode Advance(GamePauseController& controller, bool toggle) const override {
        if (toggle) { controller.Resume(); return UpdateMode::Play; }
        return UpdateMode::Menu;
    }
    bool IsPaused() const override { return true; }
};
GamePauseController::UpdateMode GamePauseController::Playing::Advance(GamePauseController& controller, bool toggle) const {
    if (toggle) { controller.state_ = &Paused::Instance(); return UpdateMode::SkipFrame; }
    return UpdateMode::Play;
}
GamePauseController::GamePauseController() : state_(&Playing::Instance()) {}
GamePauseController::UpdateMode GamePauseController::Advance(bool toggle) { return state_->Advance(*this, toggle); }
void GamePauseController::Resume() { state_ = &Playing::Instance(); }
bool GamePauseController::IsPaused() const { return state_->IsPaused(); }
}
