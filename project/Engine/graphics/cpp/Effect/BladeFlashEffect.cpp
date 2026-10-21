/**
 * @file BladeFlashEffect.cpp
 * @brief 刃の形の閃光パーティクル（BladeFlashEffect）の実装
 */
#include "BladeFlashEffect.h"
#include "PipelineHelper.h"
#include "GraphicsTuning.h"
#include "Camera.h"
#include "EngineAssert.h"
#include <algorithm>
#include <cmath>

namespace engine::graphics {
namespace {

// 刃の配色
constexpr engine::Vector4 kBlueCoreColor = { 0.95f, 1.0f, 1.0f, 1.0f };
constexpr engine::Vector4 kBlueEdgeColor = { 0.35f, 0.7f, 1.0f, 1.0f };
constexpr engine::Vector4 kPurpleCoreColor = { 0.9f, 0.8f, 1.0f, 1.0f };
constexpr engine::Vector4 kPurpleEdgeColor = { 0.55f, 0.3f, 1.0f, 1.0f };

// 三角形の頂点数
constexpr int kTriangleVertexCount = 3;

// 刃の生成範囲と伸長・減衰
constexpr float kFullTurn = 6.2831853f;
constexpr float kDepthSpread = 0.4f;
constexpr float kWidthRatioMin = 0.045f;
constexpr float kWidthRatioMax = 0.09f;
constexpr float kLifeMin = 0.14f;
constexpr float kLifeMax = 0.30f;
constexpr float kDriftSpeed = 1.5f;
constexpr float kBlueProbability = 0.8f;
constexpr float kGrowthRate = 5.0f;
constexpr float kFadeInRate = 8.0f;
constexpr float kInitialOpacity = 0.35f;
constexpr float kMinLife = 0.001f;

}

using namespace Microsoft::WRL;

/** @brief 刃の閃光の描画資源を用意する */
void BladeFlashEffect::Initialize(DirectXCommon* dxCommon)
{
    ENGINE_ASSERT(dxCommon);
    dxCommon_ = dxCommon;

    sceneCB_ = dxCommon_->CreateBufferResource(sizeof(SceneCB));
    sceneCB_->Map(0, nullptr, reinterpret_cast<void**>(&sceneCBData_));
    sceneCBData_->viewProj = MakeIdentity4x4();

    vertexBuffer_ = dxCommon_->CreateBufferResource(sizeof(BladeVertex) * kMaxBlades * kVertsPerBlade);
    vertexBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData_));
    vbv_.BufferLocation = vertexBuffer_->GetGPUVirtualAddress();
    vbv_.SizeInBytes = static_cast<UINT>(sizeof(BladeVertex) * kMaxBlades * kVertsPerBlade);
    vbv_.StrideInBytes = sizeof(BladeVertex);

    CreatePipeline();
}

/** @brief 指定位置の周囲に刃を発生させる */
void BladeFlashEffect::Emit(const Vector3& center, int count, float radius, float minLen, float maxLen)
{
    // 大きさ・寿命・色の範囲は GraphicsTuning から読む
    const float depthSpread = GraphicsTuning::Float("bladeFlash.depthSpread", kDepthSpread);
    const float widthMin = GraphicsTuning::Float("bladeFlash.widthRatioMin", kWidthRatioMin);
    const float widthMax = (std::max)(widthMin, GraphicsTuning::Float("bladeFlash.widthRatioMax", kWidthRatioMax));
    const float lifeMin = GraphicsTuning::Float("bladeFlash.lifeMin", kLifeMin, kMinLife);
    const float lifeMax = (std::max)(lifeMin, GraphicsTuning::Float("bladeFlash.lifeMax", kLifeMax, kMinLife));
    const float driftSpeed = GraphicsTuning::Float("bladeFlash.driftSpeed", kDriftSpeed);
    const float blueProbability = GraphicsTuning::Float("bladeFlash.blueProbability", kBlueProbability, 0.0f, 1.0f);
    const Vector4 blueCore = GraphicsTuning::Color("bladeFlash.blueCoreColor", kBlueCoreColor);
    const Vector4 blueEdge = GraphicsTuning::Color("bladeFlash.blueEdgeColor", kBlueEdgeColor);
    const Vector4 purpleCore = GraphicsTuning::Color("bladeFlash.purpleCoreColor", kPurpleCoreColor);
    const Vector4 purpleEdge = GraphicsTuning::Color("bladeFlash.purpleEdgeColor", kPurpleEdgeColor);
    // 位置・向き・長さ・寿命などを決める乱数の範囲
    std::uniform_real_distribution<float> angDist(0.0f, kFullTurn);
    std::uniform_real_distribution<float> radDist(0.0f, radius);
    std::uniform_real_distribution<float> zDist(-depthSpread, depthSpread);
    std::uniform_real_distribution<float> lenDist(minLen, maxLen);
    std::uniform_real_distribution<float> widthDist(widthMin, widthMax);
    std::uniform_real_distribution<float> lifeDist(lifeMin, lifeMax);
    std::uniform_real_distribution<float> driftDist(-driftSpeed, driftSpeed);
    std::uniform_real_distribution<float> hueDist(0.0f, 1.0f);

    // 上限に達するまで、中心の周りのランダムな位置・向き・長さで刃を足す
    for (int i = 0; i < count; ++i)
    {
        if (static_cast<int>(blades_.size()) >= kMaxBlades)
        {
            break;
        }

        Blade b;
        const float posAng = angDist(rng_);
        const float posRad = radDist(rng_);
        b.pos = { center.x + std::cos(posAng) * posRad,
            center.y + std::sin(posAng) * posRad,
            center.z + zDist(rng_) };

        // 刃の向きと大きさ、寿命、刃の向きに沿った漂い
        b.angle = angDist(rng_);
        b.halfLen = lenDist(rng_) * 0.5f;
        b.halfWidth = b.halfLen * widthDist(rng_);
        b.life = lifeDist(rng_);
        b.drift = Vector3 { std::cos(b.angle), std::sin(b.angle), 0.0f } * driftDist(rng_);

        // 大半は氷青、たまに紫がかった刃を混ぜる
        if (hueDist(rng_) < blueProbability)
        {
            b.coreColor = blueCore;
            b.edgeColor = blueEdge;
        }
        else
        {
            b.coreColor = purpleCore;
            b.edgeColor = purpleEdge;
        }

        blades_.push_back(b);
    }
}

