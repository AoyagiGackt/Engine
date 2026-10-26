/**
 * @file ComboTrial.h
 * @brief 決めた手順のコンボを順に出せたかを見て、手順の札を並べて見せるコンボ練習を定義するファイル
 * @note 練習するコンボの名前と手順は Resources/Config/combo_trials.json で決める
 */
#pragma once
#include "Sprite.h"
#include "SpriteCommon.h"
#include <memory>
#include <string>
#include <vector>

namespace engine {
class Input;
}

namespace engine::game {

class FontRenderer;
class Player;

/** @brief コンボ練習（選んだコンボの手順を順に出せたら成功） */
class ComboTrial {
public:
    /** @brief 手順1つで求める行動 */
    enum class Action {
        kAttack, ///< 地上で攻撃を当てる
        kLaunch, ///< 打ち上げる
        kJump, ///< ジャンプする
        kAirAttack, ///< 空中で攻撃を当てる
        kShoot, ///< 銃を撃つ
        kSkill, ///< 武器の固有技を当てる
        kDodge, ///< 回避する
        kCount
    };

    /** @brief 練習するコンボ1つ */
    struct Trial {
        std::string name; ///< 一覧に出す名前
        std::vector<Action> steps; ///< 出す順の行動
    };

    /** @brief 手順の札のスプライトを用意し、combo_trials.json を読む（シーンの Initialize で1回） */
    void Initialize(engine::graphics::SpriteCommon* spriteCommon);
    /** @brief 練習できるコンボの一覧 */
    const std::vector<Trial>& Trials() const {
        return trials_;
    }
    /** @brief index 番目のコンボの練習を始める（範囲外なら何もしない） */
    void Start(size_t index);
    /** @brief 練習をやめる */
    void Stop();
    /** @brief 練習中か */
    bool IsActive() const {
        return active_;
    }
    /**
     * @brief プレイヤーのこのフレームの行動を見て手順を進める（間が空きすぎたら最初からやり直す）
     * @note プレイヤーを更新した直後に呼ぶ（ヒットストップ中は行動の印が残るので進めない）
     */
    void Observe(const Player& player);
    /** @brief 練習中のコンボ名・手順の札の文字・成功の文字を積む */
    void QueueText(FontRenderer& font, const engine::Input* input) const;
    /** @brief 手順の札を描く（SpriteCommon の共通設定を済ませた後、文字の Draw より前に呼ぶ） */
    void Draw();

private:
    /** @brief 手順を最初に戻す */
    void ResetProgress();
    /** @brief 札の位置と色を今の進み具合に合わせる */
    void LayoutCards(const engine::Input* input) const;

    std::vector<Trial> trials_;
    size_t current_ = 0; ///< 練習中のコンボの番号
    size_t done_ = 0; ///< 出し終えた手順の数
    float idleTimer_ = 0.0f; ///< 最後に手順を進めてからの秒数（長すぎたら最初から）
    float successTimer_ = 0.0f; ///< 成功の文字を出している残り秒数
    float failTimer_ = 0.0f; ///< やり直しの合図を出している残り秒数
    float pulseTimer_ = 0.0f; ///< 次に出す手順の札を光らせる時間
    bool active_ = false;
    std::vector<std::unique_ptr<engine::graphics::Sprite>> cards_; ///< 手順の札（最大の手順数ぶんを最初に作って使い回す）
    std::unique_ptr<engine::graphics::Sprite> panel_; ///< 札の後ろの帯
};

} // namespace engine::game
