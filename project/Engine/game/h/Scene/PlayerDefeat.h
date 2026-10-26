/**
 * @file PlayerDefeat.h
 * @brief プレイヤーがやられた時の演出（被弾で固まる → 後ろへ吹き飛んで回る → 敵の上なら跳ねる → 倒れ込む → あがいて力尽きる → GAME OVER → 再挑戦かマップへ戻るかを選ぶ）を段階ごとに進めるクラス
 * @note 秒数・距離・文字・演出レシピ名は Resources/Config/player_defeat.json で決める。
 *       段階は IPhase の派生クラスで、並びは固定（段階を足す時は派生クラスと表に1行足す）
 */
#pragma once
#include "CollisionConfig.h"
#include "ReachPoseLayer.h"
#include "Sprite.h"
#include "SpriteCommon.h"
#include "UIMenu.h"
#include "Vector3.h"
#include <memory>
#include <random>
#include <vector>

namespace engine::graphics {
class ParticleManager;
}

namespace engine {
class Audio;
class Input;
}

namespace engine::game {

class CameraShaker;
class FontRenderer;
class Player;

/** @brief プレイヤーのやられ演出 */
class PlayerDefeat {
public:
    /** @brief 調整値をファイルから読み直す（調整値エディタで保存した時に呼ぶ） */
    static void ReloadTuning();

    /** @brief 演出を始める時にシーンから渡す、ステージの情報 */
    struct Setup {
        float stageLeft = 0.0f; ///< 吹き飛ぶ先をこの左右の範囲に収める（当たり判定ブロックの左右端）
        float stageRight = 0.0f;
        std::vector<AABB> solids; ///< 倒れ込む床を探す当たり判定ブロック
        std::vector<Vector3> enemyPositions; ///< 倒れた体が埋まらないよう避ける敵の位置（演出の間は敵が止まっている）
    };
    /** @brief 演出が借りるシーンの部品（非所有） */
    struct Services {
        Player* player = nullptr;
        engine::graphics::ParticleManager* particles = nullptr;
        CameraShaker* shaker = nullptr;
        std::mt19937* rng = nullptr;
    };

    /** @brief GAME OVER の後に選ばれた行き先 */
    enum class Choice {
        kNone, ///< まだ選ばれていない
        kRetry, ///< 同じステージをもう一度
        kMap, ///< マップへ戻る
    };

    PlayerDefeat();
    ~PlayerDefeat();

    /** @brief 暗転用のスプライトと選択肢のメニューを作る（シーンの Initialize で1回） */
    void Initialize(engine::graphics::SpriteCommon* spriteCommon, FontRenderer* font, engine::Audio* audio);
    /** @brief 演出を最初から始める（プレイヤーは今いる場所から、向いている方と逆へ吹き飛ぶ） */
    void Start(const Setup& setup, const Services& services);
    /** @brief 1フレーム進める（暗転が始まった後は決定・攻撃ボタンで最後まで飛ばせる。見せ終えた後は選択肢を操作する） */
    void Update(engine::Input* input);
    /** @brief 演出を始めてから、シーンを切り替えるまでの間か（間はシーンが操作・HUD・敵を止める） */
    bool IsActive() const {
        return active_;
    }
    /** @brief 選択肢が決定された最初の1回だけその行き先を返す（それ以外は kNone。シーンはこれで切り替え先を決める） */
    Choice ConsumeChoice();
    /** @brief GAME OVER を出し始めた最初の1回だけ true を返す（シーンはこれでゲームオーバーの曲を流す） */
    bool ConsumeGameOverShown();
    /** @brief カメラが追う位置（演出中はプレイヤーの代わりにこれを追う） */
    const Vector3& CameraFocus() const {
        return cameraFocus_;
    }
    /** @brief カメラの引き（1=ふだんの距離、1より小さいほど寄る） */
    float CameraZoom() const {
        return cameraZoom_;
    }
    /** @brief カメラを追う位置よりどれだけ上に置くか（寄るにつれてふだんの高さから下げ、上から見下ろしすぎないようにする） */
    float CameraHeight() const {
        return cameraHeight_;
    }
    /** @brief プレイヤーの位置・のけぞり・アニメを反映する（シーンがカメラを動かした後に呼ぶ） */
    void UpdatePlayerPose();
    /** @brief GAME OVER の文字を積む（シーンの Update 中、fontRenderer.Reset() の後に呼ぶ） */
    void QueueText(FontRenderer& font) const;
    /** @brief 暗転と選択肢を描く（SpriteCommon の共通設定を済ませた後、文字の Draw より前に呼ぶ） */
    void Draw();