/** @brief 寿命更新と頂点バッファの再構築毎フレーム呼ぶ */
void BladeFlashEffect::Update(float dt, Camera* camera)
{
    const float growthRate = GraphicsTuning::Float("bladeFlash.growthRate", kGrowthRate);
    const float fadeInRate = GraphicsTuning::Float("bladeFlash.fadeInRate", kFadeInRate);
    const float initialOpacity = GraphicsTuning::Float("bladeFlash.initialOpacity", kInitialOpacity, 0.0f, 1.0f);
    if (camera != nullptr)
    {
        sceneCBData_->viewProj = Multiply(camera->GetViewMatrix(), camera->GetProjectionMatrix());
    }

    // 刃を流し、寿命を過ぎたものを消す
    for (auto& b : blades_)
    {
        b.age += dt;
        b.pos = b.pos + b.drift * dt;
    }
    blades_.erase(
        std::remove_if(blades_.begin(), blades_.end(),
            [](const Blade& b) {
                return b.age >= b.life;
            }),
        blades_.end());

    // 頂点バッファ再構築（中心が白熱し、先端・側面へ透ける菱形）
    vertexCount_ = 0;
    for (const auto& b : blades_)
    {
        const float t = b.age / b.life;

        // 出現直後に一気に伸び、余韻で消える
        const float grow = (std::min)(1.0f, t * growthRate);
        const float fade = (1.0f - t) * (1.0f - t) * (std::min)(1.0f, t * fadeInRate + initialOpacity);
        const float width = b.halfWidth * (1.0f - t * 0.5f);

        const Vector3 dir = Vector3 { std::cos(b.angle), std::sin(b.angle), 0.0f } * (b.halfLen * grow);
        const Vector3 perp = Vector3 { -std::sin(b.angle), std::cos(b.angle), 0.0f } * width;

        // 中心から長さ方向の2つの先端と、幅方向の2つの側の点
        const Vector3 tipA = b.pos + dir;
        const Vector3 tipB = Subtract(b.pos, dir);
        const Vector3 sideA = b.pos + perp;
        const Vector3 sideB = Subtract(b.pos, perp);

        // 中心は濃く、側は半分の濃さ、先端は透明にする
        const Vector4 core = { b.coreColor.x, b.coreColor.y, b.coreColor.z, fade * b.coreColor.w };
        const Vector4 side = { b.edgeColor.x, b.edgeColor.y, b.edgeColor.z, fade * b.edgeColor.w * 0.5f };
        const Vector4 tip = { b.edgeColor.x, b.edgeColor.y, b.edgeColor.z, 0.0f };

        auto pushTri = [this](const Vector3& p0, const Vector4& c0,
                           const Vector3& p1, const Vector4& c1,
                           const Vector3& p2, const Vector4& c2) {
            vertexData_[vertexCount_ + 0] = { { p0.x, p0.y, p0.z, 1.0f }, c0 };
            vertexData_[vertexCount_ + 1] = { { p1.x, p1.y, p1.z, 1.0f }, c1 };
            vertexData_[vertexCount_ + 2] = { { p2.x, p2.y, p2.z, 1.0f }, c2 };
            vertexCount_ += kTriangleVertexCount;
        };
        // 中心から4枚の三角形で菱形を作る
        pushTri(b.pos, core, tipA, tip, sideA, side);
        pushTri(b.pos, core, sideA, side, tipB, tip);
        pushTri(b.pos, core, tipB, tip, sideB, side);
        pushTri(b.pos, core, sideB, side, tipA, tip);
    }
}

/** @brief 全刃の描画コマンドを積む3D描画パス内で呼ぶ */
void BladeFlashEffect::Draw()
{
    if (vertexCount_ == 0)
    {
        return;
    }

    PipelineHelper::DrawVertexColorEffect(dxCommon_->GetCommandList(), rootSignature_.Get(), pipelineState_.Get(),
        vbv_, sceneCB_->GetGPUVirtualAddress(), vertexCount_);
}

/** @brief 刃描画用のルートシグネチャ・PSOを生成する */
void BladeFlashEffect::CreatePipeline()
{
    PipelineHelper::CreateVertexColorEffectPipeline(dxCommon_, rootSignature_, pipelineState_);
}

} // namespace engine::graphics
