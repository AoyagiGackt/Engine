/**
 * @file ElementEffect.h
 * @brief 武器属性ごとの命中演出と振り中オーラを出し分ける属性エフェクト群
 */
#pragma once
#include "Vector3.h"
#include "Vector4.h"
#include <random>
#include <string>
namespace engine::graphics {
class ParticleManager;
}
namespace engine::game {

/** @brief 属性エフェクトが粒子を出すときに使う共有状態 */
struct ElementEffectContext {
    engine::graphics::ParticleManager& particles; ///< 粒子の発生先
    std::mt19937& rng; ///< ばらつき用の乱数
    float facingX; ///< プレイヤーの向き（右=+1, 左=-1）
};

/**
 * @brief 1属性分の演出を持つ基底クラス
 * @note 属性名からの取得は Find() を使い、呼び出し側で属性名を比較しない
 */
class IElementEffect {
public:
    virtual ~IElementEffect() = default;

    /**
     * @brief 敵への命中点に属性演出を出す
     * @param ctx   粒子の発生先と乱数
     * @param pos   命中位置
     * @param color 武器の演出色
     * @param scale コンボ段や連続ヒットを反映した拡大率
     */
    virtual void EmitHit(const ElementEffectContext& ctx, const Vector3& pos, const Vector4& color, float scale) const = 0;

    /**
     * @brief 振り中の武器から毎フレーム少量の属性粒子を零す
     * @param ctx   粒子の発生先と乱数
     * @param pos   武器位置（ばらつき適用済み）
     * @param color 武器の演出色
     */
    virtual void EmitAura(const ElementEffectContext& ctx, const Vector3& pos, const Vector4& color) const = 0;

    /**
     * @brief 属性名に対応するエフェクトを取得する
     * @param element 武器データの属性名（Water, Fire など）
     * @return 対応する属性が無ければ nullptr
     */
    static const IElementEffect* Find(const std::string& element);
};

} // namespace engine::game
