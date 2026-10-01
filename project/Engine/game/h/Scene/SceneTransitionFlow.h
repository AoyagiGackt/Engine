/**
 * @file SceneTransitionFlow.h
 * @brief シーン切り替えの進行状態を管理するクラスを定義するファイル
 */
#pragma once
namespace engine::game {
/** @brief 描画基盤に依存しない遷移状態。ロード表示を送信するまで切り替えを許さない。 */
class SceneTransitionFlow {
public:
    enum class Action { None, ShowLoading, CommitSwitch };
    SceneTransitionFlow();
    void Begin();
    Action Update(bool fadeFinished, bool loadingPresented);
    void FinishSwitch();
    void Reset();
    bool IsChanging() const;
    bool IsLoading() const;
private:
    class State;
    class Idle;
    class FadingOut;
    class Loading;
    class FadingIn;
    const State* state_;
};
}
