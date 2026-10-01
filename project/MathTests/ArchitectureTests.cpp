#include "../Engine/game/h/Scene/SceneTransitionFlow.h"
#include "../Engine/game/h/Scene/GamePauseController.h"
#include "../Engine/game/h/UI/HudLayer.h"
#include <cstdio>
#include <vector>

using namespace engine::game;
namespace {
class RecordingWidget final : public IHudWidget {
public:
    RecordingWidget(int id, std::vector<int>& calls) : id_(id), calls_(calls) {}
    ~RecordingWidget() override { calls_.push_back(-id_); }
    void Initialize(const HudServices&) override {}
    void Update(const HudFrame&) override {}
    void QueueText(FontRenderer&) const override {}
    void Draw() override { calls_.push_back(id_); }
    void Notify(HudEvent) override { calls_.push_back(id_ + 10); }
private:
    int id_;
    std::vector<int>& calls_;
};
}

int RunArchitectureTests()
{
    int failures = 0;
    int checks = 0;
    const auto check = [&](bool condition, const char* name) {
        ++checks;
        if (!condition) { ++failures; std::printf("[FAIL] %s\n", name); }
    };
    using Action = SceneTransitionFlow::Action;
    SceneTransitionFlow transition;
    check(!transition.IsChanging() && !transition.IsLoading(), "startup has no loading screen");
    check(transition.Update(true, true) == Action::None, "idle does not switch scenes");
    transition.Begin();
    check(transition.IsChanging() && !transition.IsLoading(), "fade begins before loading");
    check(transition.Update(false, true) == Action::None, "unfinished fade cannot switch");
    transition.Begin();
    check(transition.Update(true, false) == Action::ShowLoading, "duplicate request preserves fade");
    check(transition.IsLoading(), "loading follows fade");
    check(transition.Update(true, false) == Action::None, "loading must be drawn before switch");
    check(transition.Update(true, true) == Action::CommitSwitch, "presented loading permits switch");
    transition.FinishSwitch();
    check(!transition.IsChanging() && !transition.IsLoading(), "new scene resumes during fade in");
    check(transition.Update(false, true) == Action::None, "fade in never repeats switch");
    transition.Begin();
    check(transition.IsChanging(), "new transition can begin during fade in");
    transition.Reset();
    check(!transition.IsChanging() && transition.Update(true, true) == Action::None, "reset cancels transition");
    using Mode = GamePauseController::UpdateMode;
    GamePauseController pause;
    check(pause.Advance(false) == Mode::Play && !pause.IsPaused(), "initial play state");
    check(pause.Advance(true) == Mode::SkipFrame && pause.IsPaused(), "opening pause consumes triggering input");
    check(pause.Advance(false) == Mode::Menu, "paused input routes to menu");
    check(pause.Advance(true) == Mode::Play && !pause.IsPaused(), "toggle resumes play");
    pause.Advance(true);
    pause.Resume();
    check(pause.Advance(false) == Mode::Play, "explicit resume restores play");
    std::vector<int> calls;
    {
        HudLayer hud;
        hud.Add(std::make_unique<RecordingWidget>(1, calls));
        hud.Add(std::make_unique<RecordingWidget>(2, calls));
        hud.Draw();
        hud.Notify(HudEvent::WeaponAcquired);
        check(calls == std::vector<int>{1, 2, 11, 12}, "HUD dispatches draw and events in order");
    }
    check(calls.size() == 6 && calls[4] < 0 && calls[5] < 0 && calls[4] != calls[5], "HUD owns and destroys every widget");
    std::printf("Architecture: %d / %d tests passed\n", checks - failures, checks);
    return failures;
}
