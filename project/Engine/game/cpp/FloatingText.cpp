#include "FloatingText.h"
#include <algorithm>
#include <cmath>

FloatingText* FloatingText::GetInstance()
{
    static FloatingText instance;
    return &instance;
}

void FloatingText::Initialize(FontRenderer* fontRenderer)
{
    fontRenderer_ = fontRenderer;
}

void FloatingText::Spawn(const FloatingTextParams& params)
{
    // 新しいエントリを作成して初期位置をパラメータからコピー
    Entry e;
    e.params = params;
    e.timer  = 0.0f;
    e.x      = params.x;
    e.y      = params.y;
    entries_.push_back(e);
}

void FloatingText::Update(float dt)
{
    for (auto& e : entries_) {
        // 経過時間を加算
        e.timer += dt;
        // 上昇アニメーション（Y を減算すると上方向に動く）
        e.y -= e.params.riseSpeed * dt;
    }

    // 表示時間を過ぎたエントリを削除する（erase-remove イディオム）
    entries_.erase(
        std::remove_if(entries_.begin(), entries_.end(),
            [](const Entry& e) { return e.timer >= e.params.duration; }),
        entries_.end());
}

void FloatingText::Draw()
{
    if (!fontRenderer_) { return; }

    for (const auto& e : entries_) {
        // アルファの基本値はパラメータで指定された color.w
        float alpha = e.params.color.w;

        // フェードアウト計算
        // fadeStart（duration に対する比率）を過ぎたら線形にアルファを下げる
        float fadeThreshold = e.params.duration * e.params.fadeStart;
        if (e.timer > fadeThreshold) {
            // フェード開始からの進行度（0.0=始まり, 1.0=完全に透明）
            float fadeProgress = (e.timer - fadeThreshold) / (e.params.duration - fadeThreshold);
            // アルファに 1.0 から fadeProgress を引いた値を乗算
            alpha *= std::fmaxf(0.0f, 1.0f - fadeProgress);
        }

        // フェードアウト済みのアルファで文字色を上書きして描画
        Vector4 color = { e.params.color.x, e.params.color.y, e.params.color.z, alpha };
        fontRenderer_->DrawString(e.params.text, e.x, e.y, e.params.scale, color);
    }
}

void FloatingText::Clear()
{
    entries_.clear();
}
