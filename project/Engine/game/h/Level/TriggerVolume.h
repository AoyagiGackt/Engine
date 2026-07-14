/**
 * @file TriggerVolume.h
 * @brief プレイヤーが近づいたらフラグを立てるだけの、ロジックを持たない軽量トリガー
 * @note 「何が起きるか」はノードグラフ側（GetFlag→If）が判断する分業なので、
 * ここでは GameFlags::SetFlag を呼ぶだけに徹する
 */
#pragma once
#include "LevelLoader.h"
#include "MakeAffine.h"
namespace engine::game {

class TriggerVolume {
public:
    void Init(const TriggerDesc& desc)
    {
        desc_ = desc;
        consumed_ = false;
        wasInside_ = false;
    }

    /// @brief 毎フレーム呼ぶプレイヤーが範囲内に入った瞬間だけフラグを立てる
    void Update(const Vector3& playerPos);

    const TriggerDesc& GetDesc() const { return desc_; }
    TriggerDesc& GetDesc() { return desc_; }

    /// @brief 現在プレイヤーが範囲内にいるか（StageEditorのハイライト表示用）
    bool IsInside() const { return wasInside_; }

private:
    TriggerDesc desc_;
    bool consumed_ = false; // once=true のとき、一度成立したらtrueにして以降を無視する
    bool wasInside_ = false; // 前フレーム時点で範囲内にいたか（入った瞬間の検出用）
};

} // namespace engine::game
