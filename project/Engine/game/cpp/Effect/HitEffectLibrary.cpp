/**
 * @file HitEffectLibrary.cpp
 * @brief 演出レシピの読み込みと、レシピどおりに演出を出す処理を実装するファイル
 */
#include "HitEffectLibrary.h"
#include "CameraShaker.h"
#include "JsonHelper.h"
#include "Logger.h"
#include "ParticleManager.h"
#include "ScreenFlash.h"
#include "TimeManager.h"
#include <type_traits>
#include <unordered_set>
#include <variant>
using namespace engine;
using namespace engine::game;
using engine::graphics::ParticleManager;

namespace {
constexpr const char* kHitEffectsPath = "Resources/Config/hit_effects.json";
constexpr size_t kColorChannels = 4;

/** @brief レシピの1項目と、それを書き込むメンバの対応 */
template <class Def>
struct RecipeField {
    const char* key;
    std::variant<int Def::*, float Def::*, bool Def::*, std::string Def::*, Vector4 Def::*> member;
};

Vector4 ReadColor(const nlohmann::json& value, const Vector4& fallback)
{
    if (!value.is_array() || value.size() != kColorChannels) {
        return fallback;
    }
    return { value[0].get<float>(), value[1].get<float>(), value[2].get<float>(), value[3].get<float>() };
}

template <class Def, size_t N>
Def ReadRecipe(const nlohmann::json& json, const RecipeField<Def> (&fields)[N])
{
    Def def;
    for (const RecipeField<Def>& field : fields) {
        if (!json.contains(field.key)) {
            continue;
        }
        const nlohmann::json& value = json[field.key];
        std::visit([&](auto member) {
            using Value = std::remove_reference_t<decltype(def.*member)>;
            if constexpr (std::is_same_v<Value, Vector4>) {
                def.*member = ReadColor(value, def.*member);
            } else {
                def.*member = value.get<Value>();
            }
        },
            field.member);
    }
    return def;
}

template <class Def, size_t N>
void ReadSection(const nlohmann::json& root, const char* section, const RecipeField<Def> (&fields)[N],
    std::unordered_map<std::string, Def>& out)
{
    out.clear();
    const nlohmann::json entries = root.value(section, nlohmann::json::object());
    for (auto it = entries.begin(); it != entries.end(); ++it) {
        if (it.value().is_object()) {
            out[it.key()] = ReadRecipe(it.value(), fields);
        }
    }
}

constexpr RecipeField<ImpactDef> kImpactFields[] = {
    { "hitStopFrames", &ImpactDef::hitStopFrames },
    { "shakeAmount", &ImpactDef::shakeAmount },
    { "shakeSeconds", &ImpactDef::shakeSeconds },
};

constexpr RecipeField<RingBurstDef> kRingFields[] = {
    { "particle", &RingBurstDef::particle },
    { "speed", &RingBurstDef::speed },
    { "color", &RingBurstDef::color },
    { "count", &RingBurstDef::count },
    { "lifetime", &RingBurstDef::lifetime },
    { "size", &RingBurstDef::size },
    { "height", &RingBurstDef::height },
};

constexpr RecipeField<SparkBurstDef> kSparkFields[] = {
    { "particle", &SparkBurstDef::particle },
    { "speedXMin", &SparkBurstDef::speedXMin },
    { "speedXMax", &SparkBurstDef::speedXMax },
    { "riseMin", &SparkBurstDef::riseMin },
    { "riseMax", &SparkBurstDef::riseMax },
    { "count", &SparkBurstDef::count },
    { "color", &SparkBurstDef::color },
    { "altColor", &SparkBurstDef::altColor },
    { "alternateColor", &SparkBurstDef::alternateColor },
    { "lifetime", &SparkBurstDef::lifetime },
    { "size", &SparkBurstDef::size },
    { "height", &SparkBurstDef::height },
};

constexpr RecipeField<SlashDef> kSlashFields[] = {
    { "particle", &SlashDef::particle },
    { "angleRight", &SlashDef::angleRight },
    { "angleLeft", &SlashDef::angleLeft },
    { "color", &SlashDef::color },
    { "radius", &SlashDef::radius },
    { "height", &SlashDef::height },
};

constexpr RecipeField<FlashDef> kFlashFields[] = {
    { "color", &FlashDef::color },
    { "seconds", &FlashDef::seconds },
};

constexpr RecipeField<StarDef> kStarFields[] = {
    { "particle", &StarDef::particle },
    { "color", &StarDef::color },
    { "height", &StarDef::height },
};

constexpr RecipeField<ParticleDef> kParticleFields[] = {
    { "particle", &ParticleDef::particle },
    { "color", &ParticleDef::color },
    { "lifetime", &ParticleDef::lifetime },
    { "size", &ParticleDef::size },
    { "riseSpeed", &ParticleDef::riseSpeed },
    { "height", &ParticleDef::height },
    { "flicker", &ParticleDef::flicker },
    { "gravity", &ParticleDef::gravity },
};

constexpr RecipeField<TrailDef> kTrailFields[] = {
    { "particle", &TrailDef::particle },
    { "size", &TrailDef::size },
    { "lifetime", &TrailDef::lifetime },
    { "height", &TrailDef::height },
};

/** @brief レシピの色に呼び出し側の色指定を反映する */
Vector4 Styled(const Vector4& recipe, const EffectStyle& style)
{
    Vector4 color = style.color ? Vector4 { style.color->x, style.color->y, style.color->z, style.color->w * recipe.w } : recipe;
    if (style.tint) {
        color = { style.tint->x, style.tint->y, style.tint->z, color.w };
    }
    color.w *= style.alphaScale;
    return color;
}

Vector3 Raised(const Vector3& position, float height)
{
    return { position.x, position.y + height, position.z };
}
} // namespace

