/**
 * @file ClearFinish.h
 * @brief クリアの決め演出（剣なら画面を細かく斬り刻む・銃なら画面に弾を浴びせる → 静止の後に決めの一撃 → 画面が割れ落ちて結果画面が現れる）を段階ごとに進めるクラス
 * @note 斬線や弾痕の数・間隔・散らばり・秒数・演出レシピ名は Resources/Config/clear_finish.json で決める。
 *       段階は IPhase の派生クラス、斬線か弾痕かの違いは IStrikeStyle の派生クラスで差し替える
 */
#pragma once
#include "Sprite.h"
#include "SpriteCommon.h"
#include "Vector2.h"
#include <memory>
#include <random>
#include <vector>

namespace engine::graphics {
class GlassShatterEffect;
class ParticleManager;
}

namespace engine::game {

class CameraShaker;

/** @brief クリアの決め演出 */
class ClearFinish {
public:
    /** @brief ボスを倒した最後の一撃の種類（決めの見せ方が変わる） */
    enum class Style {
        kBlade, ///< 剣などの近接武器: 画面を斬線が何本も走り、最後に大きく斬る
        kGun, ///< 銃: 画面に弾が何発も入り、最後に大きく撃ち抜く
    };
    /** @brief 演出を始める時にシーンから渡す情報 */
    struct Setup {
        Style style = Style::kBlade;
        float resultSeconds = 0.0f; ///< 割れ落ちた後に結果画面を見せる長さ
    };
    /** @brief 演出が借りるシーンの部品（非所有） */
    struct Services {
        engine::graphics::GlassShatterEffect* glass = nullptr; ///< 画面を割る全画面エフェクト（撮影と描画はシーンが行う）
        engine::graphics::ParticleManager* particles = nullptr;
        CameraShaker* shaker = nullptr;
        std::mt19937* rng = nullptr;
    };

    ClearFinish();
    ~ClearFinish();

    /** @brief 暗幕・斬線・弾痕のスプライトを作る（シーンの Initialize で1回） */
    void Initialize(engine::graphics::SpriteCommon* spriteCommon);
    /** @brief 演出を最初から始める */
    void Start(const Setup& setup, const Services& services);
    /** @brief 1フレーム進める */
    void Update();
    /** @brief 演出を始めてから、シーンを切り替えるまでの間か */
    bool IsActive() const {
        return active_;
    }
    /**
     * @brief 割る前のゲーム画面を描くべきか（決めの間と、割る前の画面を撮る1フレーム）
     * @note false の間は、シーンはゲーム画面の代わりに結果画面を描き、その上に割れた破片を重ねる
     */
    bool ShowsGameFrame() const;
    /** @brief 最後まで見せ終えた最初の1回だけ true を返す（シーンはこれで次の画面へ切り替える） */
    bool ConsumeFinished();
    /** @brief 割れたガラスが集まり直して結果画面になった最初の1回だけ true を返す（シーンはこれでクリアの曲を流す） */
    bool ConsumeResultShown();
    /** @brief 暗幕・斬線・弾痕を描く（ゲーム画面の上、割る前の画面を撮る前に。SpriteCommon の共通設定を済ませた後に呼ぶ） */
    void DrawStrikeOverlay();

    class IPhase;
    class StrikePhase;
    class ShatterPhase;
    class ResultPhase;
    class IStrikeStyle;
    class BladeStyle;
    class GunStyle;

private:
    /** @brief 画面に入れる斬線1本・弾痕1つ（始めるたびに並びを作り直す） */
    struct Mark {
        Vector2 uv { 0.5f, 0.5f }; ///< 斬線が通る点、または弾痕の中心（画面に対する0〜1）
        float angle = 0.0f; ///< 斬線の傾き、または弾痕の回転（ラジアン）
        float delay = 0.0f; ///< 決めを始めてから入るまでの秒数
        bool finale = false; ///< 静止の後の決めの一撃か（太く大きく、揺れも強い）
    };

    /** @brief 段階を次へ進める（最後まで行ったら終える） */
    void AdvancePhase();
    /** @brief 決めの間、画面を暗くしていく */
    void UpdateDim();
    /** @brief 入る時刻が来た斬線・弾痕の揺れとフラッシュを出す */
    void StartDueMarks();
    /** @brief 最後の斬線・弾痕が入る時刻 */
    float LastMarkDelay() const;

    Setup setup_;
    Services services_;
    const IStrikeStyle* style_ = nullptr;
    std::vector<const IPhase*> phases_;
    size_t phaseIndex_ = 0;
    float phaseTimer_ = 0.0f;
    bool active_ = false;
    bool playing_ = false; ///< 段階を進めている途中か（見せ終えた後も active_ のまま結果画面を保つ）
    bool finishedPending_ = false; ///< 見せ終えたことをシーンへまだ伝えていない
    bool resultShownPending_ = false; ///< 結果画面が出たことをシーンへまだ伝えていない
    bool striking_ = false; ///< 決め（斬線・着弾）の段階か
    float strikeTimer_ = 0.0f; ///< 決めを始めてからの秒数（斬線と弾痕の進み具合はこれから求める）
    float dimAlpha_ = 0.0f; ///< 暗幕の濃さ（0〜1、JSONの色の不透明度に掛ける）
    std::vector<Mark> marks_; ///< 入れる順に並べた斬線・弾痕
    size_t marksStarted_ = 0; ///< 入れ始めた斬線・弾痕の数（揺れとフラッシュを1回ずつ出す）
    Vector2 impactUV_ { 0.5f, 0.5f }; ///< 割れが広がり始める画面上の点（0〜1）

    std::unique_ptr<engine::graphics::Sprite> dim_;
    std::vector<std::unique_ptr<engine::graphics::Sprite>> slashCores_;
    std::vector<std::unique_ptr<engine::graphics::Sprite>> slashGlows_;
    std::vector<std::unique_ptr<engine::graphics::Sprite>> holes_;
    std::vector<std::unique_ptr<engine::graphics::Sprite>> sparks_; ///< 着弾の瞬間に弾痕の上へ重ねる火花（弾痕と同じ数）
};

} // namespace engine::game
