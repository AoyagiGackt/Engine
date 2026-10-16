/**
 * @file ElementEffect.cpp
 * @brief 武器属性ごとの命中演出と振り中オーラの実装
 * @note 属性ごとの見た目は Resources/Config/element_effects.json に「層」の並びとして書く。
 *       層の種類（リング・ばらまき・扇・斬線・細長い光・漂う粒・吸い込み）ごとにクラスを分け、
 *       JSONの "type" から対応するクラスを作る
 */
#include "ElementEffect.h"
#include "JsonFieldTable.h"
#include "JsonHelper.h"
#include "Logger.h"
#include "ParticleManager.h"
#include <algorithm>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>
using namespace engine;
using namespace engine::graphics;
using namespace engine::game;

namespace {
constexpr const char* kElementEffectsPath = "Resources/Config/element_effects.json";
constexpr size_t kColorChannels = 4;

/** @brief -1〜1 の一様乱数を返す */
float RandomUnit(std::mt19937& rng)
{
    std::uniform_real_distribution<float> unitD(-1.0f, 1.0f);
    return unitD(rng);
}

/** @brief min〜max の一様乱数を返す（min==max ならその値） */
float RandomRange(std::mt19937& rng, float min, float max)
{
    if (min >= max) {
        return min;
    }
    std::uniform_real_distribution<float> rangeD(min, max);
    return rangeD(rng);
}

/** @brief 0〜1 の確率で true を返す */
bool Chance(std::mt19937& rng, float probability)
{
    std::uniform_real_distribution<float> unitD(0.0f, 1.0f);
    return unitD(rng) < probability;
}

/**
 * @brief 層の色の指定（"weapon" なら武器の演出色、配列なら固定色）と、混ぜる差し色
 * @note 差し色は altEvery 粒ごと（i % altEvery == altPhase の粒）または altChance の確率で使う
 */
struct LayerColor {
    bool useWeapon = true;
    Vector4 fixed = { 1.0f, 1.0f, 1.0f, 1.0f };
    bool hasAlt = false;
    Vector4 alt = { 1.0f, 1.0f, 1.0f, 1.0f };
    int altEvery = 0;
    int altPhase = 0;
    float altChance = 0.0f;

    Vector4 Pick(const Vector4& weapon, int index, std::mt19937& rng) const
    {
        const Vector4 base = useWeapon ? weapon : fixed;
        if (!hasAlt) {
            return base;
        }
        const bool everyHit = altEvery > 0 && (index % altEvery) == altPhase;
        const bool chanceHit = altChance > 0.0f && Chance(rng, altChance);
        return (everyHit || chanceHit) ? alt : base;
    }
};

Vector4 ReadColor(const nlohmann::json& value, const Vector4& fallback)
{
    if (!value.is_array() || value.size() != kColorChannels) {
        return fallback;
    }
    return { value[0].get<float>(), value[1].get<float>(), value[2].get<float>(), value[3].get<float>() };
}

LayerColor ReadLayerColor(const nlohmann::json& json)
{
    LayerColor color;
    const nlohmann::json main = json.value("color", nlohmann::json("weapon"));
    color.useWeapon = main.is_string();
    color.fixed = ReadColor(main, color.fixed);
    if (json.contains("altColor")) {
        color.hasAlt = true;
        color.alt = ReadColor(json["altColor"], color.alt);
        color.altEvery = json.value("altEvery", 0);
        color.altPhase = json.value("altPhase", 0);
        color.altChance = json.value("altChance", 0.0f);
    }
    return color;
}

/** @brief 層の共通項目（粒の種類・数・寿命・大きさ・拡大率を大きさへ掛けるか・出す確率） */
struct LayerBase {
    std::string particle = "hit_spark";
    int count = 1;
    float lifetime = 0.0f;
    float size = 0.0f;
    bool scaleSize = false;
    float chance = 1.0f; ///< 毎回出すとうるさい層は確率で間引く

