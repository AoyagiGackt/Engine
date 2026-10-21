/**
 * @file StageIntro.h
 * @brief ステージ開始演出（画面を斬り開く → ステージ全体の見渡し → 奪う武器の紹介 → 降下・着地 → GO）を段階ごとに進めるクラス
 * @note 段階の並びと秒数・文字・演出レシピ名は Resources/Config/stage_intro.json で決める。
 *       段階は IPhase の派生クラスで、名前の表から生成する（段階を足す時は派生クラスと表に1行足す）
 */
#pragma once
#include "Sprite.h"
#include "SpriteCommon.h"
#include "Vector3.h"
#include "Vector4.h"
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace engine::graphics {
class ParticleManager;
}

namespace engine {
class Input;
}

namespace engine::game {

class CameraShaker;
class FontRenderer;
class Player;

/** @brief ステージ開始演出 */
class StageIntro {
public:
    /** @brief 演出を始める時にシーンから渡す、ステージの情報 */
    struct Setup {
        std::string stageKey; ///< 初回かどうかを見分けるキー（レベルJSONのパス）
        Vector3 landing {}; ///< プレイヤーが着地する位置（開始位置）
        bool hasBoss = false;
        Vector3 bossFocus {}; ///< 武器の紹介でカメラを向ける位置
        std::wstring weaponName; ///< ボスから奪える武器の表示名
        Vector4 weaponColor { 1.0f, 1.0f, 1.0f, 1.0f };
        float stageLeft = 0.0f; ///< ステージ全体を見渡す時に収める範囲（当たり判定ブロックの左右端）
        float stageRight = 0.0f;
        float stageCenterY = 0.0f; ///< 見渡す時のカメラの高さの基準
        float stageHeight = 0.0f; ///< ステージの上下の幅（全体が縦にも収まる引きを決めるのに使う）
    };
    /** @brief 演出が借りるシーンの部品（非所有） */
    struct Services {
        Player* player = nullptr;
        engine::graphics::ParticleManager* particles = nullptr;
        CameraShaker* shaker = nullptr;
        std::mt19937* rng = nullptr;
    };

    StageIntro();
    ~StageIntro();

    /** @brief 幕と斬線のスプライトを作る（シーンの Initialize で1回） */
    void Initialize(engine::graphics::SpriteCommon* spriteCommon);
    /** @brief 演出を最初から始める（このステージを見るのが2回目以降なら初回だけの段階を飛ばす） */
    void Start(const Setup& setup, const Services& services);
    /** @brief 1フレーム進める（決定・攻撃ボタンで最後まで飛ばせる） */
    void Update(const engine::Input* input);
    /** @brief 演出の途中か（途中の間はシーンが操作・HUD・敵を止める） */
    bool IsPlaying() const {
        return playing_;
    }
    /** @brief カメラが追う位置（演出中はプレイヤーの代わりにこれを追う） */
    const Vector3& CameraFocus() const {
        return cameraFocus_;
    }
    /** @brief カメラの引き（1=ふだんの距離、2なら2倍の距離から映す） */
    float CameraZoom() const {
        return cameraZoom_;
    }
    /** @brief 演出の文字を積む（シーンの Update 中、fontRenderer.Reset() の後に呼ぶ。演出の後も GO! が残っていれば積む） */
    void QueueText(FontRenderer& font) const;
    /** @brief 幕と斬線を描く（SpriteCommon の共通設定を済ませた後に呼ぶ） */
    void Draw();

    class IPhase;
    class SlashPhase;
    class OverviewPhase;
    class BossPhase;
    class DropPhase;
    class LandPhase;

private:
    /** @brief 段階を次へ進める（最後まで行ったら終える） */
    void AdvancePhase();
    /** @brief 残りを飛ばして、操作できる状態にする（出ている GO! の文字は残す） */
    void Finish();
    /** @brief GO! の文字を出してからの時間を進め、出し終えたら消す */
    void AdvanceGoText();
    /** @brief GO! の文字を積む（大きく弾んで出て、最後の文字のフェード時間で消える） */
    void QueueGoText(FontRenderer& font) const;
    void SetPlayerPosition(const Vector3& position);
    /** @brief 演出中のプレイヤーの向き・アニメ・武器の付け方を整える（入力と物理は進めない） */
    void UpdatePlayerPose();

    Setup setup_;
    Services services_;
    std::vector<const IPhase*> phases_;
    size_t phaseIndex_ = 0;
    float phaseTimer_ = 0.0f;
    float totalTimer_ = 0.0f;
    bool playing_ = false;

    Vector3 cameraFocus_ {};
    float cameraZoom_ = 1.0f;
    Vector3 zoomInTarget_ {}; ///< 見渡した後にカメラが寄っていく先（武器紹介があればボス、無ければ開始位置）
    bool hasOverview_ = false; ///< ステージ全体を見渡す段階があるか
    Vector3 overviewFocus_ {};
    float overviewZoom_ = 1.0f;
    float curtainOpen_ = 1.0f; ///< 幕の開き具合（0=閉じて真っ暗、1=開き切った）
    float slashProgress_ = 0.0f; ///< 斬線の伸び（0〜1）
    float slashAlpha_ = 0.0f;
    bool slashFlashed_ = false;
    float textAlpha_ = 0.0f; ///< 武器紹介の文字の濃さ
    float goTimer_ = -1.0f; ///< GO の文字を出してからの秒数（負なら出していない）

    std::unique_ptr<engine::graphics::Sprite> curtainTop_;
    std::unique_ptr<engine::graphics::Sprite> curtainBottom_;
    std::unique_ptr<engine::graphics::Sprite> slashLine_;
};

} // namespace engine::game