    class IPhase;
    class HitPhase;
    class LaunchPhase;
    class EnemyBouncePhase;
    class DownPhase;
    class StrugglePhase;
    class GameOverPhase;

private:
    /** @brief 段階を次へ進める（最後まで行ったら終える） */
    void AdvancePhase();
    /** @brief 吹き飛んで最初に落ちる場所と、最後に倒れ込む床の位置を決める（落ちる先に敵がいれば敵の上で跳ねさせる） */
    void PlanLanding(const Setup& setup);
    /** @brief 手からすっぽ抜けた武器が飛んで落ちる先を決める */
    void PlanWeaponDrop(const Setup& setup);
    /** @brief 指定した横位置の真下の床に立った位置（ステージの端からは内側に収める） */
    Vector3 FloorAt(const Setup& setup, float x) const;
    /** @brief 手放した武器の飛ぶ弧と回転を進め、床に落ちた瞬間に砂ぼこりを出す */
    void UpdateWeaponDrop();
    /** @brief あがく腕を空へ伸ばす割合を決め、震えを足して腕のポーズレイヤーへ渡す（0で元の姿勢） */
    void SetStruggleReach(float weight);
    /** @brief 演出を見せ終え、暗転と GAME OVER を出し切った画で選択肢を出す */
    void OpenChoice();
    /** @brief 選択肢のカーソルを動かし、決定されたら行き先を覚える */
    void UpdateChoice(engine::Input* input);

    Services services_;
    Vector3 start_ {}; ///< やられた瞬間のプレイヤーの位置
    Vector3 landing_ {}; ///< 吹き飛んだ先で倒れ込む位置（床の上）
    Vector3 touchdown_ {}; ///< 吹き飛んで最初に落ちる位置（敵の上で跳ねるなら敵の頭の上、そうでなければ倒れた体の高さ）
    bool bounceOnEnemy_ = false; ///< 落ちる先に敵がいて、敵の上で跳ねてから倒れ込むか
    Vector3 weaponFrom_ {}; ///< 武器が手から離れた位置
    Vector3 weaponTo_ {}; ///< 武器が落ちて止まる位置（床の上）
    Vector3 weaponPosition_ {};
    float weaponSpin_ = 0.0f; ///< 武器のZ軸回りの回転
    bool weaponLanded_ = false;
    size_t phaseIndex_ = 0;
    float phaseTimer_ = 0.0f;
    float totalTimer_ = 0.0f;
    bool active_ = false;
    bool playing_ = false; ///< 段階を進めている途中か（見せ終えた後も active_ のまま最後の画を保つ）
    bool choosing_ = false; ///< 見せ終えて選択肢を出しているか
    bool decided_ = false; ///< 選択肢が決定済みか（シーンが切り替わるまでほかの操作を受け付けない）
    float choiceTimer_ = 0.0f; ///< 選択肢を出してからの時間（出た直後の連打で決まらないようにする）
    Choice choice_ = Choice::kNone; ///< 決定された行き先（シーンへまだ伝えていない）
    UIMenu menu_; ///< もう一度挑戦 / マップに戻る の選択肢
    bool gameOverShownPending_ = false; ///< GAME OVER を出し始めたことをシーンへまだ伝えていない

    Vector3 cameraFocus_ {};
    float cameraZoom_ = 1.0f;
    float cameraHeight_ = 0.0f;
    Vector3 playerPosition_ {};
    float bodyPitch_ = 0.0f; ///< 後ろへ倒れる回転（ラジアン、負で後ろへ）
    bool airborne_ = false;
    bool collapsed_ = false; ///< あがいた末に力尽きて倒れたか
    float overlayAlpha_ = 0.0f; ///< 暗転の濃さ（0〜1、JSONの色の不透明度に掛ける）
    float textDrop_ = 0.0f; ///< GAME OVER の文字が落ちてくる進み具合（0〜1）
    float textAlpha_ = 0.0f;
    ReachPoseLayer struggleReach_; ///< あがく間に腕を空へ伸ばすポーズレイヤー（演出の間だけプレイヤーへ貸す）

    std::unique_ptr<engine::graphics::Sprite> overlay_;
};

} // namespace engine::game
