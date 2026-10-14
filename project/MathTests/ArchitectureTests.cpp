#include "../Engine/game/h/Scene/SceneTransitionFlow.h"
#include "../Engine/game/h/Scene/GamePauseController.h"
#include "../Engine/game/h/UI/HudLayer.h"
#include "TestReport.h"
#include <cstdio>
#include <source_location>
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
    const auto check = [&](bool condition, const char* name,
                           const std::source_location& where = std::source_location::current()) {
        ++checks;
        if (!condition) {
            ++failures;
            TestReport::Failure(name, nullptr, where);
        }
    };
    using Action = SceneTransitionFlow::Action;
    SceneTransitionFlow transition;
    check(!transition.IsChanging() && !transition.IsLoading(), "起動直後はシーン切り替え中でもロード画面中でもない");
    check(transition.Update(true, true) == Action::None, "何も始めていなければシーンは切り替わらない");
    transition.Begin();
    check(transition.IsChanging() && !transition.IsLoading(), "切り替えはまずフェードアウトから始まる（ロード画面はまだ）");
    check(transition.Update(false, true) == Action::None, "フェードアウトが終わる前はシーンを切り替えない");
    transition.Begin();
    check(transition.Update(true, false) == Action::ShowLoading, "切り替え中にもう一度頼んでもフェードは最初からやり直さない");
    check(transition.IsLoading(), "フェードアウトの後にロード画面になる");
    check(transition.Update(true, false) == Action::None, "ロード画面を1回描くまではシーンを切り替えない");
    check(transition.Update(true, true) == Action::CommitSwitch, "ロード画面を描いた後ならシーンを切り替える");
    transition.FinishSwitch();
    check(!transition.IsChanging() && !transition.IsLoading(), "新しいシーンはフェードイン中から動き出す");
    check(transition.Update(false, true) == Action::None, "フェードイン中にシーンをもう一度切り替えない");
    transition.Begin();
    check(transition.IsChanging(), "フェードイン中でも次の切り替えを始められる");
    transition.Reset();
    check(!transition.IsChanging() && transition.Update(true, true) == Action::None, "Resetで切り替えが取り消される");
    using Mode = GamePauseController::UpdateMode;
    GamePauseController pause;
    check(pause.Advance(false) == Mode::Play && !pause.IsPaused(), "最初はポーズしていないプレイ中の状態");
    check(pause.Advance(true) == Mode::SkipFrame && pause.IsPaused(), "ポーズを開いたフレームは、開くのに使った入力でメニューを操作しない");
    check(pause.Advance(false) == Mode::Menu, "ポーズ中の入力はメニューに渡る");
    check(pause.Advance(true) == Mode::Play && !pause.IsPaused(), "もう一度押すとプレイに戻る");
    pause.Advance(true);
    pause.Resume();
    check(pause.Advance(false) == Mode::Play, "Resume()でプレイに戻る");
    std::vector<int> calls;
    {
        HudLayer hud;
        hud.Add(std::make_unique<RecordingWidget>(1, calls));
        hud.Add(std::make_unique<RecordingWidget>(2, calls));
        hud.Draw();
        hud.Notify(HudEvent::WeaponAcquired);
        check(calls == std::vector<int>{1, 2, 11, 12}, "HUDは描画とイベントを登録した順に各部品へ渡す");
    }
    check(calls.size() == 6 && calls[4] < 0 && calls[5] < 0 && calls[4] != calls[5], "HUDは持っている部品を全部破棄する");
    TestReport::Print("\n-- 設計まわり（シーン遷移・ポーズ・HUD） --\n   %d / %d 件成功\n", checks - failures, checks);
    return failures;
}
