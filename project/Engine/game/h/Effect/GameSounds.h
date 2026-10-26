/**
 * @file GameSounds.h
 * @brief 操作や演出に合わせて鳴らす効果音を、名前付きの場面から引いて鳴らすファイル
 * @note どの場面でどの音を鳴らすか（パスと音量）は Resources/Config/sounds.json で決める
 */
#pragma once
#include <string>

namespace engine::game {

class Player;

/** @brief 効果音を鳴らす場面 */
enum class SoundCue {
    kDodge, ///< 回避した
    kSwing, ///< 近接攻撃を振った（段ごと）
    kShot, ///< 銃を撃った
    kFinisherBeat, ///< フィニッシャーの斬撃を1本入れた
    kFinisherRelease, ///< フィニッシャーの溜めた斬撃を解き放った
    kGlassShatter, ///< 画面がガラスのように割れた
    kCount
};

namespace gameSounds {

/** @brief 効果音の割り当てを読み直す */
void Reload();

/** @brief 場面に割り当てた効果音を鳴らす（パスが空なら何もしない） */
void Play(SoundCue cue);

/**
 * @brief sounds.json の 名前_style の音を鳴らす（無いか空なら 名前 の音。それも空なら何もしない）
 * @note 武器ごとに当たった時の音を変える時に使う（style は weapons.json の hitStyle）
 */
void PlayStyled(const std::string& name, const std::string& style);

/**
 * @brief プレイヤーのこのフレームの行動（回避・攻撃の振り・射撃）に合わせて鳴らす
 * @note プレイヤーを更新したフレームだけ呼ぶ（ヒットストップ中は行動の印が残るので、毎フレーム呼ぶと鳴り続ける）
 */
void PlayPlayerActions(const Player& player);

} // namespace gameSounds
} // namespace engine::game
