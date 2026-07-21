/**
 * @file SlashMark.cpp
 * @brief SlashMarkが担当する処理を実装するファイル
 */
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
    if (!spriteCommon_) {
        return;
    }

    // 太く淡い残光の上に細く明るい芯を重ねて剣閃らしく見せる
    constexpr float kGlowWidthMult = 3.4f;
    constexpr float kGlowAlpha = 0.40f;
    Vector4 glowColor = params.color;
    glowColor.w *= kGlowAlpha;
    SpawnLayer(params, params.thickness * kGlowWidthMult, glowColor);

    Vector4 coreColor = { params.color.x * 0.3f + 0.7f, params.color.y * 0.3f + 0.7f,
        params.color.z * 0.3f + 0.7f, params.color.w };
    SpawnLayer(params, params.thickness, coreColor);
}

void SlashMark::SpawnLayer(const SlashMarkParams& params, float thickness, const Vector4& color)
{
    const float dx = params.end.x - params.start.x;
    const float dy = params.end.y - params.start.y;
    const float length = std::sqrt(dx * dx + dy * dy);
    const float angle = std::atan2(dy, dx);

    Entry entry;
    entry.sprite = std::make_unique<Sprite>();
    entry.sprite->Initialize(spriteCommon_, "Resources/white.png");
    entry.sprite->SetAnchorPoint({ 0.0f, 0.5f }); // 始点を基準に長さ方向へ伸ばす
    entry.sprite->SetPosition(params.start);
    entry.sprite->SetRotation(angle);
    entry.sprite->SetSize({ length, thickness });
    entry.sprite->SetColor(color);

    entry.baseColor = color;
    entry.duration = params.duration;
    entries_.push_back(std::move(entry));
}

void SlashMark::Update(float dt)
{
    for (auto& entry : entries_) {
        entry.timer += dt;
        const float t = std::clamp(entry.timer / entry.duration, 0.0f, 1.0f);
        const float fade = (1.0f - t) * (1.0f - t); // 出だしは明るく残し、終わり際に一気に消す
        Vector4 color = entry.baseColor;
        color.w *= fade;
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

void SlashMark::FlashAll(const Vector4& color, float duration)
{
    for (auto& entry : entries_) {
        // 残光レイヤーの淡さは保ったまま色だけ差し替える
        const float alpha = (std::min)(entry.baseColor.w, color.w);
        entry.baseColor = { color.x, color.y, color.z, alpha };
        entry.timer = 0.0f;
        entry.duration = duration;
    }
}

void SlashMark::Clear()
{
    entries_.clear();
}
