#include "SlashMark.h"
#include <algorithm>
#include <cmath>
using namespace engine;
using namespace engine::graphics;
using namespace engine::game;

SlashMark* SlashMark::GetInstance()
{
    static SlashMark instance;
    return &instance;
}

void SlashMark::Initialize(SpriteCommon* spriteCommon)
{
    spriteCommon_ = spriteCommon;
}

void SlashMark::Spawn(const SlashMarkParams& params)
{
    if (!spriteCommon_) { return; }

    const float dx     = params.end.x - params.start.x;
    const float dy     = params.end.y - params.start.y;
    const float length = std::sqrt(dx * dx + dy * dy);
    const float angle  = std::atan2(dy, dx);

    Entry entry;
    entry.sprite = std::make_unique<Sprite>();
    entry.sprite->Initialize(spriteCommon_, "Resources/white.png");
    entry.sprite->SetAnchorPoint({ 0.0f, 0.5f }); // 始点を基準に長さ方向へ伸ばす
    entry.sprite->SetPosition(params.start);
    entry.sprite->SetRotation(angle);
    entry.sprite->SetSize({ length, params.thickness });
    entry.sprite->SetColor(params.color);

    entry.baseColor = params.color;
    entry.duration  = params.duration;
    entries_.push_back(std::move(entry));
}

void SlashMark::Update(float dt)
{
    for (auto& entry : entries_) {
        entry.timer += dt;
        const float t     = std::clamp(entry.timer / entry.duration, 0.0f, 1.0f);
        Vector4     color = entry.baseColor;
        color.w *= (1.0f - t);
        entry.sprite->SetColor(color);
        entry.sprite->Update();
    }

    std::erase_if(entries_, [](const Entry& e) { return e.timer >= e.duration; });
}

void SlashMark::Draw()
{
    for (auto& entry : entries_) {
        entry.sprite->Draw();
    }
}

void SlashMark::Clear()
{
    entries_.clear();
}
