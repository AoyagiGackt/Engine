/**
 * @file ScreenFlash.h
 * @brief ScreenFlashのゲーム画面UIの状態更新と描画に関する公開型と操作インターフェースを定義するファイル
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
/** @brief 画面全体を単色フェードでフラッシュさせるシングルトンエフェクト */
class ScreenFlash {
public:
    /** @brief シングルトンインスタンスを返す（初回呼び出しで生成される） */
    static ScreenFlash* GetInstance();

    /**
     * @brief 画面全体を覆う不透明スプライトを生成する（Game::Initialize()で1回呼ぶ）
     * @param dxCommon DirectXの共通処理
     */
    void Initialize(DirectXCommon* dxCommon);

    /**
     * @brief フラッシュを開始する（既存のフラッシュ中に呼ぶと上書きされる）
     * @param color フラッシュ色 RGBA（例 {1,1,1,1} で白）
     * @param duration 色の不透明度が0まで減衰する秒数
     */
    void Request(const Vector4& color, float duration);

    /**
     * @brief 残り時間を減らし、経過に応じてスプライトの不透明度を線形フェードさせる
     * @param dt 実時間の経過秒数（GameConstants::kFrameDeltaTime 推奨）
     */
    void Update(float dt);

    /** @brief フラッシュ中（timer_>0）ならスプライトを描画する */
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
