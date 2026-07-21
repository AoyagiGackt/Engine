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
/**
 * @brief ScreenFlash に関する型を提供する
 * @details ScreenFlash が扱うデータと操作の責務をまとめる
 */
class ScreenFlash {
public:
    /**
     * @brief GetInstance の結果を取得する
     * @return 処理結果
     */
    static ScreenFlash* GetInstance();

    /**
     * @brief Initialize に対応する処理を開始する
     * @param dxCommon 処理に使用する値
     * @return なし
     */
    void Initialize(DirectXCommon* dxCommon);

    // color  フラッシュ色 RGBA（例  {1,1,1,1} で白）
    // duration  フェードアウトにかかる秒数
    /**
     * @brief Request に対応する処理を実行する
     * @param color 処理に使用する値
     * @param duration 処理に使用する値
     * @return なし
     */
    void Request(const Vector4& color, float duration);

    // 実時間 dt を渡す（GameConstants::kFrameDeltaTime 推奨）
    /**
     * @brief Update に対応する状態を更新する
     * @param dt 処理に使用する値
     * @return なし
     */
    void Update(float dt);

    /**
     * @brief Draw に対応する内容を描画する
     * @return なし
     */
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
