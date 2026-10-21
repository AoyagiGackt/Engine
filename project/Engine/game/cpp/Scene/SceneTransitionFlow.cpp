/**
 * @file SceneTransitionFlow.cpp
 * @brief シーン切り替えの進行（フェード・ロード表示・切り替え）を状態クラスで管理する処理の実装
 */
#include "SceneTransitionFlow.h"
namespace engine::game {
/** @brief シーン切り替えの更新結果と進行状態を抽象化する基底状態 */
class SceneTransitionFlow::State {
public:
    /** @brief 派生クラスを基底クラスから正しく破棄できるようにする仮想デストラクタ */
    virtual ~State() = default;
    /** @brief 状態へ入る際の処理を行う */
    virtual void Enter(SceneTransitionFlow&) const { }
    /** @brief 状態を離れる際の処理を行う */
    virtual void Exit(SceneTransitionFlow&) const { }
    /** @brief 何もしない（遷移していない状態の既定） */
    virtual Action Update(SceneTransitionFlow&, bool, bool) const {
        return Action::None;
    }
    /** @brief シーンを切り替え中かを返す */
    virtual bool IsChanging() const {
        return false;
    }
    /** @brief 読み込み中かを返す */
    virtual bool IsLoading() const {
        return false;
    }
};
/** @brief シーン切り替えの開始を待つ状態 */
class SceneTransitionFlow::Idle final : public State {
public:
    /** @brief この状態の共有インスタンスを返す */
    static const Idle& Instance() {
        static Idle state;
        return state;
    }
};
/** @brief ロード画面の送信完了後にシーン切り替えを許可する状態 */
class SceneTransitionFlow::Loading final : public State {
public:
    /** @brief この状態の共有インスタンスを返す */
    static const Loading& Instance() {
        static Loading state;
        return state;
    }
    /** @brief 読み込み画面を表示し終えたらシーンの切り替えを指示する */
    Action Update(SceneTransitionFlow&, bool, bool presented) const override {
        return presented ? Action::CommitSwitch : Action::None;
    }
    /** @brief 読み込み中も切り替え中として true を返す */
    bool IsChanging() const override {
        return true;
    }
    /** @brief 読み込み中なので true を返す */
    bool IsLoading() const override {
        return true;
    }
};
/** @brief フェードアウト終了を待ってロード表示へ進む状態 */
class SceneTransitionFlow::FadingOut final : public State {
public:
    /** @brief この状態の共有インスタンスを返す */
    static const FadingOut& Instance() {
        static FadingOut state;
        return state;
    }
    /** @brief フェードアウトが終わったら読み込み中へ移り、読み込み画面の表示を指示する */
    Action Update(SceneTransitionFlow& flow, bool finished, bool) const override {
        if (!finished)
        {
            return Action::None;
        }
        flow.ChangeState(Loading::Instance());
        return Action::ShowLoading;
    }
    /** @brief フェードアウト中は切り替え中として true を返す */
    bool IsChanging() const override {
        return true;
    }
};
/** @brief フェードイン終了を待って通常進行へ戻る状態 */
class SceneTransitionFlow::FadingIn final : public State {
public:
    /** @brief この状態の共有インスタンスを返す */
    static const FadingIn& Instance() {
        static FadingIn state;
        return state;
    }
    /** @brief フェードインが終わったら待機へ戻る */
    Action Update(SceneTransitionFlow& flow, bool finished, bool) const override {
        if (finished)
        {
            flow.ChangeState(Idle::Instance());
        }
        return Action::None;
    }
};
/** @brief 旧状態の終了と新状態の開始を順に実行する */
void SceneTransitionFlow::ChangeState(const State& next)
{
    state_->Exit(*this);
    state_ = &next;
    state_->Enter(*this);
}

/** @brief 遷移していない待機状態で初期化する */
SceneTransitionFlow::SceneTransitionFlow() : state_(&Idle::Instance()) {}
/** @brief 切り替え中でなければフェードアウトを始める */
void SceneTransitionFlow::Begin() {
    if (!IsChanging())
    {
        ChangeState(FadingOut::Instance());
    }
}
/** @brief 今の状態を1フレーム進め、呼び出し側が行う処理を返す */
SceneTransitionFlow::Action SceneTransitionFlow::Update(bool finished, bool presented) {
    return state_->Update(*this, finished, presented);
}
/** @brief シーンの切り替えが済んだらフェードインへ移る */
void SceneTransitionFlow::FinishSwitch() {
    if (IsLoading())
    {
        ChangeState(FadingIn::Instance());
    }
}
/** @brief 遷移を止めて待機へ戻す */
void SceneTransitionFlow::Reset() {
    ChangeState(Idle::Instance());
}
/** @brief シーンを切り替え中かを返す */
bool SceneTransitionFlow::IsChanging() const {
    return state_->IsChanging();
}
/** @brief 読み込み中かを返す */
bool SceneTransitionFlow::IsLoading() const {
    return state_->IsLoading();
}
}
