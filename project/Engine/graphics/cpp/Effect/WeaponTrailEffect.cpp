/**
 * @file WeaponTrailEffect.cpp
 * @brief WeaponTrailEffectの軌跡サンプルの管理、Catmull-Rom補間による帯の生成、描画を実装するファイル
 */
#include "WeaponTrailEffect.h"
#include "PipelineHelper.h"
#include "GraphicsTuning.h"
#include "Camera.h"
#include "EngineAssert.h"
#include <algorithm>

using namespace Microsoft::WRL;

namespace engine::graphics {
namespace {

constexpr float kMinLifetime = 0.001f; // 寿命0で割らないための下限

/** @brief Catmull-Rom曲線の項の数（t^0〜t^3 の4次数、制御点も4つ） */
constexpr int kCatmullRomTerms = 4;
/** @brief Catmull-Rom曲線の基底行列（行が t^0〜t^3 の係数、列が p0〜p3 にかかる重み） */
constexpr float kCatmullRomBasis[kCatmullRomTerms][kCatmullRomTerms] = {
    { 0.0f, 2.0f, 0.0f, 0.0f },
    { -1.0f, 0.0f, 1.0f, 0.0f },
    { 2.0f, -5.0f, 4.0f, -1.0f },
    { -1.0f, 3.0f, -3.0f, 1.0f },
};
/** @brief 基底行列で求めた多項式に掛ける全体の係数（張力 0.5 の Catmull-Rom） */
constexpr float kCatmullRomScale = 0.5f;

/** @brief Catmull-Rom曲線上の点を返す（p1→p2間をtで補間し、p0/p3は接線を決める前後の点） */
Vector3 CatmullRom(const Vector3& p0, const Vector3& p1, const Vector3& p2, const Vector3& p3, float t)
{
    const float powers[kCatmullRomTerms] = { 1.0f, t, t * t, t * t * t };
    // 1軸ぶんの4点の値から、t の各次数の係数を基底行列で求めて足し合わせる
    auto axis = [&powers](float a0, float a1, float a2, float a3) {
        float sum = 0.0f;
        for (int degree = 0; degree < kCatmullRomTerms; ++degree)
        {
            const float* w = kCatmullRomBasis[degree];
            sum += (w[0] * a0 + w[1] * a1 + w[2] * a2 + w[3] * a3) * powers[degree];
        }
        return kCatmullRomScale * sum;
    };
    return { axis(p0.x, p1.x, p2.x, p3.x), axis(p0.y, p1.y, p2.y, p3.y), axis(p0.z, p1.z, p2.z, p3.z) };
}

} // namespace

/** @brief 武器の軌跡の描画資源を用意する */
void WeaponTrailEffect::Initialize(DirectXCommon* dxCommon)
{
    ENGINE_ASSERT(dxCommon);
    dxCommon_ = dxCommon;
    style_.tipColor = GraphicsTuning::Color("weaponTrail.tipColor", style_.tipColor);
    style_.baseColor = GraphicsTuning::Color("weaponTrail.baseColor", style_.baseColor);
    style_.lifetime = GraphicsTuning::Float("weaponTrail.lifetime", style_.lifetime, kMinLifetime);
    style_.subdivisions = GraphicsTuning::Int("weaponTrail.subdivisions", style_.subdivisions, 1, kMaxSubdivisions);

    sceneCB_ = dxCommon_->CreateBufferResource(sizeof(SceneCB));
    sceneCB_->Map(0, nullptr, reinterpret_cast<void**>(&sceneCBData_));
    sceneCBData_->viewProj = MakeIdentity4x4();

    vertexBuffer_ = dxCommon_->CreateBufferResource(sizeof(TrailVertex) * kMaxVertices);
    vertexBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData_));
    vbv_.BufferLocation = vertexBuffer_->GetGPUVirtualAddress();
    vbv_.SizeInBytes = static_cast<UINT>(sizeof(TrailVertex) * kMaxVertices);
    vbv_.StrideInBytes = sizeof(TrailVertex);

    CreatePipeline();
}

/** @brief 今フレームの刃の位置を積む */
void WeaponTrailEffect::AddSample(const Vector3& base, const Vector3& tip)
{
    if (static_cast<int>(samples_.size()) >= kMaxSamples)
    {
        samples_.pop_front();
    }
    samples_.push_back({ base, tip, 0.0f, strokeId_ });
}

/** @brief 寿命を進めて頂点バッファを組み直す */
void WeaponTrailEffect::Update(float dt, Camera* camera)
{
    if (camera != nullptr)
    {
        sceneCBData_->viewProj = Multiply(camera->GetViewMatrix(), camera->GetProjectionMatrix());
    }

    for (auto& s : samples_)
    {
        s.age += dt;
    }
    while (!samples_.empty() && samples_.front().age >= style_.lifetime)
    {
        samples_.pop_front();
    }

    RebuildVertices();
}

