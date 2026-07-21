/**
 * @file AnimationStateMachine.cpp
 * @brief AnimationStateMachineのアプリケーション実行基盤の管理に関する具体的な処理を実装するファイル
 */
#include "AnimationStateMachine.h"
#include "EngineAssert.h"
#include <cmath>
using namespace engine;
using namespace engine::game;

// 状態登録

void AnimationStateMachine::AddState(const std::string& name,
    Animation* anim,
    bool loop,
    float speed)
{
    State s;
    s.name = name;
    s.anim = anim;
    s.loop = loop;
    s.speed = speed;
    states_[name] = s;
}

// 遷移ルール登録

void AnimationStateMachine::AddTransition(const std::string& from,
    const std::string& to,
    const std::string& trigger)
{
    transitions_[from].push_back({ to, trigger });
}

void AnimationStateMachine::AddAutoTransition(const std::string& from,
    const std::string& to)
{
    auto it = states_.find(from);
    if (it != states_.end()) {
        it->second.autoTransitionTo = to;
    }
}

// 初期 / 強制状態変更

void AnimationStateMachine::SetState(const std::string& name)
{
    TransitionTo(name);
}

// トリガー発火

void AnimationStateMachine::Trigger(const std::string& triggerName)
{
    auto it = transitions_.find(currentState_);
    if (it == transitions_.end()) {
        return;
    }
    for (const auto& t : it->second) {
        if (t.trigger == triggerName) {
            TransitionTo(t.to);
            return; // 最初にヒットした遷移のみ実行
        }
    }
}

// 更新

void AnimationStateMachine::Update(float dt)
{
    if (currentState_.empty()) {
        return;
    }

    auto it = states_.find(currentState_);
    if (it == states_.end()) {
        return;
    }

    State& s = it->second;
    if (!s.anim || s.anim->duration <= 0.0f) {
        return;
    }

    // 再生時刻を進める
    currentTime_ += dt * s.speed;

    if (s.loop) {
        // ループ  duration を超えたら先頭に折り返す
        currentTime_ = std::fmod(currentTime_, s.anim->duration);
    } else {
        // 非ループ  終端でクランプし、自動遷移があれば遷移
        if (currentTime_ >= s.anim->duration) {
            currentTime_ = s.anim->duration;
            if (!s.autoTransitionTo.empty()) {
                TransitionTo(s.autoTransitionTo);
            }
        }
    }
}

// 情報取得

const Animation* AnimationStateMachine::GetCurrentAnimation() const
{
    if (currentState_.empty()) {
        return nullptr;
    }
    auto it = states_.find(currentState_);
    if (it == states_.end()) {
        return nullptr;
    }
    return it->second.anim;
}

bool AnimationStateMachine::IsCurrentAnimationFinished() const
{
    if (currentState_.empty()) {
        return false;
    }
    auto it = states_.find(currentState_);
    if (it == states_.end()) {
        return false;
    }

    const State& s = it->second;
    if (s.loop || !s.anim || s.anim->duration <= 0.0f) {
        return false;
    }

    return currentTime_ >= s.anim->duration;
}

// 内部  状態遷移

void AnimationStateMachine::TransitionTo(const std::string& stateName)
{
    if (stateName == currentState_) {
        return;
    } // 同じ状態なら何もしない

    // 遷移先が未登録の場合はアサート（デバッグ用）
    ENGINE_ASSERT(states_.count(stateName) > 0 && "AnimationStateMachine: 遷移先の状態が登録されていません");

    currentState_ = stateName;
    currentTime_ = 0.0f; // 遷移時に時刻をリセット
}
