#include "Sequencer.h"
#include <algorithm>
using namespace engine;

Sequencer* Sequencer::GetInstance() {
    static Sequencer instance;
    return &instance;
}

Sequencer::Builder::Builder(Sequencer* owner) : owner_(owner) {}

Sequencer::Builder& Sequencer::Builder::Do(std::function<void()> action) {
    steps_.push_back({ Step::Type::Action, std::move(action) });
    return *this;
}

Sequencer::Builder& Sequencer::Builder::WaitSeconds(float seconds) {
    Step s;
    s.type    = Step::Type::WaitSeconds;
    s.seconds = seconds;
    steps_.push_back(std::move(s));
    return *this;
}

Sequencer::Builder& Sequencer::Builder::WaitFrames(int frames) {
    Step s;
    s.type   = Step::Type::WaitFrames;
    s.frames = frames;
    steps_.push_back(std::move(s));
    return *this;
}

Sequencer::Builder& Sequencer::Builder::WaitUntil(std::function<bool()> condition) {
    Step s;
    s.type      = Step::Type::WaitUntil;
    s.condition = std::move(condition);
    steps_.push_back(std::move(s));
    return *this;
}

int Sequencer::Builder::Run() {
    Sequence seq;
    seq.id    = owner_->nextId_++;
    seq.steps = std::move(steps_);
    owner_->sequences_.push_back(std::move(seq));
    return owner_->sequences_.back().id;
}

Sequencer::Builder Sequencer::Begin() {
    return Builder(this);
}

void Sequencer::Cancel(int id) {
    for (auto& seq : sequences_) {
        if (seq.id == id) { seq.done = true; return; }
    }
}

void Sequencer::Clear() {
    sequences_.clear();
    nextId_ = 0;
}

void Sequencer::Update(float dt) {
    for (auto& seq : sequences_) {
        if (seq.done) { continue; }

        // 1フレームで Action を連続実行し、Wait に当たったら止まる
        bool waiting = false;
        while (!waiting && seq.stepIdx < (int)seq.steps.size()) {
            auto& step = seq.steps[seq.stepIdx];

            switch (step.type) {
            case Builder::Step::Type::Action:
                if (step.action) { step.action(); }
                seq.stepIdx++;
                break;

            case Builder::Step::Type::WaitSeconds:
                if (seq.waitTimer <= 0.0f) { seq.waitTimer = step.seconds; }
                seq.waitTimer -= dt;
                if (seq.waitTimer > 0.0f) {
                    waiting = true;
                } else {
                    seq.waitTimer = 0.0f;
                    seq.stepIdx++;
                }
                break;

            case Builder::Step::Type::WaitFrames:
                if (seq.waitFrames <= 0) { seq.waitFrames = step.frames; }
                seq.waitFrames--;
                if (seq.waitFrames > 0) {
                    waiting = true;
                } else {
                    seq.waitFrames = 0;
                    seq.stepIdx++;
                }
                break;

            case Builder::Step::Type::WaitUntil:
                if (step.condition && !step.condition()) {
                    waiting = true;
                } else {
                    seq.stepIdx++;
                }
                break;
            }
        }

        if (seq.stepIdx >= (int)seq.steps.size()) { seq.done = true; }
    }

    // 完了済みを削除
    sequences_.erase(
        std::remove_if(sequences_.begin(), sequences_.end(),
            [](const Sequence& s) { return s.done; }),
        sequences_.end());
}
