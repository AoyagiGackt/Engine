/**
 * @file GimmickMotion.h
 * @brief 仕掛け（gimmick）の動き方（往復移動・回転・崩落・点滅・自由動作）ごとの振る舞いを表すクラス群
 * @note 動き方の名前（gimmickMotion）を見るのは GimmickMotion::Of() だけにし、違いは派生クラスで表す
 */
#pragma once
#include "Vector3.h"
#include <string>
#include <vector>

namespace engine::graphics {
class ParticleManager;
}

namespace engine::game {
struct ObjectDesc;

/** @brief ある時刻での仕掛けの一時的な姿（保存する編集値には足さず、描画と当たり判定にだけ使う） */
struct GimmickPose {
    Vector3 position = { }; ///< 編集位置からのずれ
    Vector3 rotation = { }; ///< 編集回転からのずれ
    bool visible = true; ///< 描画するか
    bool solid = true; ///< 当たり判定を出すか
};

/**
 * @brief 仕掛けの動き方の基底
 * @note 動き方はステートレスで、同じ動き方の仕掛けは同じインスタンスを共有する
 */
class GimmickMotion {
public:
    virtual ~GimmickMotion() = default;

    /** @brief 動き方の名前に対応する動き方を返す（未知の名前は動かない） */
    static const GimmickMotion& Of(const std::string& name);

    /** @brief 経過時間 timer での一時的な姿を求める */
    virtual GimmickPose Evaluate(const ObjectDesc&, float) const { return { }; }
    /** @brief 可動域の向きと量（デバッグ表示用。移動しない動き方はゼロ） */
    virtual Vector3 RangeAxis(const ObjectDesc&) const { return { }; }
    /** @brief 可動域が片道か（往復しない） */
    virtual bool IsOneWay(const ObjectDesc&) const { return false; }
    /** @brief 子の配置物も一緒に回して見せるか */
    virtual bool CarriesChildren() const { return false; }
    /** @brief 移動方向・回転量・往復方式を自由に決める動き方か（インスペクタで詳細項目を出す） */
    virtual bool IsFreeform() const { return false; }
    /** @brief 動き方固有の設定ミスを issues へ追加する */
    virtual void Validate(const ObjectDesc&, std::vector<std::string>&) const { }
    /**
     * @brief 動きに伴う演出を出す（崩れる床の砂ぼこりなど）
     * @param worldPosition 一時的な姿を反映したワールド位置
     * @param wasSolid 直前フレームに当たり判定があったか（演出側が更新する）
     */
    virtual void EmitEffects(engine::graphics::ParticleManager*, const ObjectDesc&, const Vector3&, float, bool&) const { }
};

} // namespace engine::game
