/**
 * @file GamePauseController.cpp
 * @brief プレイ中とポーズ中の入力方針を状態オブジェクトで切り替えるクラス（GamePauseController）の実装
 */
#include "GamePauseController.h"

// ══════════════════════════════════════════════════════
// 状態（プレイ中 / ポーズ中）
// ══════════════════════════════════════════════════════

namespace engine::game {
/** @brief プレイ中とポーズ中の入力方針と更新内容を抽象化する状態 */
class GamePauseController::State {
public:
    /** @brief 派生クラスを基底クラスから正しく破棄できるようにする仮想デストラクタ */
    virtual ~State() = default;
    /** @brief 開閉入力を受けて状態を進め、このフレームの更新方針を返す */
    virtual UpdateMode Advance(GamePauseController& controller, bool toggle) const = 0;
    /** @brief ポーズ中の状態かを返す */
    virtual bool IsPaused() const = 0;
};

/** @brief ポーズ開始入力を受け付けながらゲームの更新を許可する状態 */
class GamePauseController::Playing final : public State {
public:
    /** @brief この状態の共有インスタンスを返す */
    static const Playing& Instance() {
        static const Playing kState;
        return kState;
    }

    UpdateMode Advance(GamePauseController& controller, bool toggle) const override;
    /** @brief ポーズ中でないので false を返す */
    bool IsPaused() const override {
        return false;
    }
};

/** @brief ポーズ解除までメニュー操作だけを許可する状態 */
class GamePauseController::Paused final : public State {
public:
    /** @brief この状態の共有インスタンスを返す */
    static const Paused& Instance() {
        static const Paused kState;
        return kState;
    }

    UpdateMode Advance(GamePauseController& controller, bool toggle) const override;
    /** @brief ポーズ中なので true を返す */
    bool IsPaused() const override {
        return true;
    }
};

/** @brief プレイ中にポーズの入力があったらポーズへ移る */
GamePauseController::UpdateMode GamePauseController::Playing::Advance(GamePauseController& controller, bool toggle) const {
    if (!toggle) {
        return UpdateMode::kPlay;
    }
    controller.state_ = &Paused::Instance();
    // 開いたフレームはメニュー入力を受けない（開くのに使ったボタンで項目を決定しないため）
    return UpdateMode::kSkipFrame;
}

/** @brief ポーズ中にポーズの入力があったらプレイへ戻る */
GamePauseController::UpdateMode GamePauseController::Paused::Advance(GamePauseController& controller, bool toggle) const {
    if (!toggle) {
        return UpdateMode::kMenu;
    }
    controller.Resume();
    return UpdateMode::kPlay;
}

// ══════════════════════════════════════════════════════
// GamePauseController
// ══════════════════════════════════════════════════════

/** @brief ポーズされていないプレイ状態から入力制御を開始する */
GamePauseController::GamePauseController()
    : state_(&Playing::Instance()) {
}

/** @brief 開閉入力を受けて状態を進める */
GamePauseController::UpdateMode GamePauseController::Advance(bool toggle) {
    return state_->Advance(*this, toggle);
}

/** @brief ポーズを解除してプレイ中に戻す */
void GamePauseController::Resume() {
    state_ = &Playing::Instance();
}

/** @brief ポーズ中かを返す */
bool GamePauseController::IsPaused() const {
    return state_->IsPaused();
}

} // namespace engine::game
