/**
 * @file HitEffectLibrary.h
 * @brief ヒット・破壊・被弾などの演出レシピを Resources/Config/hit_effects.json から名前で引く仕組み
 * @note シーンはどの演出を出すかだけを名前で指定し、数・色・寿命・揺れの強さはJSON側が持つ
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

/** @brief 戦闘演出の配置・発生間隔・警告表示を調整する設定 */
struct SceneParticleDef {
    int hitStopLaunch = 8; ///< 打ち上げ
    int hitStopAwakenExtra = 2; ///< 覚醒中の多段ヒットの2発目以降（刻みを速く見せるため短く）
    int hitStopJuggle = 4; ///< ジャグル
    int hitStopFinish = 12; ///< フィニッシュ
    int hitStopFinisherSlash = 20; ///< 大技 本命ヒット
    int hitStopFinisherBeat = 1; ///< 大技 斬撃線が1本出るごとの小停止
    float shakeLaunchAmt = 0.35f;
    float shakeLaunchDur = 0.28f;
    float shakeFinishAmt = 0.55f;
    float shakeFinishDur = 0.40f;
    float shakeFinisherSlashAmt = 0.65f;
    float shakeFinisherSlashDur = 0.45f;
    float finisherSlashRadius = 4.5f; ///< 斬撃線の最大半長
    float finisherOverlayAlpha = 0.60f; ///< 演出中の画面暗転の濃さ
    float ghostSpawnInterval = 0.05f; ///< 残像を追加する間隔（秒）
    float ghostLifetime = 0.3f; ///< 残像が消えるまでの秒数
    float ghostMaxAlpha = 0.5f; ///< 残像の最大不透明度
    Vector4 ghostColor = { 0.4f, 0.75f, 1.0f, 1.0f }; ///< 残像の色
    float swingCenterHeight = 0.4f; ///< 敵の近接攻撃演出を出す高さ
    float beatShakeAmount = 0.06f; ///< フィニッシャー各段の揺れ幅
    float beatShakeSeconds = 0.05f; ///< フィニッシャー各段の揺れ時間
    float beatWarpImpulse = 0.12f; ///< 各段で足す画面歪み
    Vector4 releaseFlashColor = { 0.75f, 0.95f, 1.0f, 0.5f }; ///< フィニッシャー解放時の閃光色
    float releaseFlashSeconds = 0.15f; ///< 解放時の閃光時間
    float absorbTargetHeight = 0.5f; ///< 武器を吸い寄せる位置の高さ
    Vector4 absorbGlowColor = { 0.5f, 0.85f, 1.0f, 1.0f }; ///< 吸収粒子の色
    int absorbOrbCount = 10; ///< 吸収粒子の数
    float absorbOrbSpeedBase = 4.0f; ///< 吸収粒子の基準速度
    float absorbOrbSpeedStep = 0.3f; ///< 粒子ごとに足す速度
    float absorbOrbLifetime = 0.35f; ///< 吸収粒子の寿命
    float absorbOrbSize = 0.22f; ///< 吸収粒子の大きさ
    float absorbPullRate = 0.16f; ///< 1フレームあたりの吸引率
    float surfaceLerp = 0.3f; ///< 敵中心から命中演出を手前へ寄せる割合
    int extraRingCountDivisor = 2; ///< 武器の追加粒子数からリング追加数を求める除数
    float chestHeight = 0.5f; ///< プレイヤー足元から胸までの高さ
    float comboSlashGrowthPerStep = 0.5625f; ///< コンボ段ごとに足す斬線倍率
    int comboFinisherStep = 3; ///< 締め演出を出すコンボ段
    int burstColumns = 5; ///< 武器粒子の奥行き方向の列数
    int burstCenterColumn = 2; ///< 奥行き方向の中央列
    float burstSpreadZ = 0.45f; ///< 列ごとの奥行き間隔
    float burstSpeedBase = 2.0f; ///< 武器粒子の横速度の基準
    float burstSpeedStep = 0.3f; ///< 粒子ごとに足す横速度
    float burstRiseBase = 1.8f; ///< 武器粒子の上昇速度の基準
    int burstRiseSteps = 4; ///< 上昇速度の繰り返し段数
    float burstRiseStep = 0.65f; ///< 段ごとに足す上昇速度
    int minBullets = 2; ///< 発砲演出の最小粒子数
    int fallbackBullets = 4; ///< 射撃定義が無い場合の粒子数
    float fallbackSpread = 0.15f; ///< 射撃定義が無い場合の拡散係数
    float fanSpeedBase = 7.0f; ///< 扇状に出す弾煙の基準速度
    float fanSpeedStep = 1.0f; ///< 粒子ごとに足す扇状速度
    float halfRange = 0.5f; ///< 扇状分布の中心補正
    float sequenceSlashAngleStep = 1.1f; ///< 連撃の段ごとの斬線角度
    int bladeCount = 12; ///< 固有技命中時に出す刃の数
    float bladeRadius = 2.6f; ///< 刃の発生半径
    float bladeSpeedMin = 1.5f; ///< 刃の最小速度
    float bladeSpeedMax = 3.5f; ///< 刃の最大速度
    float slashLineHalfLength = 3.2f; ///< 固有技の斬線の半分の長さ
    Vector4 slashLineColor = { 0.85f, 0.97f, 1.0f, 1.0f }; ///< 固有技の斬線色
    float slashLineThickness = 8.0f; ///< 固有技の斬線の太さ
    float slashLineSeconds = 0.32f; ///< 固有技の斬線の表示時間
    float auraInterval = 0.07f; ///< 覚醒オーラの発生間隔
    float auraJitter = 0.15f; ///< 武器オーラの位置の揺らぎ幅
    float warningMinScale = 0.6f; ///< 予告開始時の着弾範囲に対する倍率
    float warningBlinkSpeed = 22.0f; ///< 予告円の点滅速度
    float warningGreenBase = 0.15f; ///< 予告円の緑成分の基準
    float warningGreenGrowth = 0.1f; ///< 予告進行に応じて足す緑成分
    float warningBlue = 0.1f; ///< 予告円の青成分
    float warningAlphaBase = 0.35f; ///< 予告円の基準不透明度
    float warningAlphaBlink = 0.4f; ///< 点滅で足す不透明度
    float warningRingGreenBase = 0.3f; ///< 予告リングの緑成分の基準
    float warningRingGreenFade = 0.15f; ///< 進行に応じて引く緑成分
    float warningRingBlueBase = 0.15f; ///< 予告リングの青成分の基準
    float warningRingBlueFade = 0.1f; ///< 進行に応じて引く青成分
};