// ══════════════════════════════════════════════════════
// 読み込み
// ══════════════════════════════════════════════════════

HitEffectLibrary* HitEffectLibrary::GetInstance()
{
    static HitEffectLibrary instance;
    return &instance;
}

HitEffectLibrary::HitEffectLibrary()
{
    Reload();
}

void HitEffectLibrary::Reload()
{
    const nlohmann::json root = JsonHelper::Load(kHitEffectsPath);
    if (!root.is_object()) {
        Logger::LogWarning(std::string("HitEffectLibrary: ") + kHitEffectsPath + " が読めません。演出は出ません");
    }
    const nlohmann::json safeRoot = root.is_object() ? root : nlohmann::json::object();
    ReadSection(safeRoot, "impacts", kImpactFields, impacts_);
    ReadSection(safeRoot, "rings", kRingFields, rings_);
    ReadSection(safeRoot, "sparks", kSparkFields, sparks_);
    ReadSection(safeRoot, "slashes", kSlashFields, slashes_);
    ReadSection(safeRoot, "flashes", kFlashFields, flashes_);
    ReadSection(safeRoot, "stars", kStarFields, stars_);
    ReadSection(safeRoot, "particles", kParticleFields, particles_);
    ReadSection(safeRoot, "trails", kTrailFields, trails_);

    colors_.clear();
    const nlohmann::json colors = safeRoot.value("colors", nlohmann::json::object());
    for (auto it = colors.begin(); it != colors.end(); ++it) {
        colors_[it.key()] = ReadColor(it.value(), { 1.0f, 1.0f, 1.0f, 1.0f });
    }
}

template <class Def>
const Def& HitEffectLibrary::Find(const std::unordered_map<std::string, Def>& table, const std::string& name,
    const char* section) const
{
    auto it = table.find(name);
    if (it != table.end()) {
        return it->second;
    }
    static const Def kEmpty {};
    static std::unordered_set<std::string> warned;
    const std::string key = std::string(section) + "." + name;
    if (warned.insert(key).second) {
        Logger::LogWarning("HitEffectLibrary: " + key + " が hit_effects.json にありません");
    }
    return kEmpty;
}

const ImpactDef& HitEffectLibrary::Impact(const std::string& name) const { return Find(impacts_, name, "impacts"); }
const RingBurstDef& HitEffectLibrary::Ring(const std::string& name) const { return Find(rings_, name, "rings"); }
const SparkBurstDef& HitEffectLibrary::Sparks(const std::string& name) const { return Find(sparks_, name, "sparks"); }
const SlashDef& HitEffectLibrary::Slash(const std::string& name) const { return Find(slashes_, name, "slashes"); }
const FlashDef& HitEffectLibrary::Flash(const std::string& name) const { return Find(flashes_, name, "flashes"); }
const StarDef& HitEffectLibrary::Star(const std::string& name) const { return Find(stars_, name, "stars"); }
const ParticleDef& HitEffectLibrary::Particle(const std::string& name) const { return Find(particles_, name, "particles"); }
const TrailDef& HitEffectLibrary::Trail(const std::string& name) const { return Find(trails_, name, "trails"); }
const Vector4& HitEffectLibrary::Color(const std::string& name) const { return Find(colors_, name, "colors"); }

// ══════════════════════════════════════════════════════
// 演出を出す
// ══════════════════════════════════════════════════════

HitEffectPlayer::HitEffectPlayer(ParticleManager* particles, CameraShaker* shaker, std::mt19937* rng)
    : particles_(particles)
    , shaker_(shaker)
    , rng_(rng)
{
}

