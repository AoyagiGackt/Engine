/**
 * @file TriggerVolume.h
 * @brief プレイヤーが近づいたらフラグを立てるだけの、ロジックを持たない軽量トリガー
 * @note 何が起きるかはノードグラフ側（GetFlag→If）が判断する分業なので、
 * ここでは GameFlags::SetFlag を呼ぶだけに徹する
 */
#pragma once
#include "LevelLoader.h"
#include "MakeAffine.h"
namespace engine::game {

/**
 * @brief プレイヤーの進入を検出して指定フラグへ通知する
 *
 * 範囲判定と一度だけ実行する状態を管理し、成立後のゲーム処理はノードグラフへ委譲する。
 */
class TriggerVolume {
public:
    /** @brief トリガー設定と進入状態を初期化する @param desc 判定範囲と通知先を含む設定 */
    void Init(const TriggerDesc& desc)
    {
        desc_ = desc;
        consumed_ = false;
        wasInside_ = false;
    }

    /** @brief プレイヤーが範囲内に入った瞬間だけフラグを立てる @param playerPos 判定するプレイヤー位置 */
    void Update(const Vector3& playerPos);

    /** @brief 読み取り専用のトリガー設定を返す @return トリガー設定の参照 */
    const TriggerDesc& GetDesc() const { return desc_; }
    /** @brief 編集可能なトリガー設定を返す @return トリガー設定の参照 */
    TriggerDesc& GetDesc() { return desc_; }

    /** @brief 現在プレイヤーが範囲内にいるか返す @return 範囲内の場合はtrue */
    bool IsInside() const { return wasInside_; }

private:
    TriggerDesc desc_;
    bool consumed_ = false; // once=true のとき、一度成立したらtrueにして以降を無視する
    bool wasInside_ = false; // 前フレーム時点で範囲内にいたか（入った瞬間の検出用）
};

} // namespace engine::game
