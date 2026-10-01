#include "SceneTransitionFlow.h"
namespace engine::game {
class SceneTransitionFlow::State {
public:
    virtual ~State() = default;
    virtual Action Update(SceneTransitionFlow&, bool, bool) const { return Action::None; }
    virtual bool IsChanging() const { return false; }
    virtual bool IsLoading() const { return false; }
};
class SceneTransitionFlow::Idle final : public State {
public:
    static const Idle& Instance() { static Idle state; return state; }
};
class SceneTransitionFlow::Loading final : public State {
public:
    static const Loading& Instance() { static Loading state; return state; }
    Action Update(SceneTransitionFlow&, bool, bool presented) const override {
        return presented ? Action::CommitSwitch : Action::None;
    }
    bool IsChanging() const override { return true; }
    bool IsLoading() const override { return true; }
};
class SceneTransitionFlow::FadingOut final : public State {
public:
    static const FadingOut& Instance() { static FadingOut state; return state; }
    Action Update(SceneTransitionFlow& flow, bool finished, bool) const override {
        if (!finished) { return Action::None; }
        flow.state_ = &Loading::Instance();
        return Action::ShowLoading;
    }
    bool IsChanging() const override { return true; }
};
class SceneTransitionFlow::FadingIn final : public State {
public:
    static const FadingIn& Instance() { static FadingIn state; return state; }
    Action Update(SceneTransitionFlow& flow, bool finished, bool) const override {
        if (finished) { flow.state_ = &Idle::Instance(); }
        return Action::None;
    }
};
SceneTransitionFlow::SceneTransitionFlow() : state_(&Idle::Instance()) {}
void SceneTransitionFlow::Begin() { if (!IsChanging()) { state_ = &FadingOut::Instance(); } }
SceneTransitionFlow::Action SceneTransitionFlow::Update(bool finished, bool presented) {
    return state_->Update(*this, finished, presented);
}
void SceneTransitionFlow::FinishSwitch() { if (IsLoading()) { state_ = &FadingIn::Instance(); } }
void SceneTransitionFlow::Reset() { state_ = &Idle::Instance(); }
bool SceneTransitionFlow::IsChanging() const { return state_->IsChanging(); }
bool SceneTransitionFlow::IsLoading() const { return state_->IsLoading(); }
}
