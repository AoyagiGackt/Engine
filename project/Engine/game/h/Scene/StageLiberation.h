/**
 * @file StageLiberation.h
 * @brief ステージをクリアしてマップへ戻った時の解放演出（クリアした入口が光る → 光の柱と閃光で画面全体が光る → 解放の文字 → 次の入口へカメラが移って開く）を段階ごとに進めるクラス
 * @note 秒数・色・文字は Resources/Config/stage_liberation.json で決める。段階は IPhase の派生クラスで、並びは固定
 */
#pragma once
#include "Sprite.h"
#include "SpriteCommon.h"
#include "Vector4.h"
#include <memory>
#include <string>

namespace engine {
class Input;
}

namespace engine::game {

class FontRenderer;

/** @brief マップへ戻った時のステージ解放演出 */
class StageLiberation {
public:
    /** @brief 調整値をファイルから読み直す（調整値エディタで保存した時に呼ぶ） */
    static void ReloadTuning();

    /** @brief 演出を始める時にマップから渡す情報 */
    struct Setup {
        int clearedIndex = 0; ///< クリアした入口の番号
        int nextIndex = -1; ///< 次に開く入口の番号（無ければ負）
        float clearedX = 0.0f; ///< クリアした入口のワールドX座標
        float nextX = 0.0f; ///< 次に開く入口のワールドX座標
        float portalScreenY = 0.0f; ///< 入口の中心の画面のY座標（光の玉を置く高さ）
    };

    /** @brief 光の柱・光の玉・画面を覆う光のスプライトを作る（マップの Initialize で1回） */
    void Initialize(engine::graphics::SpriteCommon* spriteCommon);
    /** @brief 演出を最初から始める */
    void Start(const Setup& setup);
    /** @brief 1フレーム進める（決定・攻撃・ジャンプで残りを飛ばせる） */
    void Update(const engine::Input* input);
    /** @brief 演出の途中か（間はマップがプレイヤーの操作を止め、カメラをこのクラスに合わせる） */
    bool IsPlaying() const {
        return playing_;
    }
    /** @brief カメラが見るワールドX座標 */
    float CameraX() const {
        return cameraX_;
    }
    /**
     * @brief 入口の色を、演出の進み具合に合わせて差し替える
     * @param index 入口の番号
     * @param normal 演出が無い時の色（クリア済みなら解放後の色、今のステージなら開いた色）
     * @param locked まだ行けない入口の閉じた色（次に開く入口はこの色から開いていく）
     */
    Vector4 PortalColor(int index, const Vector4& normal, const Vector4& locked) const;
    /** @brief 入口の大きさに掛ける倍率（光る瞬間と開く瞬間に一度ふくらむ） */
    float PortalScale(int index) const;
    /** @brief クリア済みの入口の色（解放されて光が残った色） */
    static Vector4 ClearedColor();
    /** @brief 光の柱・光の玉・画面を覆う光を描く（3Dの後、文字の前。SpriteCommon の共通設定を済ませた後に呼ぶ） */
    void Draw(float cameraX);
    /** @brief 解放の文字を積む（fontRenderer.Reset() の後に呼ぶ） */
    void QueueText(FontRenderer& font, float cameraX) const;

    class IPhase;
    class GlowPhase;
    class BurstPhase;
    class PanPhase;
    class OpenPhase;

private:
    /** @brief 段階を次へ進める（最後まで行ったら終える） */
    void AdvancePhase();
    /** @brief 演出を終えて、カメラと入口を最後の状態にする */
    void Finish();
    /** @brief ワールドX座標を画面のX座標へ直す */
    static float ToScreenX(float worldX, float cameraX);

    Setup setup_;
    size_t phaseIndex_ = 0;
    float phaseTimer_ = 0.0f;
    bool playing_ = false;
    float cameraX_ = 0.0f;
    float glow_ = 0.0f; ///< クリアした入口の光り具合（0〜1）
    float pillar_ = 0.0f; ///< 光の柱の濃さ（0〜1）
    float pillarWidth_ = 0.0f; ///< 光の柱の太さ（画面の座標）
    float orbSize_ = 0.0f; ///< 入口からあふれる光の玉の大きさ（画面の座標、濃さは光の柱と同じ）
    float wash_ = 0.0f; ///< 画面全体を覆う光の濃さ（0〜1）
    float textScale_ = 0.0f; ///< 解放の文字の大きさの倍率（弾んで1に落ち着く）
    float textAlpha_ = 0.0f;
    float clearedPop_ = 0.0f; ///< クリアした入口がふくらむ量（0〜1）
    float open_ = 0.0f; ///< 次の入口の開き具合（0=閉じた色、1=開いた色）
    float openPop_ = 0.0f; ///< 次の入口が開く瞬間にふくらむ量（0〜1）

    std::unique_ptr<engine::graphics::Sprite> pillarSprite_;
    std::unique_ptr<engine::graphics::Sprite> orbSprite_;
    std::unique_ptr<engine::graphics::Sprite> washSprite_;
};

} // namespace engine::game