    float Size(float scale) const { return scaleSize ? size * scale : size; }
};
constexpr JsonField<LayerBase> kLayerBaseFields[] = {
    { nullptr, "particle", &LayerBase::particle },
    { nullptr, "count", &LayerBase::count },
    { nullptr, "lifetime", &LayerBase::lifetime },
    { nullptr, "size", &LayerBase::size },
    { nullptr, "scaleSize", &LayerBase::scaleSize },
    { nullptr, "chance", &LayerBase::chance },
};

/** @brief 層ごとの設定値をJSONから読む（共通項目・色・その層だけの項目） */
template <class Params, size_t N>
void ReadLayer(const nlohmann::json& json, const JsonField<Params> (&fields)[N],
    LayerBase& base, LayerColor& color, Params& params)
{
    ReadJsonFields(json, kLayerBaseFields, base);
    color = ReadLayerColor(json);
    ReadJsonFields(json, fields, params);
}

/** @brief 属性演出の1層 */
class IElementLayer {
public:
    virtual ~IElementLayer() = default;
    virtual void Emit(const ElementEffectContext& ctx, const Vector3& pos, const Vector4& weaponColor, float scale) const = 0;
};

/** @brief 放射状に広がるリングの設定 */
struct RingParams {
    float speed = 1.0f;
    bool scaleSpeed = false;
};
constexpr JsonField<RingParams> kRingFields[] = {
    { nullptr, "speed", &RingParams::speed },
    { nullptr, "scaleSpeed", &RingParams::scaleSpeed },
};

/** @brief 放射状に広がるリング */
class RingLayer final : public IElementLayer {
public:
    explicit RingLayer(const nlohmann::json& json)
    {
        base_.particle = "hit_ring";
        ReadLayer(json, kRingFields, base_, color_, params_);
    }
    void Emit(const ElementEffectContext& ctx, const Vector3& pos, const Vector4& weaponColor, float scale) const override
    {
        ctx.particles.EmitRing(base_.particle, pos, params_.scaleSpeed ? params_.speed * scale : params_.speed,
            color_.Pick(weaponColor, 0, ctx.rng), static_cast<uint32_t>(base_.count), base_.lifetime, base_.Size(scale));
    }

private:
    LayerBase base_;
    LayerColor color_;
    RingParams params_;
};

/** @brief 左右へばらまく粒の設定（横速度は ±speedX、上向きは riseMin〜riseMax。riseMax 省略時は riseMin） */
struct ScatterParams {
    float speedX = 0.0f;
    float riseMin = 0.0f;
    float riseMax = 0.0f;
};
constexpr JsonField<ScatterParams> kScatterFields[] = {
    { nullptr, "speedX", &ScatterParams::speedX },
    { nullptr, "riseMin", &ScatterParams::riseMin },
    { nullptr, "riseMax", &ScatterParams::riseMax },
};

/** @brief 左右へばらまいて重力で落とす粒 */
class ScatterLayer final : public IElementLayer {
public:
    explicit ScatterLayer(const nlohmann::json& json)
    {
        ReadLayer(json, kScatterFields, base_, color_, params_);
        if (!json.contains("riseMax")) {
            params_.riseMax = params_.riseMin;
        }
    }
    void Emit(const ElementEffectContext& ctx, const Vector3& pos, const Vector4& weaponColor, float scale) const override
    {
        for (int i = 0; i < base_.count; ++i) {
            ctx.particles.EmitGravity(base_.particle, pos,
                { RandomUnit(ctx.rng) * params_.speedX, RandomRange(ctx.rng, params_.riseMin, params_.riseMax), 0.0f },
                color_.Pick(weaponColor, i, ctx.rng), base_.lifetime, base_.Size(scale));
        }
    }

private:
    LayerBase base_;
    LayerColor color_;
    ScatterParams params_;
};

/**
 * @brief 規則的に並べる粒の設定
 * @note 横速度 = (i % columns - centerColumn) * columnSpacing + 向き * (facingSpeed + i * facingSpeedStep)
 *       上向き = riseBase + (i % riseSteps) * riseStep
 */
struct FanParams {
    int columns = 1;
    int centerColumn = 0;
    float columnSpacing = 0.0f;
    float facingSpeed = 0.0f;
    float facingSpeedStep = 0.0f;
    float riseBase = 0.0f;
    int riseSteps = 1;
    float riseStep = 0.0f;
};
constexpr JsonField<FanParams> kFanFields[] = {
    { nullptr, "columns", &FanParams::columns },
    { nullptr, "centerColumn", &FanParams::centerColumn },
    { nullptr, "columnSpacing", &FanParams::columnSpacing },
    { nullptr, "facingSpeed", &FanParams::facingSpeed },
    { nullptr, "facingSpeedStep", &FanParams::facingSpeedStep },
    { nullptr, "riseBase", &FanParams::riseBase },
    { nullptr, "riseSteps", &FanParams::riseSteps },
    { nullptr, "riseStep", &FanParams::riseStep },
};

/** @brief 規則的に並べて重力で落とす粒 */
class FanLayer final : public IElementLayer {
public:
    explicit FanLayer(const nlohmann::json& json)
    {
        ReadLayer(json, kFanFields, base_, color_, params_);
        params_.columns = (std::max)(params_.columns, 1);
        params_.riseSteps = (std::max)(params_.riseSteps, 1);
    }
    void Emit(const ElementEffectContext& ctx, const Vector3& pos, const Vector4& weaponColor, float scale) const override
    {
        for (int i = 0; i < base_.count; ++i) {
            const float column = static_cast<float>((i % params_.columns) - params_.centerColumn) * params_.columnSpacing;
            const float forward = ctx.facingX * (params_.facingSpeed + static_cast<float>(i) * params_.facingSpeedStep);
            const float rise = params_.riseBase + static_cast<float>(i % params_.riseSteps) * params_.riseStep;
            ctx.particles.EmitGravity(base_.particle, pos, { column + forward, rise, 0.0f },
                color_.Pick(weaponColor, i, ctx.rng), base_.lifetime, base_.Size(scale));
        }
    }

private:
    LayerBase base_;
    LayerColor color_;
    FanParams params_;
};

/** @brief 斬線の設定（右向きと左向きで角度を変える。angleLeft 省略時は angleRight と同じ） */
struct SlashParams {
    float angleRight = 0.0f;
    float angleLeft = 0.0f;
    float radius = 1.0f;
    bool scaleRadius = false;
};
constexpr JsonField<SlashParams> kSlashFields[] = {
    { nullptr, "angleRight", &SlashParams::angleRight },
    { nullptr, "angleLeft", &SlashParams::angleLeft },
    { nullptr, "radius", &SlashParams::radius },
    { nullptr, "scaleRadius", &SlashParams::scaleRadius },
};

/** @brief 斬線 */
class SlashLayer final : public IElementLayer {
public:
    explicit SlashLayer(const nlohmann::json& json)
    {
        base_.particle = "sword_slash";
        ReadLayer(json, kSlashFields, base_, color_, params_);
        if (!json.contains("angleLeft")) {
            params_.angleLeft = params_.angleRight;
        }
    }
    void Emit(const ElementEffectContext& ctx, const Vector3& pos, const Vector4& weaponColor, float scale) const override
    {
        ctx.particles.EmitSlash(base_.particle, pos, ctx.facingX > 0.0f ? params_.angleRight : params_.angleLeft,
            color_.Pick(weaponColor, 0, ctx.rng), params_.scaleRadius ? params_.radius * scale : params_.radius);
    }

private:
    LayerBase base_;
    LayerColor color_;
    SlashParams params_;
};

/** @brief 細長い光の設定（横速度は向き * facingSpeed） */
struct EllipseParams {
    float facingSpeed = 0.0f;
    float riseSpeed = 0.0f;
    float length = 1.0f;
    float thickness = 0.1f;
    bool vertical = false; ///< true なら縦長
    bool scaleLength = false;
};
constexpr JsonField<EllipseParams> kEllipseFields[] = {
    { nullptr, "facingSpeed", &EllipseParams::facingSpeed },
    { nullptr, "riseSpeed", &EllipseParams::riseSpeed },
    { nullptr, "length", &EllipseParams::length },
    { nullptr, "thickness", &EllipseParams::thickness },
    { nullptr, "vertical", &EllipseParams::vertical },
    { nullptr, "scaleLength", &EllipseParams::scaleLength },
};

/** @brief 細長い光（放電・風の筋） */
class EllipseLayer final : public IElementLayer {
public:
    explicit EllipseLayer(const nlohmann::json& json)
    {
        ReadLayer(json, kEllipseFields, base_, color_, params_);
    }
    void Emit(const ElementEffectContext& ctx, const Vector3& pos, const Vector4& weaponColor, float scale) const override
    {
        if (!Chance(ctx.rng, base_.chance)) {
            return;
        }
        const float length = params_.scaleLength ? params_.length * scale : params_.length;
        ctx.particles.EmitEllipse(base_.particle, pos, { ctx.facingX * params_.facingSpeed, params_.riseSpeed, 0.0f },
            color_.Pick(weaponColor, 0, ctx.rng), base_.lifetime,
            params_.vertical ? params_.thickness : length, params_.vertical ? length : params_.thickness);
    }

private:
    LayerBase base_;
    LayerColor color_;
    EllipseParams params_;
};

/**
 * @brief 漂う粒の設定（riseMax 省略時は riseMin）
 * @note 速度 = 乱数(±driftX, ±driftY) + (riseMin〜riseMax) + 格子 ((i % gridColumnsX - gridCenterX) * gridSpeedX, 縦も同様)
 */
struct MoteParams {
    float driftX = 0.0f;
    float driftY = 0.0f;
    float riseMin = 0.0f;
    float riseMax = 0.0f;
    int gridColumnsX = 1;
    int gridColumnsY = 1;
    int gridCenterX = 0;
    int gridCenterY = 0;
    float gridSpeedX = 0.0f;
    float gridSpeedY = 0.0f;
    bool flicker = false;
};
constexpr JsonField<MoteParams> kMoteFields[] = {
    { nullptr, "driftX", &MoteParams::driftX },
    { nullptr, "driftY", &MoteParams::driftY },
    { nullptr, "riseMin", &MoteParams::riseMin },
    { nullptr, "riseMax", &MoteParams::riseMax },
    { nullptr, "gridColumnsX", &MoteParams::gridColumnsX },
    { nullptr, "gridColumnsY", &MoteParams::gridColumnsY },
    { nullptr, "gridCenterX", &MoteParams::gridCenterX },
    { nullptr, "gridCenterY", &MoteParams::gridCenterY },
    { nullptr, "gridSpeedX", &MoteParams::gridSpeedX },
    { nullptr, "gridSpeedY", &MoteParams::gridSpeedY },
    { nullptr, "flicker", &MoteParams::flicker },
};

/** @brief 重力を受けずに漂う粒（明滅させられる） */
class MoteLayer final : public IElementLayer {
public:
    explicit MoteLayer(const nlohmann::json& json)
    {
        ReadLayer(json, kMoteFields, base_, color_, params_);
        if (!json.contains("riseMax")) {
            params_.riseMax = params_.riseMin;
        }
        params_.gridColumnsX = (std::max)(params_.gridColumnsX, 1);
        params_.gridColumnsY = (std::max)(params_.gridColumnsY, 1);
    }
    void Emit(const ElementEffectContext& ctx, const Vector3& pos, const Vector4& weaponColor, float scale) const override
    {
        for (int i = 0; i < base_.count; ++i) {
            const float gridX = static_cast<float>((i % params_.gridColumnsX) - params_.gridCenterX) * params_.gridSpeedX;
            const float gridY = static_cast<float>((i % params_.gridColumnsY) - params_.gridCenterY) * params_.gridSpeedY;
            const Vector3 velocity = {
                RandomUnit(ctx.rng) * params_.driftX + gridX,
                RandomUnit(ctx.rng) * params_.driftY + RandomRange(ctx.rng, params_.riseMin, params_.riseMax) + gridY,
                0.0f,
            };
            ctx.particles.EmitWithColor(base_.particle, pos, velocity,
                color_.Pick(weaponColor, i, ctx.rng), base_.lifetime, base_.Size(scale), params_.flicker);
        }
    }

private:
    LayerBase base_;
    LayerColor color_;
    MoteParams params_;
};

/** @brief 吸い込まれる粒の設定（半径 radius の中に出し、中心へ向かって pullSpeed 倍で動く） */
struct PullParams {
    float radius = 0.0f;
    float pullSpeed = 0.0f;
};
constexpr JsonField<PullParams> kPullFields[] = {
    { nullptr, "radius", &PullParams::radius },
    { nullptr, "pullSpeed", &PullParams::pullSpeed },
};

/** @brief 周りから中心へ吸い込まれる粒 */
class PullLayer final : public IElementLayer {
public:
    explicit PullLayer(const nlohmann::json& json)
    {
        ReadLayer(json, kPullFields, base_, color_, params_);
    }
    void Emit(const ElementEffectContext& ctx, const Vector3& pos, const Vector4& weaponColor, float scale) const override
    {
        for (int i = 0; i < base_.count; ++i) {
            const Vector3 offset = { RandomUnit(ctx.rng) * params_.radius, RandomUnit(ctx.rng) * params_.radius, 0.0f };
            ctx.particles.EmitWithColor(base_.particle, { pos.x + offset.x, pos.y + offset.y, pos.z },
                { -offset.x * params_.pullSpeed, -offset.y * params_.pullSpeed, 0.0f },
                color_.Pick(weaponColor, i, ctx.rng), base_.lifetime, base_.Size(scale));
        }
    }

private:
    LayerBase base_;
    LayerColor color_;
    PullParams params_;
};

using LayerFactory = std::function<std::unique_ptr<IElementLayer>(const nlohmann::json&)>;

/** @brief JSONの "type" から層を作る表 */
const std::unordered_map<std::string, LayerFactory>& LayerFactories()
{
    static const std::unordered_map<std::string, LayerFactory> factories = {
        { "ring", [](const nlohmann::json& j) { return std::make_unique<RingLayer>(j); } },
        { "scatter", [](const nlohmann::json& j) { return std::make_unique<ScatterLayer>(j); } },
        { "fan", [](const nlohmann::json& j) { return std::make_unique<FanLayer>(j); } },
        { "slash", [](const nlohmann::json& j) { return std::make_unique<SlashLayer>(j); } },
        { "ellipse", [](const nlohmann::json& j) { return std::make_unique<EllipseLayer>(j); } },
        { "mote", [](const nlohmann::json& j) { return std::make_unique<MoteLayer>(j); } },
        { "pull", [](const nlohmann::json& j) { return std::make_unique<PullLayer>(j); } },
    };
    return factories;
}

/** @brief JSONに書かれた層を重ねて出す属性演出 */
class LayeredElementEffect final : public IElementEffect {
public:
    LayeredElementEffect(const std::string& element, const nlohmann::json& json)
    {
        ReadLayers(element, json.value("hit", nlohmann::json::array()), hitLayers_);
        ReadLayers(element, json.value("aura", nlohmann::json::array()), auraLayers_);
    }

