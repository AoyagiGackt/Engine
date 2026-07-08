/**
 * @file StyleMeter.h
 * @brief DMC風スタイリッシュランク（D〜SSS）の採点と画面右上へのHUD描画を行うクラス
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
 *       - 同じ技を連発するほど加点が減る（技IDごとの「熱」が冷めるまで戻らない）
 *       - 直前と違う技を出すとバリエーションボーナス
 *       - 攻撃が途切れるとポイントが減衰し、ランクも落ちていく
 */
class StyleMeter {
public:
    static constexpr int kRankCount = 7; ///< D C B A S SS SSS

    void Initialize(SpriteCommon* spriteCommon);

    /**
     * @brief 攻撃ヒットを採点に登録する
     * @param moveId     技の識別子（MeleeAttackDef::id や "shot" など）
     * @param basePoints 技の基礎点（おおよそダメージに比例させる）
     */
    void RegisterHit(const std::string& moveId, float basePoints);

    /** @brief ポイント減衰・技の熱冷まし・表示アニメを1フレーム進める */
    void Update(float dt);

    /**
     * @brief 右上HUDの文字列をフォントレンダラーへ積み、バースプライトの位置を更新する
     * @note シーンの Update 中（fontRenderer.Reset() の後）に呼ぶこと
     */
    void UpdateHud(FontRenderer& font);

    /** @brief ランクゲージのバースプライトを描画する（SpriteCommon 設定済みの状態で呼ぶ） */
    void DrawHud();

    int GetRankIndex() const;                       ///< 0=D 〜 6=SSS
    int GetHitCount()  const { return hitCount_; }  ///< 現在のヒットチェーン数

private:
    float points_ = 0.0f;    ///< スタイルポイント（0〜kMaxPoints）
    float noHitTimer_ = 0.0f; ///< 最後のヒットからの経過秒数（減衰開始の判定）

    // 技の使用「熱」。高いほど同じ技の加点が減る。時間で冷める
    std::unordered_map<std::string, float> moveHeat_;
    std::string lastMoveId_;

    int   hitCount_   = 0;
    int   bestChain_  = 0;
    float chainTimer_ = 0.0f; ///< ヒットチェーン維持の残り秒数
    float hudAlpha_   = 0.0f; ///< HUD全体のフェード

    int   prevRank_       = 0;
    float rankFlashTimer_ = 0.0f; ///< ランク変動時の演出タイマー
    float hitPopTimer_    = 0.0f; ///< ヒット加算時に文字を弾ませる

    std::unique_ptr<Sprite> barBg_;
    std::unique_ptr<Sprite> barFg_;

    static constexpr float kMaxPoints  = 1000.0f;
    static constexpr float kChainKeep  = 3.0f;  ///< ヒットチェーンが切れるまでの秒数
    static constexpr float kDecayGrace = 1.2f;  ///< 攻撃をやめてから減衰が始まるまでの秒数
    static constexpr float kHeatCool   = 0.30f; ///< 技の熱が1秒あたりに冷める量
};

} // namespace engine::game