/** @brief フィニッシャーの斬線・刃・画面歪みの演出設定 */
struct FinisherVisualDef {
    float slashLineLengthMin = 4.0f;
    float slashLineLengthMax = 9.0f;
    float slashLineThicknessMin = 3.0f;
    float slashLineThicknessMax = 7.0f;
    float slashLineLingerSeconds = 0.25f;
    Vector4 slashLineColor = { 0.75f, 0.95f, 1.0f, 1.0f };
    int beatBladeCount = 3;
    float beatBladeRadius = 4.0f;
    float beatBladeSpeedMin = 1.2f;
    float beatBladeSpeedMax = 2.8f;
    Vector4 slashFlashColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    float slashFlashSeconds = 0.22f;
    int releaseSlashCount = 8;
    float releaseSlashThickness = 9.0f;
    float releaseSlashSeconds = 0.15f;
    int releaseBladeCount = 30;
    float releaseBladeSpeedMin = 2.0f;
    float releaseBladeSpeedMax = 5.0f;
    float releaseWarpImpulse = 1.0f;
};

/**
 * @brief hit_effects.json を読み込んで保持するシングルトン
 * @note 存在しない名前を引いた時は何も出ない既定値を返し、ログに1回だけ警告を出す
 */
class HitEffectLibrary {
public:
    /** @brief 演出レシピを保存する共通の設定ファイル */
    static constexpr const char* kConfigPath = "Resources/Config/hit_effects.json";
    /** @brief 演出レシピを管理する共有インスタンスを取得する */
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

    /** @brief シーン共通の演出設定を読み取り専用で取得する */
    const SceneParticleDef& SceneParticles() const
    {
        return sceneParticles_;
    }

    /** @brief フィニッシャーの共通演出設定を参照する */
    const FinisherVisualDef& FinisherVisuals() const
    {
        return finisherVisuals_;
    }

private:
    /** @brief 設定ファイルから全レシピを読み込む */
    HitEffectLibrary();
    HitEffectLibrary(const HitEffectLibrary&) = delete;
    HitEffectLibrary& operator=(const HitEffectLibrary&) = delete;

    template <class Def>
    const Def& Find(const std::unordered_map<std::string, Def>& table, const std::string& name, const char* section) const;

    SceneParticleDef sceneParticles_;
    FinisherVisualDef finisherVisuals_;
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