    void EmitHit(const ElementEffectContext& ctx, const Vector3& pos, const Vector4& color, float scale) const override
    {
        for (const auto& layer : hitLayers_) {
            layer->Emit(ctx, pos, color, scale);
        }
    }

    void EmitAura(const ElementEffectContext& ctx, const Vector3& pos, const Vector4& color) const override
    {
        for (const auto& layer : auraLayers_) {
            layer->Emit(ctx, pos, color, 1.0f);
        }
    }

private:
    static void ReadLayers(const std::string& element, const nlohmann::json& list,
        std::vector<std::unique_ptr<IElementLayer>>& out)
    {
        for (const nlohmann::json& layerJson : list) {
            const std::string type = layerJson.value("type", "");
            const auto& factories = LayerFactories();
            const auto factory = factories.find(type);
            if (factory == factories.end()) {
                Logger::LogWarning("ElementEffect: " + element + " の層の種類 '" + type + "' は未対応です");
                continue;
            }
            out.push_back(factory->second(layerJson));
        }
    }

    std::vector<std::unique_ptr<IElementLayer>> hitLayers_;
    std::vector<std::unique_ptr<IElementLayer>> auraLayers_;
};

/** @brief 属性名からエフェクトを引く表を element_effects.json から作る */
std::unordered_map<std::string, std::unique_ptr<IElementEffect>> BuildRegistry()
{
    std::unordered_map<std::string, std::unique_ptr<IElementEffect>> registry;
    const nlohmann::json root = JsonHelper::Load(kElementEffectsPath);
    if (!root.is_object()) {
        Logger::LogWarning(std::string("ElementEffect: ") + kElementEffectsPath + " が読めません。属性演出は出ません");
        return registry;
    }
    for (auto it = root.begin(); it != root.end(); ++it) {
        if (it.value().is_object()) {
            registry.emplace(it.key(), std::make_unique<LayeredElementEffect>(it.key(), it.value()));
        }
    }
    return registry;
}

} // namespace

const IElementEffect* IElementEffect::Find(const std::string& element)
{
    static const auto registry = BuildRegistry();
    const auto it = registry.find(element);
    return it != registry.end() ? it->second.get() : nullptr;
}