void HitEffectPlayer::Impact(const std::string& name, const EffectStyle& style) const
{
    const ImpactDef& def = HitEffectLibrary::GetInstance()->Impact(name);
    if (def.hitStopFrames > 0) {
        TimeManager::GetInstance()->RequestHitStop(def.hitStopFrames);
    }
    if (shaker_ && def.shakeAmount > 0.0f) {
        shaker_->Request(def.shakeAmount * style.scale, def.shakeSeconds);
    }
}

void HitEffectPlayer::Ring(const std::string& name, const Vector3& position, const EffectStyle& style) const
{
    const RingBurstDef& def = HitEffectLibrary::GetInstance()->Ring(name);
    const int count = def.count + style.extraCount;
    if (!particles_ || count <= 0) {
        return;
    }
    const float speed = (style.speed ? *style.speed * def.speed : def.speed) * style.scale;
    particles_->EmitRing(def.particle, Raised(position, def.height), speed, Styled(def.color, style),
        static_cast<uint32_t>(count), def.lifetime, def.size * style.scale);
}

void HitEffectPlayer::Sparks(const std::string& name, const Vector3& position, const EffectStyle& style) const
{
    const SparkBurstDef& def = HitEffectLibrary::GetInstance()->Sparks(name);
    const int count = def.count + style.extraCount;
    if (!particles_ || !rng_ || count <= 0) {
        return;
    }
    std::uniform_real_distribution<float> speedX(def.speedXMin, def.speedXMax);
    std::uniform_real_distribution<float> rise(def.riseMin, def.riseMax);
    const Vector3 origin = Raised(position, def.height);
    const Vector4 color = Styled(def.color, style);
    const Vector4 altColor = Styled(def.altColor, style);
    for (int i = 0; i < count; ++i) {
        // 1粒ごとに左右を入れ替え、両側へ均等に散らす
        const bool even = (i % 2) == 0;
        const float side = even ? -1.0f : 1.0f;
        particles_->EmitGravity(def.particle, origin,
            { side * speedX(*rng_) * style.scale, rise(*rng_) * style.scale, 0.0f },
            (def.alternateColor && !even) ? altColor : color, def.lifetime, def.size * style.scale);
    }
}

void HitEffectPlayer::Slash(const std::string& name, const Vector3& position, float facingX, const EffectStyle& style) const
{
    const SlashDef& def = HitEffectLibrary::GetInstance()->Slash(name);
    if (!particles_ || def.particle.empty()) {
        return;
    }
    const float angle = style.angle ? *style.angle : (facingX >= 0.0f ? def.angleRight : def.angleLeft);
    const float radius = (style.radius ? *style.radius : def.radius) * style.scale;
    particles_->EmitSlash(def.particle, Raised(position, def.height), angle, Styled(def.color, style), radius);
}

void HitEffectPlayer::Flash(const std::string& name, const EffectStyle& style) const
{
    const FlashDef& def = HitEffectLibrary::GetInstance()->Flash(name);
    if (def.seconds > 0.0f) {
        ScreenFlash::GetInstance()->Request(Styled(def.color, style), def.seconds);
    }
}

void HitEffectPlayer::Star(const std::string& name, const Vector3& position, const EffectStyle& style) const
{
    const StarDef& def = HitEffectLibrary::GetInstance()->Star(name);
    if (particles_ && !def.particle.empty()) {
        particles_->EmitHitStar(def.particle, Raised(position, def.height), Styled(def.color, style));
    }
}

void HitEffectPlayer::Particle(const std::string& name, const Vector3& position, const EffectStyle& style) const
{
    const ParticleDef& def = HitEffectLibrary::GetInstance()->Particle(name);
    if (particles_ && !def.particle.empty()) {
        const Vector3 velocity = style.velocity ? *style.velocity : Vector3 { 0.0f, def.riseSpeed, 0.0f };
        const Vector3 origin = Raised(position, def.height);
        if (def.gravity) {
            particles_->EmitGravity(def.particle, origin, velocity, Styled(def.color, style), def.lifetime, def.size * style.scale);
        } else {
            particles_->EmitWithColor(def.particle, origin, velocity,
                Styled(def.color, style), def.lifetime, def.size * style.scale, def.flicker);
        }
    }
}

void HitEffectPlayer::Trail(const std::string& name, const Vector3& position, const Vector4& color, const EffectStyle& style) const
{
    const TrailDef& def = HitEffectLibrary::GetInstance()->Trail(name);
    if (particles_ && !def.particle.empty()) {
        particles_->EmitTrail(def.particle, Raised(position, def.height), Styled(color, style),
            def.size * style.scale, def.lifetime);
    }
}
