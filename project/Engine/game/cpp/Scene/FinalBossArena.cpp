/** @file FinalBossArena.cpp
 * @brief 石床と発光する縁、外周の柱と粒を描く。
 */
#include "FinalBossArena.h"
#include "JsonFieldTable.h"
#include "ModelManager.h"
#include "ParticleManager.h"
#include <algorithm>
#include <cmath>
#include <numbers>
namespace engine::game {
namespace {
constexpr const char* kModel = "Resources/block/block.obj";
constexpr const char* kTexture = "Resources/block/block.png";
constexpr const char* kWhite = "Resources/white.png";
constexpr const char* kParticleGroup = "final_boss_arena"; ///< 漂う光の粒のパーティクルグループ名
constexpr float kTau = std::numbers::pi_v<float> + std::numbers::pi_v<float>;
constexpr float kMinimumSize = 0.01f;
constexpr int kMaxPieces = 256;
constexpr int kMaxEmissions = 8;
constexpr float kOrbitSpeed = 0.37f;
constexpr float kRadialFraction = 0.8f;
constexpr float kDefaultRadius = 12.0f;
constexpr float kDefaultFloorY = -0.1f;
constexpr float kDefaultThickness = 0.6f;
constexpr int kDefaultSlices = 48;
constexpr int kDefaultRimCount = 96;
constexpr float kDefaultRimHeight = 0.12f;
constexpr float kDefaultRimWidth = 0.16f;
constexpr int kDefaultPillarCount = 8;
constexpr float kDefaultPillarHeight = 7.0f;
constexpr float kDefaultPillarDistance = 15.0f;
constexpr float kDefaultPillarWidth = 0.85f;
constexpr float kDefaultPulseSpeed = 2.0f;
constexpr float kDefaultPulseDepth = 0.18f;
constexpr float kDefaultParticleInterval = 0.16f;
constexpr float kDefaultParticleHeight = 3.0f;
constexpr float kDefaultParticleSpeed = 0.18f;
constexpr float kDefaultParticleLife = 4.0f;
constexpr float kDefaultParticleSize = 0.07f;
constexpr Vector4 kStoneColor = { 0.12f, 0.13f, 0.18f, 1.0f };
constexpr Vector4 kCalmColor = { 0.65f, 0.75f, 1.0f, 1.0f };
constexpr Vector4 kRageColor = { 1.0f, 0.12f, 0.2f, 1.0f };
/** @brief 闘技場の見た目の調整値。 */
struct Tuning {
    float radius = kDefaultRadius;
    float floorY = kDefaultFloorY;
    float floorThickness = kDefaultThickness;
    int floorSlices = kDefaultSlices;
    int rimCount = kDefaultRimCount;
    float rimHeight = kDefaultRimHeight;
    float rimWidth = kDefaultRimWidth;
    int pillarCount = kDefaultPillarCount;
    float pillarHeight = kDefaultPillarHeight;
    float pillarDistance = kDefaultPillarDistance;
    float pillarWidth = kDefaultPillarWidth;
    float pulseSpeed = kDefaultPulseSpeed;
    float pulseDepth = kDefaultPulseDepth;
    float particleInterval = kDefaultParticleInterval;
    float particleHeight = kDefaultParticleHeight;
    float particleSpeed = kDefaultParticleSpeed;
    float particleLife = kDefaultParticleLife;
    float particleSize = kDefaultParticleSize;
    Vector4 stoneColor = kStoneColor;
    Vector4 calmColor = kCalmColor;
    Vector4 rageColor = kRageColor;
};
constexpr JsonField<Tuning> kFields[] = {
    { nullptr, "radius", &Tuning::radius },
    { nullptr, "floorY", &Tuning::floorY },
    { nullptr, "floorThickness", &Tuning::floorThickness },
    { nullptr, "floorSlices", &Tuning::floorSlices },
    { nullptr, "rimCount", &Tuning::rimCount },
    { nullptr, "rimHeight", &Tuning::rimHeight },
    { nullptr, "rimWidth", &Tuning::rimWidth },
    { nullptr, "pillarCount", &Tuning::pillarCount },
    { nullptr, "pillarHeight", &Tuning::pillarHeight },
    { nullptr, "pillarDistance", &Tuning::pillarDistance },
    { nullptr, "pillarWidth", &Tuning::pillarWidth },
    { nullptr, "pulseSpeed", &Tuning::pulseSpeed },
    { nullptr, "pulseDepth", &Tuning::pulseDepth },
    { nullptr, "particleInterval", &Tuning::particleInterval },
    { nullptr, "particleHeight", &Tuning::particleHeight },
    { nullptr, "particleSpeed", &Tuning::particleSpeed },
    { nullptr, "particleLife", &Tuning::particleLife },
    { nullptr, "particleSize", &Tuning::particleSize },
    { nullptr, "stoneColor", &Tuning::stoneColor },
    { nullptr, "calmColor", &Tuning::calmColor },
    { nullptr, "rageColor", &Tuning::rageColor },
};
} // namespace

/** @brief 設定と共有モデルから全装飾を生成する。 */
void FinalBossArena::Initialize(const Setup& setup) {
    if (!pieces_.empty()) {
        return;
    }
    const Tuning tune = ReadJsonFields(JsonHelper::Load("Resources/Config/final_boss_stage.json"), kFields);
    radius_ = (std::max)(tune.radius, kMinimumSize);
    floorY_ = tune.floorY;
    pulseSpeed_ = tune.pulseSpeed;
    pulseDepth_ = std::clamp(tune.pulseDepth, 0.0f, 1.0f);
    particleInterval_ = (std::max)(tune.particleInterval, kMinimumSize);
    particleHeight_ = (std::max)(tune.particleHeight, 0.0f);
    particleSpeed_ = tune.particleSpeed;
    particleLife_ = (std::max)(tune.particleLife, kMinimumSize);
    particleSize_ = (std::max)(tune.particleSize, kMinimumSize);
    calmColor_ = tune.calmColor;
    rageColor_ = tune.rageColor;
    if (!setup.modelCommon) {
        return;
    }
    auto* model = engine::graphics::ModelManager::GetInstance()->GetOrLoad(setup.modelCommon, kModel, kTexture);
    /** @brief 単位立方体から装飾を追加する。 */
    const auto add = [&](const Vector3& position, const Vector3& scale, float yaw, bool glow) {
        Piece piece;
        piece.object = std::make_unique<engine::graphics::Object3d>();
        piece.object->Initialize(setup.modelCommon);
        piece.object->SetModel(model);
        piece.object->SetScale(scale);
        piece.object->SetColor(tune.stoneColor);
        piece.object->SetEnableLighting(!glow);
        piece.object->SetUseTexture(!glow);
        piece.position = position;
        piece.yaw = yaw;
        piece.glow = glow;
        pieces_.push_back(std::move(piece));
    };
    // 円の弦幅を持つ帯を敷き、同一平面の重なりを避ける。
    const int slices = std::clamp(tune.floorSlices, 1, kMaxPieces);
    const float diameter = radius_ + radius_;
    const float sliceDepth = diameter / static_cast<float>(slices);
    const float thickness = (std::max)(tune.floorThickness, kMinimumSize);
    for (int i = 0; i < slices; ++i) {
        const float z = -radius_ + (static_cast<float>(i) + 0.5f) * sliceDepth;
        const float halfWidth = std::sqrt((std::max)(radius_ * radius_ - z * z, 0.0f));
        add({ 0.0f, floorY_ - thickness * 0.5f, z }, { halfWidth + halfWidth, thickness, sliceDepth }, 0.0f, false);
    }
    const int rimCount = std::clamp(tune.rimCount, 1, kMaxPieces);
    const float rimHeight = (std::max)(tune.rimHeight, kMinimumSize);
    const float rimWidth = (std::max)(tune.rimWidth, kMinimumSize);
    for (int i = 0; i < rimCount; ++i) {
        const float angle = kTau * static_cast<float>(i) / static_cast<float>(rimCount);
        add({ radius_ * std::cos(angle), floorY_ + rimHeight * 0.5f, radius_ * std::sin(angle) },
            { rimWidth, rimHeight, kTau * radius_ / static_cast<float>(rimCount) }, -angle, true);
    }
    const int pillars = std::clamp(tune.pillarCount, 0, kMaxPieces);
    const float height = (std::max)(tune.pillarHeight, kMinimumSize);
    const float width = (std::max)(tune.pillarWidth, kMinimumSize);
    const float distance = (std::max)(tune.pillarDistance, radius_ + width);
    for (int i = 0; i < pillars; ++i) {
        const float angle = kTau * static_cast<float>(i) / static_cast<float>(pillars);
        add({ distance * std::cos(angle), floorY_ + height * 0.5f, distance * std::sin(angle) },
            { width, height, width }, -angle, false);
    }
    particles_ = setup.particles;
    if (particles_) {
        // シーンを抜ける時に全グループが消されるので、名前は固定でよい。
        particleGroup_ = kParticleGroup;
        particles_->CreateParticleGroup(particleGroup_, kWhite);
        particles_->SetAdditiveBlend(particleGroup_, true);
    }
    Update(0.0f);
}

/** @brief 中心と回転角を保持する。 */
void FinalBossArena::SetFrame(const Vector3& center, float yaw) {
    center_ = center;
    yaw_ = yaw;
}

/** @brief 発光の補間量を範囲内に収める。 */
void FinalBossArena::SetRage(float amount) {
    rage_ = std::clamp(amount, 0.0f, 1.0f);
}

/** @brief 行ベクトルを使う既存の回転行列で配置する。 */
Vector3 FinalBossArena::ToScene(const Vector3& local) const {
    const auto rotation = MakeRotateYMatrix(yaw_);
    return { center_.x + local.x * rotation.m[0][0] + local.z * rotation.m[2][0],
        center_.y + local.y,
        center_.z + local.x * rotation.m[0][2] + local.z * rotation.m[2][2] };
}

/** @brief 時間と外部フレームから明滅と全行列を更新する。 */
void FinalBossArena::Update(float deltaTime) {
    const float dt = (std::max)(deltaTime, 0.0f);
    time_ += dt;
    const float pulse = 1.0f - pulseDepth_ * (0.5f + 0.5f * std::sin(time_ * pulseSpeed_));
    const Vector4 color = { std::lerp(calmColor_.x, rageColor_.x, rage_) * pulse,
        std::lerp(calmColor_.y, rageColor_.y, rage_) * pulse,
        std::lerp(calmColor_.z, rageColor_.z, rage_) * pulse, std::lerp(calmColor_.w, rageColor_.w, rage_) };
    for (auto& piece : pieces_) {
        piece.object->SetPosition(ToScene(piece.position));
        piece.object->SetRotation({ 0.0f, piece.yaw + yaw_, 0.0f });
        if (piece.glow) {
            piece.object->SetColor(color);
        }
        piece.object->Update();
    }
    if (!particles_) {
        return;
    }
    // 長いフレームでも放出数を制限し、既存の粒用資源を使う。
    emitClock_ = (std::min)(emitClock_ + dt, particleInterval_ * kMaxEmissions);
    while (emitClock_ >= particleInterval_) {
        emitClock_ -= particleInterval_;
        const float angle = (time_ - emitClock_) * kOrbitSpeed;
        const Vector3 local = { radius_ * kRadialFraction * std::cos(angle),
            floorY_ + particleHeight_ * (0.5f + 0.5f * std::sin(angle)), radius_ * kRadialFraction * std::sin(angle) };
        particles_->EmitWithColor(particleGroup_, ToScene(local), { 0.0f, particleSpeed_, 0.0f },
            color, particleLife_, particleSize_, true);
    }
}

/** @brief 最新カメラの行列を反映して描画する。 */
void FinalBossArena::Draw() {
    for (auto& piece : pieces_) {
        // カメラ更新の順序によらず描画直前の行列を使う。
        piece.object->Update();
        piece.object->Draw();
    }
}

/** @brief 歩ける半径を返す。 */
float FinalBossArena::GetRadius() const {
    return radius_;
}

/** @brief 中心の高さを含む床上面を返す。 */
float FinalBossArena::GetFloorY() const {
    return center_.y + floorY_;
}
} // namespace engine::game
