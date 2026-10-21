/**
 * @file Fade.h
 * @brief 帯で画面を覆って開くフェード（Fade）の宣言
 */
#pragma once
#include "Sprite.h"
#include <memory>
#include <array>
namespace engine::graphics {
/**
 * @brief 画面のフェードイン・フェードアウトを管理するクラス
 */
class Fade {
public:
    enum class Status {
        None, ///< 待機中
        FadeIn, ///< 暗い状態から明るくなる
        FadeOut, ///< 明るい状態から暗くなる
    };

    /**
     * @brief 初期化
     * @param spriteCommon 2D描画共通設定
     */
    void Initialize(SpriteCommon* spriteCommon);

    /**
     * @brief 更新処理
     */
    void Update();

    /**
     * @brief 描画処理
     */
    void Draw();

    /**
     * @brief フェード開始
     * @param status フェードの種類（In または Out）
     * @param duration かける時間（秒）
     */
    void Start(Status status, float duration);

    /**
     * @brief フェードが完了したか確認
     */
    bool IsFinished() const;

    /** @brief 呼び出し側へ現在のフェード種別を返す */
    Status GetStatus() const;

private:
    class IState;
    class IdleState;
    class FadeInState;
    class FadeOutState;
    /** @brief 暗転の進行段階に応じた共有状態を返す */
    static const IState& Idle();
    static const IState& FadingIn();
    static const IState& FadingOut();
    /** @brief 旧状態の終了と次状態の開始を順に実行する */
    void ChangeState(const IState& next);
    /** @brief 共通の経過時間を進め、完了したら待機へ戻す */
    void Advance(bool coverWhenFinished);
    /** @brief 現在状態の補間値を帯と縁へ反映する */
    void UpdateSprites();
    static constexpr size_t kBandCount = 8;
    SpriteCommon* spriteCommon_ = nullptr;
    std::array<std::unique_ptr<Sprite>, kBandCount> bands_;
    std::array<std::unique_ptr<Sprite>, kBandCount> edges_;
    bool covered_ = false;
    const IState* state_ = &Idle(); ///< 現在の状態オブジェクト
    float duration_ = 0.0f; /// 目標時間
    float counter_ = 0.0f; /// 経過時間タイマー
};

} // namespace engine::graphics