/** @brief 補間位置1点ぶんの頂点色を作る */
Vector4 WeaponTrailEffect::MakeColor(const Vector4& color, float age) const
{
    // 生まれた直後が最も濃く、寿命に向かって二乗で抜ける（尾がすっと消える）
    const float life = (std::max)(style_.lifetime, kMinLifetime);
    float fade = std::clamp(1.0f - age / life, 0.0f, 1.0f);
    fade *= fade;
    return { color.x, color.y, color.z, color.w * fade };
}

/** @brief サンプル列から帯の三角形を組み立てる */
void WeaponTrailEffect::RebuildVertices()
{
    // 頂点を作り直す（分割数は上限で丸める）
    vertexCount_ = 0;
    const int count = static_cast<int>(samples_.size());
    const int subdiv = std::clamp(style_.subdivisions, 1, kMaxSubdivisions);

    auto pushVertex = [this](const Vector3& p, const Vector4& c) {
        vertexData_[vertexCount_++] = { { p.x, p.y, p.z, 1.0f }, c };
    };

    // 隣り合う2つの記録点の間を、同じ振りの中だけ帯でつなぐ
    for (int i = 0; i + 1 < count; ++i)
    {
        const Sample& a = samples_[i];
        const Sample& b = samples_[i + 1];
        if (a.stroke != b.stroke)
        {
            continue;
        }
        // 帯の端では前後の点が無いので端点自身を使う（端で曲線が外へはみ出さない）
        const Sample& prev = (i > 0 && samples_[i - 1].stroke == a.stroke) ? samples_[i - 1] : a;
        const Sample& next = (i + 2 < count && samples_[i + 2].stroke == a.stroke) ? samples_[i + 2] : b;

        for (int s = 0; s < subdiv; ++s)
        {
            if (vertexCount_ + kVertsPerQuad > kMaxVertices)
            {
                return;
            }
            // 区間を分割数ぶんに切り、根元と刃先をそれぞれ曲線で補間する
            const float t0 = static_cast<float>(s) / static_cast<float>(subdiv);
            const float t1 = static_cast<float>(s + 1) / static_cast<float>(subdiv);

            const Vector3 base0 = CatmullRom(prev.base, a.base, b.base, next.base, t0);
            const Vector3 base1 = CatmullRom(prev.base, a.base, b.base, next.base, t1);
            const Vector3 tip0 = CatmullRom(prev.tip, a.tip, b.tip, next.tip, t0);
            const Vector3 tip1 = CatmullRom(prev.tip, a.tip, b.tip, next.tip, t1);
            const float age0 = a.age + (b.age - a.age) * t0;
            const float age1 = a.age + (b.age - a.age) * t1;

            // 古い点ほど薄くする色
            const Vector4 baseColor0 = MakeColor(style_.baseColor, age0);
            const Vector4 baseColor1 = MakeColor(style_.baseColor, age1);
            const Vector4 tipColor0 = MakeColor(style_.tipColor, age0);
            const Vector4 tipColor1 = MakeColor(style_.tipColor, age1);

            // 四角形1つを三角形2つで作る
            pushVertex(base0, baseColor0);
            pushVertex(tip0, tipColor0);
            pushVertex(tip1, tipColor1);
            pushVertex(base0, baseColor0);
            pushVertex(tip1, tipColor1);
            pushVertex(base1, baseColor1);
        }
    }
}

/** @brief 最新のサンプルから指定秒数さかのぼった間に刃先が動いた量を返す */
bool WeaponTrailEffect::GetRecentTipMotion(float window, Vector3& outMotion) const
{
    if (samples_.size() < 2)
    {
        return false;
    }
    const Sample& newest = samples_.back();
    const Sample* oldest = nullptr;
    for (auto it = samples_.rbegin() + 1; it != samples_.rend(); ++it)
    {
        if (it->stroke != newest.stroke || it->age - newest.age > window)
        {
            break;
        }
        oldest = &(*it);
    }
    if (oldest == nullptr)
    {
        return false;
    }
    outMotion = { newest.tip.x - oldest->tip.x, newest.tip.y - oldest->tip.y, newest.tip.z - oldest->tip.z };
    return true;
}

/** @brief 帯の描画コマンドを積む（3D描画パス内で呼ぶ） */
void WeaponTrailEffect::Draw()
{
    if (vertexCount_ == 0)
    {
        return;
    }

    PipelineHelper::DrawVertexColorEffect(dxCommon_->GetCommandList(), rootSignature_.Get(), pipelineState_.Get(),
        vbv_, sceneCB_->GetGPUVirtualAddress(), vertexCount_);
}

/** @brief 武器の軌跡用のパイプラインを生成する */
void WeaponTrailEffect::CreatePipeline()
{
    PipelineHelper::CreateVertexColorEffectPipeline(dxCommon_, rootSignature_, pipelineState_);
}

} // namespace engine::graphics
