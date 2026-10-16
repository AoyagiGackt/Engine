/**
 * @file HitEffectLibrary.h
 * @brief ヒット・破壊・被弾などの演出レシピを Resources/Config/hit_effects.json から名前で引く仕組み
 * @note シーンは「どの演出を出すか」だけを名前で指定し、数・色・寿命・揺れの強さはJSON側が持つ
 */
#pragma once
#include "MakeAffine.h"
#include <optional>
#include <random>
#include <string>
#include <unordered_map>

namespace engine::graphics {
class ParticleManager;
}

namespace engine::game {
class CameraShaker;

/** @brief ヒットストップとカメラ揺れ */
struct ImpactDef {
    int hitStopFrames = 0;
    float shakeAmount = 0.0f;
    float shakeSeconds = 0.0f;
};

/**
 * @brief 放射状に広がるリング
 * @note speed は呼び出し側が速度（半径）を渡さない時の値。渡した時はそれに speed を掛ける（倍率として使う）
 */
struct RingBurstDef {
    std::string particle = "hit_ring";
    float speed = 1.0f;
    Vector4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
    int count = 0;
    float lifetime = 0.0f;
    float size = 0.0f;
    float height = 0.0f; ///< 出す位置を上へずらす量
};

/** @brief 左右へ飛び散って重力で落ちる火花（横速度は左右交互、絶対値が speedXMin〜speedXMax） */
struct SparkBurstDef {
    std::string particle = "hit_spark";
    float speedXMin = 0.0f;
    float speedXMax = 0.0f;
    float riseMin = 0.0f;
    float riseMax = 0.0f;
    int count = 0;
    Vector4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
    Vector4 altColor = { 1.0f, 1.0f, 1.0f, 1.0f }; ///< alternateColor の時に1粒おきに使う色
    bool alternateColor = false;
    float lifetime = 0.0f;
    float size = 0.0f;
    float height = 0.0f;
};

/** @brief 斬線（右向きと左向きで角度を変える） */
struct SlashDef {
    std::string particle = "sword_slash";
    float angleRight = 0.0f;
    float angleLeft = 0.0f;
    Vector4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
    float radius = 1.0f;
    float height = 0.0f;
};

/** @brief 画面全体のフラッシュ */
struct FlashDef {
    Vector4 color = { 1.0f, 1.0f, 1.0f, 0.0f };
    float seconds = 0.0f;
};

/** @brief 星形のきらめき */
struct StarDef {
    std::string particle = "hit_spark";
    Vector4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
    float height = 0.0f;
};

/** @brief 1粒だけの粒子（曳光弾の軌跡・オーラなど毎フレーム出すもの） */
struct ParticleDef {
    std::string particle;
    Vector4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
    float lifetime = 0.0f;
    float size = 0.0f;
    float riseSpeed = 0.0f; ///< 上向きの初速
    float height = 0.0f;
    bool flicker = false; ///< 明滅させるか
    bool gravity = false; ///< 重力で落とすか（velocity と組み合わせて火花にする）
};

/** @brief 武器の残像（色は呼び出し側が武器の色を渡す） */
struct TrailDef {
    std::string particle = "weapon_trail";
    float size = 0.0f;
    float lifetime = 0.0f;
    float height = 0.0f;
};

/**
 * @brief hit_effects.json を読み込んで保持するシングルトン
 * @note 存在しない名前を引いた時は何も出ない既定値を返し、ログに1回だけ警告を出す
 */
class HitEffectLibrary {
public:
    static HitEffectLibrary* GetInstance();

    /** @brief JSONを読み直す */
    void Reload();

    const ImpactDef& Impact(const std::string& name) const;
    const RingBurstDef& Ring(const std::string& name) const;
    const SparkBurstDef& Sparks(const std::string& name) const;
    const SlashDef& Slash(const std::string& name) const;
    const FlashDef& Flash(const std::string& name) const;
    const StarDef& Star(const std::string& name) const;
    const ParticleDef& Particle(const std::string& name) const;
    const TrailDef& Trail(const std::string& name) const;
    const Vector4& Color(const std::string& name) const;

private:
    HitEffectLibrary();
    HitEffectLibrary(const HitEffectLibrary&) = delete;
    HitEffectLibrary& operator=(const HitEffectLibrary&) = delete;

    template <class Def>
    const Def& Find(const std::unordered_map<std::string, Def>& table, const std::string& name, const char* section) const;

    std::unordered_map<std::string, ImpactDef> impacts_;
    std::unordered_map<std::string, RingBurstDef> rings_;
    std::unordered_map<std::string, SparkBurstDef> sparks_;
    std::unordered_map<std::string, SlashDef> slashes_;
    std::unordered_map<std::string, FlashDef> flashes_;
    std::unordered_map<std::string, StarDef> stars_;
    std::unordered_map<std::string, ParticleDef> particles_;
    std::unordered_map<std::string, TrailDef> trails_;
    std::unordered_map<std::string, Vector4> colors_;
};

/**
 * @brief レシピに上乗せする、その場で決まる値（強さ・武器の色・爆風の半径など）
 * @note 指定初期化子で必要な項目だけ書く。例: { .scale = strength, .tint = weaponRgb }
 */
struct EffectStyle {
    float scale = 1.0f; ///< 速度（半径）と大きさに掛ける倍率
    std::optional<float> speed; ///< リング: 指定するとレシピの speed を倍率として掛けた値を速度にする
    std::optional<float> radius; ///< 斬線: 指定するとレシピの radius の代わりに使う
    std::optional<float> angle; ///< 斬線: 指定すると向きに関係なくこの角度（ラジアン）にする
    std::optional<Vector3> tint; ///< RGBを差し替える（不透明度はレシピのまま）
    std::optional<Vector4> color; ///< 色を差し替える（不透明度にはレシピの色の不透明度を掛ける）
    float alphaScale = 1.0f; ///< 不透明度に掛ける倍率
    int extraCount = 0; ///< レシピの数に足す
    std::optional<Vector3> velocity; ///< 1粒: 初速を差し替える
};

/**
 * @brief 演出レシピを実際に出す（シーンが持つパーティクル・カメラ揺れ・乱数を借りる）
 * @note 使う時に作る軽い窓口
 */
class HitEffectPlayer {
public:
    HitEffectPlayer(engine::graphics::ParticleManager* particles, CameraShaker* shaker, std::mt19937* rng);

    /** @brief ヒットストップとカメラ揺れ（style.scale は揺れの強さに掛ける） */
    void Impact(const std::string& name, const EffectStyle& style = {}) const;
    void Ring(const std::string& name, const Vector3& position, const EffectStyle& style = {}) const;
    void Sparks(const std::string& name, const Vector3& position, const EffectStyle& style = {}) const;
    /** @brief 斬線を出す（facingX が正なら右向きの角度） */
    void Slash(const std::string& name, const Vector3& position, float facingX, const EffectStyle& style = {}) const;
    void Flash(const std::string& name, const EffectStyle& style = {}) const;
    void Star(const std::string& name, const Vector3& position, const EffectStyle& style = {}) const;
    void Particle(const std::string& name, const Vector3& position, const EffectStyle& style = {}) const;
    /** @brief 武器の残像を1粒出す（毎フレーム呼んで軌跡にする） */
    void Trail(const std::string& name, const Vector3& position, const Vector4& color, const EffectStyle& style = {}) const;

private:
    engine::graphics::ParticleManager* particles_;
    CameraShaker* shaker_;
    std::mt19937* rng_;
};

} // namespace engine::game
