/**
 * @file ScreenFlash.h
 * @brief ScreenFlashが公開する型とAPIを定義するファイル
 */
#pragma once
#include "MakeAffine.h"
#include "Sprite.h"
#include "SpriteCommon.h"
#include <memory>
namespace engine {
class DirectXCommon;
}

namespace engine::game {
using engine::DirectXCommon;
using engine::graphics::Sprite;
using engine::graphics::SpriteCommon;

// 画面全体を色でフラッシュさせるエフェクト
// Game::Initialize() で Initialize()、Update()/Draw() を毎フレーム呼ぶ
// 使い方  ScreenFlash::GetInstance()->Request({ 1,1,1,1 }, 0.12f);
class ScreenFlash {
public:
    static ScreenFlash* GetInstance();

    void Initialize(DirectXCommon* dxCommon);

    // color  フラッシュ色 RGBA（例  {1,1,1,1} で白）
    // duration  フェードアウトにかかる秒数
    void Request(const Vector4& color, float duration);

    // 実時間 dt を渡す（GameConstants::kFrameDeltaTime 推奨）
    void Update(float dt);

    void Draw();

    bool IsActive() const { return timer_ > 0.0f; }

private:
    ScreenFlash() = default;

    std::unique_ptr<SpriteCommon> spriteCommon_;
    std::unique_ptr<Sprite> sprite_;
    Vector4 baseColor_ = { };
    float timer_ = 0.0f;
    float duration_ = 0.0f;
};

} // namespace engine::game
