/**
 * @file HitEffectLibrary.cpp
 * @brief 演出レシピの読み込みと、レシピどおりに演出を出す処理を実装するファイル
 */
#include "HitEffectLibrary.h"
#include "CameraShaker.h"
#include "JsonHelper.h"
#include "JsonFieldTable.h"
#include "Logger.h"
#include "ParticleManager.h"
#include "ScreenFlash.h"
#include "TimeManager.h"
#include <algorithm>
#include <limits>
#include <unordered_set>
using engine::graphics::ParticleManager;

namespace engine::game {
namespace {

/** @brief hit_effects.json の節を読み、名前ごとのレシピを表へ登録する */
template <class Def, size_t N>
void ReadSection(const nlohmann::json& root, const char* section, const JsonField<Def> (&fields)[N],
    std::unordered_map<std::string, Def>& out) {
    out.clear();
    const nlohmann::json entries = root.value(section, nlohmann::json::object());
    for (auto it = entries.begin(); it != entries.end(); ++it) {
        if (it.value().is_object()) {
            out[it.key()] = ReadJsonFields(it.value(), fields);
        }
    }
}

constexpr JsonField<FinisherVisualDef> kFinisherVisualFields[] = {
    { nullptr, "slashLineLengthMin", &FinisherVisualDef::slashLineLengthMin },
    { nullptr, "slashLineLengthMax", &FinisherVisualDef::slashLineLengthMax },
    { nullptr, "slashLineThicknessMin", &FinisherVisualDef::slashLineThicknessMin },
    { nullptr, "slashLineThicknessMax", &FinisherVisualDef::slashLineThicknessMax },
    { nullptr, "slashLineLingerSeconds", &FinisherVisualDef::slashLineLingerSeconds },
    { nullptr, "slashLineColor", &FinisherVisualDef::slashLineColor },
    { nullptr, "beatBladeCount", &FinisherVisualDef::beatBladeCount },
    { nullptr, "beatBladeRadius", &FinisherVisualDef::beatBladeRadius },
    { nullptr, "beatBladeSpeedMin", &FinisherVisualDef::beatBladeSpeedMin },
    { nullptr, "beatBladeSpeedMax", &FinisherVisualDef::beatBladeSpeedMax },
    { nullptr, "slashFlashColor", &FinisherVisualDef::slashFlashColor },
    { nullptr, "slashFlashSeconds", &FinisherVisualDef::slashFlashSeconds },
    { nullptr, "releaseSlashCount", &FinisherVisualDef::releaseSlashCount },
    { nullptr, "releaseSlashThickness", &FinisherVisualDef::releaseSlashThickness },
    { nullptr, "releaseSlashSeconds", &FinisherVisualDef::releaseSlashSeconds },
    { nullptr, "releaseBladeCount", &FinisherVisualDef::releaseBladeCount },
    { nullptr, "releaseBladeSpeedMin", &FinisherVisualDef::releaseBladeSpeedMin },
    { nullptr, "releaseBladeSpeedMax", &FinisherVisualDef::releaseBladeSpeedMax },
    { nullptr, "releaseWarpImpulse", &FinisherVisualDef::releaseWarpImpulse },
};

constexpr JsonField<SceneParticleDef> kSceneParticleFields[] = {
    { nullptr, "hitStopLaunch", &SceneParticleDef::hitStopLaunch },
    { nullptr, "hitStopAwakenExtra", &SceneParticleDef::hitStopAwakenExtra },
    { nullptr, "hitStopJuggle", &SceneParticleDef::hitStopJuggle },
    { nullptr, "hitStopFinish", &SceneParticleDef::hitStopFinish },
    { nullptr, "hitStopFinisherSlash", &SceneParticleDef::hitStopFinisherSlash },
    { nullptr, "hitStopFinisherBeat", &SceneParticleDef::hitStopFinisherBeat },
    { nullptr, "shakeLaunchAmt", &SceneParticleDef::shakeLaunchAmt },
    { nullptr, "shakeLaunchDur", &SceneParticleDef::shakeLaunchDur },
    { nullptr, "shakeFinishAmt", &SceneParticleDef::shakeFinishAmt },
    { nullptr, "shakeFinishDur", &SceneParticleDef::shakeFinishDur },
    { nullptr, "shakeFinisherSlashAmt", &SceneParticleDef::shakeFinisherSlashAmt },
    { nullptr, "shakeFinisherSlashDur", &SceneParticleDef::shakeFinisherSlashDur },
    { nullptr, "finisherSlashRadius", &SceneParticleDef::finisherSlashRadius },
    { nullptr, "finisherOverlayAlpha", &SceneParticleDef::finisherOverlayAlpha },
    { nullptr, "ghostSpawnInterval", &SceneParticleDef::ghostSpawnInterval },
    { nullptr, "ghostLifetime", &SceneParticleDef::ghostLifetime },
    { nullptr, "ghostMaxAlpha", &SceneParticleDef::ghostMaxAlpha },
    { nullptr, "ghostColor", &SceneParticleDef::ghostColor },
    { nullptr, "swingCenterHeight", &SceneParticleDef::swingCenterHeight },
    { nullptr, "beatShakeAmount", &SceneParticleDef::beatShakeAmount },
    { nullptr, "beatShakeSeconds", &SceneParticleDef::beatShakeSeconds },
    { nullptr, "beatWarpImpulse", &SceneParticleDef::beatWarpImpulse },
    { nullptr, "releaseFlashColor", &SceneParticleDef::releaseFlashColor },
    { nullptr, "releaseFlashSeconds", &SceneParticleDef::releaseFlashSeconds },
    { nullptr, "absorbTargetHeight", &SceneParticleDef::absorbTargetHeight },
    { nullptr, "absorbGlowColor", &SceneParticleDef::absorbGlowColor },
    { nullptr, "absorbOrbCount", &SceneParticleDef::absorbOrbCount },
    { nullptr, "absorbOrbSpeedBase", &SceneParticleDef::absorbOrbSpeedBase },
    { nullptr, "absorbOrbSpeedStep", &SceneParticleDef::absorbOrbSpeedStep },
    { nullptr, "absorbOrbLifetime", &SceneParticleDef::absorbOrbLifetime },
    { nullptr, "absorbOrbSize", &SceneParticleDef::absorbOrbSize },
    { nullptr, "absorbPullRate", &SceneParticleDef::absorbPullRate },
    { nullptr, "surfaceLerp", &SceneParticleDef::surfaceLerp },
    { nullptr, "extraRingCountDivisor", &SceneParticleDef::extraRingCountDivisor },
    { nullptr, "chestHeight", &SceneParticleDef::chestHeight },
    { nullptr, "comboSlashGrowthPerStep", &SceneParticleDef::comboSlashGrowthPerStep },
    { nullptr, "comboFinisherStep", &SceneParticleDef::comboFinisherStep },
    { nullptr, "burstColumns", &SceneParticleDef::burstColumns },
    { nullptr, "burstCenterColumn", &SceneParticleDef::burstCenterColumn },
    { nullptr, "burstSpreadZ", &SceneParticleDef::burstSpreadZ },
    { nullptr, "burstSpeedBase", &SceneParticleDef::burstSpeedBase },
    { nullptr, "burstSpeedStep", &SceneParticleDef::burstSpeedStep },
    { nullptr, "burstRiseBase", &SceneParticleDef::burstRiseBase },
    { nullptr, "burstRiseSteps", &SceneParticleDef::burstRiseSteps },
    { nullptr, "burstRiseStep", &SceneParticleDef::burstRiseStep },
    { nullptr, "minBullets", &SceneParticleDef::minBullets },
    { nullptr, "fallbackBullets", &SceneParticleDef::fallbackBullets },
    { nullptr, "fallbackSpread", &SceneParticleDef::fallbackSpread },
    { nullptr, "fanSpeedBase", &SceneParticleDef::fanSpeedBase },
    { nullptr, "fanSpeedStep", &SceneParticleDef::fanSpeedStep },
    { nullptr, "halfRange", &SceneParticleDef::halfRange },
    { nullptr, "sequenceSlashAngleStep", &SceneParticleDef::sequenceSlashAngleStep },
    { nullptr, "bladeCount", &SceneParticleDef::bladeCount },
    { nullptr, "bladeRadius", &SceneParticleDef::bladeRadius },
    { nullptr, "bladeSpeedMin", &SceneParticleDef::bladeSpeedMin },
    { nullptr, "bladeSpeedMax", &SceneParticleDef::bladeSpeedMax },
    { nullptr, "slashLineHalfLength", &SceneParticleDef::slashLineHalfLength },
    { nullptr, "slashLineColor", &SceneParticleDef::slashLineColor },
    { nullptr, "slashLineThickness", &SceneParticleDef::slashLineThickness },
    { nullptr, "slashLineSeconds", &SceneParticleDef::slashLineSeconds },
    { nullptr, "auraInterval", &SceneParticleDef::auraInterval },
    { nullptr, "auraJitter", &SceneParticleDef::auraJitter },
    { nullptr, "warningMinScale", &SceneParticleDef::warningMinScale },
    { nullptr, "warningBlinkSpeed", &SceneParticleDef::warningBlinkSpeed },
    { nullptr, "warningGreenBase", &SceneParticleDef::warningGreenBase },
    { nullptr, "warningGreenGrowth", &SceneParticleDef::warningGreenGrowth },
    { nullptr, "warningBlue", &SceneParticleDef::warningBlue },
    { nullptr, "warningAlphaBase", &SceneParticleDef::warningAlphaBase },
    { nullptr, "warningAlphaBlink", &SceneParticleDef::warningAlphaBlink },
    { nullptr, "warningRingGreenBase", &SceneParticleDef::warningRingGreenBase },
    { nullptr, "warningRingGreenFade", &SceneParticleDef::warningRingGreenFade },
    { nullptr, "warningRingBlueBase", &SceneParticleDef::warningRingBlueBase },
    { nullptr, "warningRingBlueFade", &SceneParticleDef::warningRingBlueFade },
};

constexpr JsonField<ImpactDef> kImpactFields[] = {
    { nullptr, "hitStopFrames", &ImpactDef::hitStopFrames },
    { nullptr, "shakeAmount", &ImpactDef::shakeAmount },
    { nullptr, "shakeSeconds", &ImpactDef::shakeSeconds },
};

constexpr JsonField<RingBurstDef> kRingFields[] = {
    { nullptr, "particle", &RingBurstDef::particle },
    { nullptr, "speed", &RingBurstDef::speed },
    { nullptr, "color", &RingBurstDef::color },
    { nullptr, "count", &RingBurstDef::count },
    { nullptr, "lifetime", &RingBurstDef::lifetime },
    { nullptr, "size", &RingBurstDef::size },
    { nullptr, "height", &RingBurstDef::height },
};

constexpr JsonField<SparkBurstDef> kSparkFields[] = {
    { nullptr, "particle", &SparkBurstDef::particle },
    { nullptr, "speedXMin", &SparkBurstDef::speedXMin },
    { nullptr, "speedXMax", &SparkBurstDef::speedXMax },
    { nullptr, "riseMin", &SparkBurstDef::riseMin },
    { nullptr, "riseMax", &SparkBurstDef::riseMax },
    { nullptr, "count", &SparkBurstDef::count },
    { nullptr, "color", &SparkBurstDef::color },
    { nullptr, "altColor", &SparkBurstDef::altColor },
    { nullptr, "alternateColor", &SparkBurstDef::alternateColor },
    { nullptr, "lifetime", &SparkBurstDef::lifetime },
    { nullptr, "size", &SparkBurstDef::size },
    { nullptr, "height", &SparkBurstDef::height },
};

constexpr JsonField<SlashDef> kSlashFields[] = {
    { nullptr, "particle", &SlashDef::particle },
    { nullptr, "angleRight", &SlashDef::angleRight },
    { nullptr, "angleLeft", &SlashDef::angleLeft },
    { nullptr, "color", &SlashDef::color },
    { nullptr, "radius", &SlashDef::radius },
    { nullptr, "height", &SlashDef::height },
};

constexpr JsonField<FlashDef> kFlashFields[] = {
    { nullptr, "color", &FlashDef::color },
    { nullptr, "seconds", &FlashDef::seconds },
};

constexpr JsonField<StarDef> kStarFields[] = {
    { nullptr, "particle", &StarDef::particle },
    { nullptr, "color", &StarDef::color },
    { nullptr, "height", &StarDef::height },
};

constexpr JsonField<ParticleDef> kParticleFields[] = {
    { nullptr, "particle", &ParticleDef::particle },
    { nullptr, "color", &ParticleDef::color },
    { nullptr, "lifetime", &ParticleDef::lifetime },
    { nullptr, "size", &ParticleDef::size },
    { nullptr, "riseSpeed", &ParticleDef::riseSpeed },
    { nullptr, "height", &ParticleDef::height },
    { nullptr, "flicker", &ParticleDef::flicker },
    { nullptr, "gravity", &ParticleDef::gravity },
};

constexpr JsonField<TrailDef> kTrailFields[] = {
    { nullptr, "particle", &TrailDef::particle },
    { nullptr, "size", &TrailDef::size },
    { nullptr, "lifetime", &TrailDef::lifetime },
    { nullptr, "height", &TrailDef::height },
};

/** @brief レシピの色に呼び出し側の色指定を反映する */
Vector4 Styled(const Vector4& recipe, const EffectStyle& style) {
    Vector4 color = style.color ? Vector4 { style.color->x, style.color->y, style.color->z, style.color->w * recipe.w } : recipe;
    if (style.tint) {
        color = { style.tint->x, style.tint->y, style.tint->z, color.w };
    }
    color.w *= style.alphaScale;
    return color;
}

/** @brief 位置を指定の高さだけ持ち上げた位置を返す */
Vector3 Raised(const Vector3& position, float height) {
    return { position.x, position.y + height, position.z };
}
} // namespace

// ══════════════════════════════════════════════════════
// 読み込み
// ══════════════════════════════════════════════════════

/** @brief 演出定義の共有インスタンスを取得する */
HitEffectLibrary* HitEffectLibrary::GetInstance() {
    static HitEffectLibrary sInstance;
    return &sInstance;
}

/** @brief hit_effects.json を読み込んで演出の表を作る */
HitEffectLibrary::HitEffectLibrary() {
    Reload();
}

/** @brief JSONを読み直す */
void HitEffectLibrary::Reload() {
    // 設定が読めなくても空の設定として最後まで読み、既定値の演出で動かす
    const nlohmann::json root = JsonHelper::Load(HitEffectLibrary::kConfigPath);
    if (!root.is_object()) {
        Logger::LogWarning(std::string("HitEffectLibrary: ") + HitEffectLibrary::kConfigPath + " が読めません。演出は出ません");
    }
    const nlohmann::json safeRoot = root.is_object() ? root : nlohmann::json::object();
    // 締め演出の値を読み、最小と最大が逆に書かれていたら最大を最小に合わせる
    finisherVisuals_ = ReadJsonFields(safeRoot.value("finisherVisuals", nlohmann::json::object()), kFinisherVisualFields);
    finisherVisuals_.slashLineLengthMax = (std::max)(finisherVisuals_.slashLineLengthMin, finisherVisuals_.slashLineLengthMax);
    finisherVisuals_.slashLineThicknessMax = (std::max)(finisherVisuals_.slashLineThicknessMin, finisherVisuals_.slashLineThicknessMax);
    finisherVisuals_.beatBladeSpeedMax = (std::max)(finisherVisuals_.beatBladeSpeedMin, finisherVisuals_.beatBladeSpeedMax);
    finisherVisuals_.releaseBladeSpeedMax = (std::max)(finisherVisuals_.releaseBladeSpeedMin, finisherVisuals_.releaseBladeSpeedMax);
    sceneParticles_ = ReadJsonFields(safeRoot.value("sceneParticles", nlohmann::json::object()), kSceneParticleFields);
    // 除算・剰余と乱数分布に渡す値は有効な範囲に保つ。
    sceneParticles_.extraRingCountDivisor = (std::max)(sceneParticles_.extraRingCountDivisor, 1);
    sceneParticles_.ghostLifetime = (std::max)(sceneParticles_.ghostLifetime, std::numeric_limits<float>::epsilon());
    sceneParticles_.burstColumns = (std::max)(sceneParticles_.burstColumns, 1);
    sceneParticles_.burstRiseSteps = (std::max)(sceneParticles_.burstRiseSteps, 1);
    sceneParticles_.bladeCount = (std::max)(sceneParticles_.bladeCount, 0);
    sceneParticles_.bladeSpeedMax = (std::max)(sceneParticles_.bladeSpeedMax, sceneParticles_.bladeSpeedMin);
    sceneParticles_.auraJitter = (std::max)(sceneParticles_.auraJitter, 0.0f);
    // 名前で呼び出す演出レシピを種類ごとに読む
    ReadSection(safeRoot, "impacts", kImpactFields, impacts_);
    ReadSection(safeRoot, "rings", kRingFields, rings_);
    ReadSection(safeRoot, "sparks", kSparkFields, sparks_);
    ReadSection(safeRoot, "slashes", kSlashFields, slashes_);
    ReadSection(safeRoot, "flashes", kFlashFields, flashes_);
    ReadSection(safeRoot, "stars", kStarFields, stars_);
    ReadSection(safeRoot, "particles", kParticleFields, particles_);
    ReadSection(safeRoot, "trails", kTrailFields, trails_);

    // 名前付きの色（読み直しで古い色が残らないよう先に空にする）
    colors_.clear();
    const nlohmann::json colors = safeRoot.value("colors", nlohmann::json::object());
    for (auto it = colors.begin(); it != colors.end(); ++it) {
        colors_[it.key()] = JsonHelper::ReadColor(it.value(), { 1.0f, 1.0f, 1.0f, 1.0f });
    }
}

/** @brief 表から名前のレシピを探す（無ければ警告を出して空のレシピを返す） */
template <class Def>
const Def& HitEffectLibrary::Find(const std::unordered_map<std::string, Def>& table, const std::string& name,
    const char* section) const {
    auto it = table.find(name);
    if (it != table.end()) {
        return it->second;
    }
    static const Def kEmpty {};
    static std::unordered_set<std::string> sWarned;
    const std::string key = std::string(section) + "." + name;
    if (sWarned.insert(key).second) {
        Logger::LogWarning("HitEffectLibrary: " + key + " が hit_effects.json にありません");
    }
    return kEmpty;
}

namespace {
/** @brief 表に名前のレシピがあるか */
template <class Def>
bool Contains(const std::unordered_map<std::string, Def>& table, const std::string& name) {
    return table.find(name) != table.end();
}
} // namespace

/** @brief その節に名前のレシピがあるか */
bool HitEffectLibrary::Has(const std::string& section, const std::string& name) const {
    /** @brief 節の名前と、その節にレシピがあるかの対応 */
    struct SectionHas {
        const char* section;
        bool found;
    };
    const SectionHas kSections[] = {
        { "impacts", Contains(impacts_, name) },
        { "rings", Contains(rings_, name) },
        { "sparks", Contains(sparks_, name) },
        { "slashes", Contains(slashes_, name) },
        { "flashes", Contains(flashes_, name) },
        { "stars", Contains(stars_, name) },
    };
    for (const SectionHas& entry : kSections) {
        if (section == entry.section) {
            return entry.found;
        }
    }
    return false;
}

/** @brief 名前の衝撃（ヒットストップ・カメラの揺れ）のレシピを返す */
const ImpactDef& HitEffectLibrary::Impact(const std::string& name) const {
    return Find(impacts_, name, "impacts");
}
/** @brief 名前のリングのレシピを返す */
const RingBurstDef& HitEffectLibrary::Ring(const std::string& name) const {
    return Find(rings_, name, "rings");
}
/** @brief 名前の火花のレシピを返す */
const SparkBurstDef& HitEffectLibrary::Sparks(const std::string& name) const {
    return Find(sparks_, name, "sparks");
}
/** @brief 名前の斬線のレシピを返す */
const SlashDef& HitEffectLibrary::Slash(const std::string& name) const {
    return Find(slashes_, name, "slashes");
}
/** @brief 名前の画面フラッシュのレシピを返す */
const FlashDef& HitEffectLibrary::Flash(const std::string& name) const {
    return Find(flashes_, name, "flashes");
}
/** @brief 名前のきらめきのレシピを返す */
const StarDef& HitEffectLibrary::Star(const std::string& name) const {
    return Find(stars_, name, "stars");
}
/** @brief 名前のパーティクルのレシピを返す */
const ParticleDef& HitEffectLibrary::Particle(const std::string& name) const {
    return Find(particles_, name, "particles");
}
/** @brief 名前の軌跡のレシピを返す */
const TrailDef& HitEffectLibrary::Trail(const std::string& name) const {
    return Find(trails_, name, "trails");
}
/** @brief 名前の色を返す */
const Vector4& HitEffectLibrary::Color(const std::string& name) const {
    return Find(colors_, name, "colors");
}

// ══════════════════════════════════════════════════════
// 演出を出す
// ══════════════════════════════════════════════════════

/** @brief 演出の出力先と乱数生成器を借用する。各ポインターの所有権は呼び出し側に残す */
HitEffectPlayer::HitEffectPlayer(ParticleManager* particles, CameraShaker* shaker, std::mt19937* rng)
    : particles_(particles)
    , shaker_(shaker)
    , rng_(rng) {
}

/** @brief ヒットストップとカメラ揺れ（style.scale は揺れの強さに掛ける） */
void HitEffectPlayer::Impact(const std::string& name, const EffectStyle& style) const {
    const ImpactDef& def = HitEffectLibrary::GetInstance()->Impact(name);
    if (def.hitStopFrames > 0) {
        TimeManager::GetInstance()->RequestHitStop(def.hitStopFrames);
    }
    if (shaker_ && def.shakeAmount > 0.0f) {
        shaker_->Request(def.shakeAmount * style.scale, def.shakeSeconds);
    }
}

/** @brief 名前のレシピでリングを出す */
void HitEffectPlayer::Ring(const std::string& name, const Vector3& position, const EffectStyle& style) const {
    const RingBurstDef& def = HitEffectLibrary::GetInstance()->Ring(name);
    const int count = def.count + style.extraCount;
    if (!particles_ || count <= 0) {
        return;
    }
    const float speed = (style.speed ? *style.speed * def.speed : def.speed) * style.scale;
    particles_->EmitRing(def.particle, Raised(position, def.height), speed, Styled(def.color, style),
        static_cast<uint32_t>(count), def.lifetime, def.size * style.scale);
}

/** @brief 名前のレシピで火花を出す */
void HitEffectPlayer::Sparks(const std::string& name, const Vector3& position, const EffectStyle& style) const {
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

/** @brief 斬線を出す（facingX が正なら右向きの角度） */
void HitEffectPlayer::Slash(const std::string& name, const Vector3& position, float facingX, const EffectStyle& style) const {
    const SlashDef& def = HitEffectLibrary::GetInstance()->Slash(name);
    if (!particles_ || def.particle.empty()) {
        return;
    }
    const float angle = style.angle ? *style.angle : (facingX >= 0.0f ? def.angleRight : def.angleLeft);
    const float radius = (style.radius ? *style.radius : def.radius) * style.scale;
    particles_->EmitSlash(def.particle, Raised(position, def.height), angle, Styled(def.color, style), radius);
}

/** @brief 名前のレシピで画面フラッシュを出す */
void HitEffectPlayer::Flash(const std::string& name, const EffectStyle& style) const {
    const FlashDef& def = HitEffectLibrary::GetInstance()->Flash(name);
    if (def.seconds > 0.0f) {
        ScreenFlash::GetInstance()->Request(Styled(def.color, style), def.seconds);
    }
}

/** @brief 名前のレシピできらめきを出す */
void HitEffectPlayer::Star(const std::string& name, const Vector3& position, const EffectStyle& style) const {
    const StarDef& def = HitEffectLibrary::GetInstance()->Star(name);
    if (particles_ && !def.particle.empty()) {
        particles_->EmitHitStar(def.particle, Raised(position, def.height), Styled(def.color, style));
    }
}

/** @brief 名前のレシピでパーティクルを出す */
void HitEffectPlayer::Particle(const std::string& name, const Vector3& position, const EffectStyle& style) const {
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

/** @brief 武器の残像を1粒出す（毎フレーム呼んで軌跡にする） */
void HitEffectPlayer::Trail(const std::string& name, const Vector3& position, const Vector4& color, const EffectStyle& style) const {
    const TrailDef& def = HitEffectLibrary::GetInstance()->Trail(name);
    if (particles_ && !def.particle.empty()) {
        particles_->EmitTrail(def.particle, Raised(position, def.height), Styled(color, style),
            def.size * style.scale, def.lifetime);
    }
}

} // namespace engine::game
