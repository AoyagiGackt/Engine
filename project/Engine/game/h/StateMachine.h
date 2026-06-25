#pragma once
#include <functional>
#include <map>
#include <vector>

// ゲームロジック用の汎用ステートマシン（ヘッダーオンリー）
// State に任意の enum class / int / bool が使える。
//
// 使い方:
//   enum class PS { Ground, Air, Rampage };
//   StateMachine<PS> sm;
//
//   sm.AddState(PS::Ground,
//       []{ /* onEnter */ },
//       [](float dt){ /* onUpdate */ },
//       []{ /* onExit  */ });
//
//   // condition が true になると自動遷移
//   sm.AddTransition(PS::Ground, PS::Air, [&]{ return !player.IsOnGround(); });
//
//   sm.SetState(PS::Ground); // 初期状態（onEnter が呼ばれる）
//   sm.Update(dt);           // 毎フレーム（条件チェック → 遷移 or onUpdate）
//
// ※ AnimationStateMachine はアニメーション再生専用。
//   こちらはゲームロジック（プレイヤー状態・敵AI・UIフロー）向け。
template<typename State>
class StateMachine {
public:
    using Callback  = std::function<void()>;
    using UpdateCb  = std::function<void(float)>;
    using Condition = std::function<bool()>;

    // ---- 状態登録 ----
    void AddState(State s,
        Callback onEnter  = {},
        UpdateCb onUpdate = {},
        Callback onExit   = {})
    {
        states_[s] = { std::move(onEnter), std::move(onUpdate), std::move(onExit) };
    }

    // ---- 遷移ルール登録（condition が true になると遷移する）----
    void AddTransition(State from, State to, Condition condition) {
        transitions_[from].push_back({ to, std::move(condition) });
    }

    // ---- 初期状態の設定（onEnter を呼ぶ）----
    void SetState(State s) {
        if (initialized_ && current_ == s) { return; }
        if (initialized_) {
            auto it = states_.find(current_);
            if (it != states_.end() && it->second.onExit) { it->second.onExit(); }
        }
        current_     = s;
        initialized_ = true;
        auto it = states_.find(s);
        if (it != states_.end() && it->second.onEnter) { it->second.onEnter(); }
    }

    // ---- 毎フレーム更新 ----
    // 遷移条件を先にチェックし、満たせば遷移（その後 onUpdate はスキップ）。
    // 遷移しなければ現在状態の onUpdate を呼ぶ。
    void Update(float dt) {
        if (!initialized_) { return; }

        auto tit = transitions_.find(current_);
        if (tit != transitions_.end()) {
            for (auto& t : tit->second) {
                if (t.condition && t.condition()) {
                    SetState(t.to);
                    return;
                }
            }
        }

        auto it = states_.find(current_);
        if (it != states_.end() && it->second.onUpdate) { it->second.onUpdate(dt); }
    }

    // ---- 情報取得 ----
    State GetState()    const { return current_; }
    bool  IsIn(State s) const { return initialized_ && current_ == s; }
    bool  IsReady()     const { return initialized_; }

private:
    struct StateData {
        Callback onEnter;
        UpdateCb onUpdate;
        Callback onExit;
    };
    struct Transition {
        State     to;
        Condition condition;
    };

    std::map<State, StateData>               states_;
    std::map<State, std::vector<Transition>> transitions_;
    State current_{};
    bool  initialized_ = false;
};
