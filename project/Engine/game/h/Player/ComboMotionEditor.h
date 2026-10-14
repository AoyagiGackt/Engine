/**
 * @file ComboMotionEditor.h
 * @brief コンボの段ごとの加算モーションを、時間軸のスクラブとボーン回転のキーで編集するパネルを定義するファイル
 */
#pragma once
#include <string>

namespace engine {
class Input;
} // namespace engine

namespace engine::game {

class Player;
struct MeleeAttackDef;
struct MotionClip;
enum class WeaponType;

/**
 * @brief 加算モーションのエディタ（F7）
 * @note プレビュー中はゲームの更新を止め、選んだ段を指定時刻の姿勢で見せる。
 *       角度を動かすとその時刻にキーが打たれ、すぐ見た目に反映される。
 *       データは ComboMotionLibrary（Resources/Config/combo_motion.json）に保存する。
 */
class ComboMotionEditor {
public:
    /**
     * @brief F7で開閉し、開いていればパネルを描いてプレビューを進める（Updateフェーズから呼ぶこと）
     * @param input      開閉キーの入力
     * @param player     プレビューと編集の対象
     * @param weaponType 今装備している武器（段の一覧に使う）
     * @return プレビュー中でゲームの更新を止めるべきならtrue
     */
    bool Update(engine::Input* input, Player& player, WeaponType weaponType);

    /** @brief 開いていれば閉じてプレビューを終える（シーン終了時に呼ぶ） */
    void Close(Player& player);

private:
    /** @brief 段の一覧を描き、選ばれている段を返す（無ければnullptr） */
    const MeleeAttackDef* DrawStepList(WeaponType weaponType);
    /** @brief 時間軸（スクラブ・再生・ヒット/キャンセル位置・キーの印）を描く */
    void DrawTimeline(const MeleeAttackDef& attack, const MotionClip* clip);
    /** @brief ボーンの一覧を描く */
    void DrawBoneList(const Player& player, const MotionClip* clip);
    /** @brief 選んだボーンのキーを編集する */
    void DrawKeyEditor(MotionClip& clip);
    /** @brief 保存・読み直しなどのファイル操作を描く */
    void DrawFileControls();

    bool open_ = false;
    bool previewing_ = true; ///< ゲームを止めて段の姿勢を見せるか
    bool wasPreviewing_ = false; ///< 前フレームにプレビューしていたか（終わった瞬間に姿勢を戻すため）
    bool playing_ = false; ///< 時間軸を自動で進めるか
    bool showFingers_ = false; ///< ボーン一覧に指の骨を出すか
    float time_ = 0.0f; ///< 段の経過割合
    static constexpr float kDefaultPlaySpeed = 0.5f; ///< 自動再生の速さの初期値（段の長さの倍の時間をかけて見せる）
    float playSpeed_ = kDefaultPlaySpeed; ///< 自動再生の速さ（実際の段の長さに対する倍率）
    std::string selectedAttackId_; ///< 選んでいる段の技ID
    std::string selectedBone_; ///< 選んでいるボーン名
    std::string status_; ///< 保存結果などの表示
};

} // namespace engine::game
