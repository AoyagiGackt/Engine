/**
 * @file AnimationStateMachine.cpp
 * @brief 複数アニメーションの状態登録・トリガー遷移・自動遷移（AnimationStateMachine）の実装
 */
#include "AnimationStateMachine.h"
#include "EngineAssert.h"
#include <cmath>

// 状態登録

namespace engine::game {
/** @brief 状態を追加する */
void AnimationStateMachine::AddState(const std::string& name,
    Animation* anim,
    bool loop,
    float speed) {
    State s;
    s.name = name;
    s.anim = anim;
    s.loop = loop;
    s.speed = speed;
    states_[name] = s;
}

// 遷移ルール登録

/** @brief トリガー名で起こる状態遷移を追加する */
void AnimationStateMachine::AddTransition(const std::string& from,
    const std::string& to,
    const std::string& trigger) {
    transitions_[from].push_back({ to, trigger });
}

/** @brief アニメーション終了時の自動遷移を追加する（loop=false の状態向け） */
void AnimationStateMachine::AddAutoTransition(const std::string& from,
    const std::string& to) {
    auto it = states_.find(from);
    if (it != states_.end()) {
        it->second.autoTransitionTo = to;
    }
}

// 初期 / 強制状態変更

/** @brief 遷移条件を見ずに、指定した状態へ直接切り替える */
void AnimationStateMachine::SetState(const std::string& name) {
    TransitionTo(name);
}

// トリガー発火

/** @brief 今の状態にトリガー名が一致する遷移があれば、その遷移先へ切り替える */
void AnimationStateMachine::Trigger(const std::string& triggerName) {
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

/** @brief 今の状態のアニメーション再生時刻を進める */
void AnimationStateMachine::Update(float dt) {
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

/** @brief 今の状態の Animation を返す（状態が無ければ nullptr） */
const Animation* AnimationStateMachine::GetCurrentAnimation() const {
    if (currentState_.empty()) {
        return nullptr;
    }
    auto it = states_.find(currentState_);
    if (it == states_.end()) {
        return nullptr;
    }
    return it->second.anim;
}

/** @brief 現在の状態のアニメーションが終端まで達したか（loop=false の場合のみ有効） */
bool AnimationStateMachine::IsCurrentAnimationFinished() const {
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

/** @brief 状態を stateName へ切り替え、再生時刻を0に戻す（同じ状態なら何もしない） */
void AnimationStateMachine::TransitionTo(const std::string& stateName) {
    if (stateName == currentState_) {
        return;
    } // 同じ状態なら何もしない

    // 遷移先が未登録の場合はアサート（デバッグ用）
    ENGINE_ASSERT(states_.count(stateName) > 0 && "AnimationStateMachine: 遷移先の状態が登録されていません");

    currentState_ = stateName;
    currentTime_ = 0.0f; // 遷移時に時刻をリセット
}

} // namespace engine::game
