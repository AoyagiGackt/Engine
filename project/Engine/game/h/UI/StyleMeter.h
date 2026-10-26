/**
 * @file StyleMeter.h
 * @brief スタイリッシュランク（D〜SSS）の採点と画面右上へのHUD描画を行うクラス
 */
#pragma once
#include "Sprite.h"
#include "SpriteCommon.h"
#include <memory>
#include <string>
#include <unordered_map>
namespace engine::game {
class FontRenderer;
using engine::graphics::Sprite;
using engine::graphics::SpriteCommon;

/**
 * @brief スタイルポイントの蓄積・減衰からランクを算出するメーター
 * @note 採点ルール:
 *       - 同じ技を連発するほど加点が減る（技IDごとの熱が冷めるまで戻らない）
 *       - 直前と違う技を出すとバリエーションボーナス
 *       - 攻撃が途切れるとポイントが減衰し、ランクも落ちていく
 */
class StyleMeter {
public:
    static constexpr int kRankCount = 7; ///< D C B A S SS SSS

    /**
     * @brief ランクゲージのバー（背景/前景）スプライトを生成する
     * @param spriteCommon スプライト描画の共通設定
     */
    void Initialize(SpriteCommon* spriteCommon);

    /**
     * @brief 攻撃ヒットを採点に登録する
     * @param moveId     技の識別子（MeleeAttackDef::id や "shot" など）
     * @param basePoints 技の基礎点（おおよそダメージに比例させる）
     */
    void RegisterHit(const std::string& moveId, float basePoints);

    /** @brief コンボの締め演出を開始する */
    void RegisterFinisher();

    /** @brief 調整値をファイルから読み直す（調整値エディタで保存した時に呼ぶ） */
    static void ReloadTuning();

    /**
     * @brief 外部で採点済みの正規化スタイル値(0.0〜1.0)を表示へ反映する
     * @note 独自にスタイル値を管理するシーンが、採点ロジックはそのままに
     *       同じ見た目のランクHUDだけを使うための入口。毎フレームUpdate()の前に呼ぶ
     */
    void SetNormalizedPoints(float t);

    /** @brief ポイント減衰・技の熱冷まし・表示アニメを1フレーム進める */
    void Update(float dt);

    /**
     * @brief 右上HUDの文字列をフォントレンダラーへ積み、バースプライトの位置を更新する
     * @note シーンの Update 中（fontRenderer.Reset() の後）に呼ぶこと
     */
    void UpdateHud(FontRenderer& font);

    /** @brief ランクゲージのバースプライトを描画する（SpriteCommon 設定済みの状態で呼ぶ） */
    void DrawHud();

    int GetRankIndex() const; ///< 0=D 〜 6=SSS
    /** @brief 今の連続ヒット数を返す */
    int GetHitCount() const {
        return hitCount_;
    }
    /** @brief このメーターを作ってからの最大連続ヒット数を返す */
    int GetBestChain() const {
        return bestChain_;
    }
    /** @brief 現在のランクに対応する表示色を返す（ランクアップ演出の色をHUDと一致させるのに使う） */
    Vector4 GetRankColor() const;
    /** @brief ランクの文字（"S" など）に対応する表示色を返す（知らない文字なら D の色） */
    static Vector4 ColorForRank(const std::string& letter);
    /** @brief 直前のUpdate()でランクが上がった瞬間ならtrue（1フレームだけ立つ、ランクアップ演出のトリガー用） */
    bool JustRankedUp() const {
        return justRankedUp_;
    }

private:
    float points_ = 0.0f; ///< スタイルポイント（0〜style_meter.json の maxPoints）
    float noHitTimer_ = 0.0f; ///< 最後のヒットからの経過秒数（減衰開始の判定）

    // 技の使用熱。高いほど同じ技の加点が減る。時間で冷める
    std::unordered_map<std::string, float> moveHeat_;
    std::string lastMoveId_;

    int hitCount_ = 0;
    int bestChain_ = 0;
    float chainTimer_ = 0.0f; ///< ヒットチェーン維持の残り秒数
    float hudAlpha_ = 0.0f; ///< HUD全体のフェード

    int prevRank_ = 0;
    bool justRankedUp_ = false; ///< 直前のUpdate()でランクが上がった瞬間か（1フレームのみ）
    float rankFlashTimer_ = 0.0f; ///< ランク変動時の演出タイマー
    float hitPopTimer_ = 0.0f; ///< ヒット加算時に文字を弾ませる

    std::unique_ptr<Sprite> barBg_;
    std::unique_ptr<Sprite> barFg_;
    std::unique_ptr<Sprite> hitTimerBarBg_;
    std::unique_ptr<Sprite> hitTimerBarFg_;
    float finisherTimer_ = 0.0f; ///< 締め演出の残り秒数

};

} // namespace engine::game
