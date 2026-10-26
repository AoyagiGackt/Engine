/**
 * @file MeshSliceEffect.cpp
 * @brief モデルを切断して飛ばす演出（MeshSliceEffect）の実装
 */
#include "MeshSliceEffect.h"
#include "PipelineHelper.h"
#include "GraphicsTuning.h"
#include "Camera.h"
#include "EngineAssert.h"
#include "Model.h"
#include "SrvManager.h"
#include "TextureManager.h"
#include <algorithm>
#include <cmath>

namespace engine::graphics {
namespace {

// 破片の発光色
constexpr float kGlowRed = 0.65f;
constexpr float kGlowGreen = 0.9f;

// 三角形の切断と縮退判定
constexpr int kTriangleVertexCount = 3;
constexpr int kClippedPolygonCapacity = 4;
constexpr size_t kMinCapPointCount = 4;
constexpr float kPlaneDistanceEpsilon = 1.0e-5f;

// 切断面の分布と破片の運動・発光
constexpr float kSlideRadiusRatio = 0.35f;
constexpr float kFullTurn = 6.2831853f;
constexpr float kPlaneTilt = 0.35f;
constexpr float kPlaneOffsetRatio = 0.3f;
constexpr float kDirectionJitter = 0.25f;
constexpr float kBurstSpeedMin = 2.5f;
constexpr float kBurstSpeedMax = 4.5f;
constexpr float kUpSpeedMax = 2.5f;
constexpr float kSpinSpeed = 6.0f;
constexpr float kDirectionEpsilon = 1.0e-4f;
constexpr float kGravityScale = 10.0f;
constexpr float kBurstGlow = 1.2f;
constexpr float kInitialGlow = 0.15f;
constexpr float kSlideGlowGain = 1.05f;
constexpr float kTintGain = 0.4f;

}

using namespace Microsoft::WRL;

/** @brief メッシュを切る演出の描画資源を用意する */
void MeshSliceEffect::Initialize(DirectXCommon* dxCommon) {
    ENGINE_ASSERT(dxCommon);
    dxCommon_ = dxCommon;

    // 破片ごとの定数バッファは最大数ぶんを最初に確保しておく
    pieceCB_ = dxCommon_->CreateBufferResource(kCBSlotSize * kMaxPieces);
    pieceCB_->Map(0, nullptr, reinterpret_cast<void**>(&pieceCBData_));

    CreatePipeline();
}

/** @brief モデルを切断して再生を開始する */
void MeshSliceEffect::Start(const Model* model, const Vector3& worldPos, const Vector3& scale, uint32_t seed) {
    if (model == nullptr || model->GetVertexCount() == 0) {
        return;
    }

    worldPos_ = worldPos;
    scale_ = scale;
    textureFilePath_ = model->GetTextureFilePath();
    TextureManager::GetInstance()->LoadTexture(textureFilePath_);

    std::mt19937 rng(seed);
    std::vector<SliceVertex> vertices;
    BuildPieces(model, rng, vertices);
    if (pieces_.empty()) {
        return;
    }

    UploadVertices(vertices);

    timer_ = 0.0f;
    active_ = true;
}

/** @brief 再生を打ち切って非アクティブに戻す */
void MeshSliceEffect::Reset() {
    active_ = false;
    timer_ = 0.0f;
    pieces_.clear();
}

/** @brief 破片が飛散するフェーズに入ったか */
bool MeshSliceEffect::IsBursting() const {
    const float holdTime = GraphicsTuning::Float("meshSlice.holdTime", kHoldTime);
    const float slideTime = GraphicsTuning::Positive("meshSlice.slideTime", kSlideTime);
    return active_ && timer_ >= holdTime + slideTime;
}

/** @brief 画面暗転オーバーレイの推奨ウェイト（静止〜ずれ中は1、飛散中は減衰、非アクティブは0） */
float MeshSliceEffect::GetOverlayWeight() const {
    if (!active_) {
        return 0.0f;
    }
    const float burstStart = GraphicsTuning::Float("meshSlice.holdTime", kHoldTime)
        + GraphicsTuning::Positive("meshSlice.slideTime", kSlideTime);
    if (timer_ < burstStart) {
        return 1.0f;
    }
    const float burstTime = GraphicsTuning::Positive("meshSlice.burstTime", kBurstTime);
    return std::clamp(1.0f - (timer_ - burstStart) / burstTime, 0.0f, 1.0f);
}

// 切断

/** @brief 2頂点の位置・UV・法線を線形補間する（三角形を平面で切るときの交点に使う） */
MeshSliceEffect::SliceVertex MeshSliceEffect::LerpVertex(const SliceVertex& a, const SliceVertex& b, float t) {
    SliceVertex v;
    v.position = { a.position.x + (b.position.x - a.position.x) * t,
        a.position.y + (b.position.y - a.position.y) * t,
        a.position.z + (b.position.z - a.position.z) * t,
        1.0f };
    v.texcoord = { a.texcoord.x + (b.texcoord.x - a.texcoord.x) * t,
        a.texcoord.y + (b.texcoord.y - a.texcoord.y) * t };
    v.normal = Normalize(Lerp(a.normal, b.normal, t));
    v.cap = a.cap + (b.cap - a.cap) * t;
    return v;
}

/** @brief 平面をまたぐ三角形を片側だけ切り出す交点は outCutPoints に追加される */
void MeshSliceEffect::ClipTriangle(const SliceVertex tri[3], const float dist[3], bool keepPositive,
    std::vector<SliceVertex>& outTris, std::vector<Vector3>* outCutPoints) {
    SliceVertex poly[kClippedPolygonCapacity];
    int polyCount = 0;

    for (int i = 0; i < kTriangleVertexCount; ++i) {
        const int j = (i + 1) % kTriangleVertexCount;
        const float di = keepPositive ? dist[i] : -dist[i];
        const float dj = keepPositive ? dist[j] : -dist[j];

        if (di >= 0.0f) {
            poly[polyCount] = tri[i];
            polyCount++;
        }
        if ((di > 0.0f && dj < 0.0f) || (di < 0.0f && dj > 0.0f)) {
            const float t = di / (di - dj);
            SliceVertex m = LerpVertex(tri[i], tri[j], t);
            poly[polyCount] = m;
            polyCount++;
            if (outCutPoints != nullptr) {
                outCutPoints->push_back({ m.position.x, m.position.y, m.position.z });
            }
        }
    }

    // 扇状に三角形へ分解（頂点順序は維持される）
    for (int k = 1; k + 1 < polyCount; ++k) {
        outTris.push_back(poly[0]);
        outTris.push_back(poly[k]);
        outTris.push_back(poly[k + 1]);
    }
}

/** @brief 破片を平面（法線n・通過点p0）で表裏に分割し、切断面のフタも生成する */
void MeshSliceEffect::SplitPiece(const PieceBuild& src, const Vector3& n, const Vector3& p0,
    PieceBuild& outFront, PieceBuild& outBack) {
    std::vector<Vector3> cutPoints; // 交線上の点（2個で1辺）

    const size_t triCount = src.tris.size() / kTriangleVertexCount;
    for (size_t t = 0; t < triCount; ++t) {
        const SliceVertex* v = &src.tris[t * kTriangleVertexCount];

        float d[kTriangleVertexCount];
        for (int i = 0; i < kTriangleVertexCount; ++i) {
            d[i] = Dot({ v[i].position.x - p0.x, v[i].position.y - p0.y, v[i].position.z - p0.z }, n);
        }

        // kTriangleVertexCount頂点が片側に収まる三角形はそのまま振り分ける
        if (d[0] >= -kPlaneDistanceEpsilon && d[1] >= -kPlaneDistanceEpsilon && d[2] >= -kPlaneDistanceEpsilon) {
            outFront.tris.insert(outFront.tris.end(), v, v + kTriangleVertexCount);
            continue;
        }
        if (d[0] <= kPlaneDistanceEpsilon && d[1] <= kPlaneDistanceEpsilon && d[2] <= kPlaneDistanceEpsilon) {
            outBack.tris.insert(outBack.tris.end(), v, v + kTriangleVertexCount);
            continue;
        }

        // 平面をまたぐ三角形は両側に切り分ける（交点は表側の処理でだけ集める）
        ClipTriangle(v, d, true, outFront.tris, &cutPoints);
        ClipTriangle(v, d, false, outBack.tris, nullptr);
    }

    // 切断面のフタ（交線の重心から扇状に張る発光フラグ付き）
    if (cutPoints.size() >= kMinCapPointCount) {
        Vector3 c = { };
        for (const auto& p : cutPoints) {
            c = c + p;
        }
        c = c * (1.0f / static_cast<float>(cutPoints.size()));

        auto addCap = [&c, &cutPoints](PieceBuild& dst, const Vector3& capNormal) {
            for (size_t k = 0; k + 1 < cutPoints.size(); k += 2) {
                const Vector3& a = cutPoints[k];
                const Vector3& b = cutPoints[k + 1];
                dst.tris.push_back({ { c.x, c.y, c.z, 1.0f }, { 0.5f, 0.5f }, capNormal, 1.0f });
                dst.tris.push_back({ { a.x, a.y, a.z, 1.0f }, { 0.5f, 0.5f }, capNormal, 1.0f });
                dst.tris.push_back({ { b.x, b.y, b.z, 1.0f }, { 0.5f, 0.5f }, capNormal, 1.0f });
            }
        };
        addCap(outFront, { -n.x, -n.y, -n.z });
        addCap(outBack, n);
    }
}

/** @brief モデルを平面群で切断し、破片リストと頂点列を構築する */
void MeshSliceEffect::BuildPieces(const Model* model, std::mt19937& rng, std::vector<SliceVertex>& outVertices) {
    // 分割と飛び散り方の調整値は GraphicsTuning から読む
    const float slideRatio = GraphicsTuning::Float("meshSlice.slideRadiusRatio", kSlideRadiusRatio);
    const float planeTilt = GraphicsTuning::Float("meshSlice.planeTilt", kPlaneTilt);
    const float planeOffset = GraphicsTuning::Float("meshSlice.planeOffsetRatio", kPlaneOffsetRatio);
    const float jitter = GraphicsTuning::Float("meshSlice.directionJitter", kDirectionJitter);
    const float speedMin = GraphicsTuning::Float("meshSlice.burstSpeedMin", kBurstSpeedMin);
    const float speedMax = (std::max)(speedMin, GraphicsTuning::Float("meshSlice.burstSpeedMax", kBurstSpeedMax));
    const float upSpeedMax = GraphicsTuning::Float("meshSlice.upSpeedMax", kUpSpeedMax, 1.0f);
    const float spinSpeed = GraphicsTuning::Float("meshSlice.spinSpeed", kSpinSpeed);
    const int planeCount = GraphicsTuning::Int("meshSlice.planeCount", kPlaneCount, 1, kPlaneCount);
    // 元メッシュを1つの破片として取り込む
    PieceBuild whole;
    const auto& srcVerts = model->GetVertices();
    const auto& srcIdx = model->GetIndices();
    auto pushVert = [&whole](const Model::VertexData& v) {
        whole.tris.push_back({ v.position, v.texcoord, v.normal, 0.0f });
    };
    if (!srcIdx.empty()) {
        for (uint32_t i : srcIdx) {
            pushVert(srcVerts[i]);
        }
    } else {
        for (const auto& v : srcVerts) {
            pushVert(v);
        }
    }

    // 頂点の平均位置（破片の重心）
    auto centroidOf = [](const PieceBuild& piece) {
        Vector3 sum = { };
        for (const auto& v : piece.tris) {
            sum = sum + Vector3 { v.position.x, v.position.y, v.position.z };
        }
        return sum * (1.0f / static_cast<float>(piece.tris.size()));
    };

    // メッシュ全体の重心と半径（切断位置と運動量のスケール基準）
    const Vector3 meshCenter = centroidOf(whole);

    float radius = 0.0f;
    for (const auto& v : whole.tris) {
        radius = (std::max)(radius, Distance(meshCenter, { v.position.x, v.position.y, v.position.z }));
    }
    if (radius <= 0.0f) {
        radius = 1.0f;
    }
    meshRadius_ = radius;
    slideDist_ = radius * slideRatio;

    // 中心付近を通るランダムな平面群で分割していく
    // 法線はほぼXY面内に寄せて、横視点から切り口が見えやすい向きにする
    std::uniform_real_distribution<float> angDist(0.0f, kFullTurn);
    std::uniform_real_distribution<float> tiltDist(-planeTilt, planeTilt);
    std::uniform_real_distribution<float> offDist(-planeOffset, planeOffset);

    std::vector<PieceBuild> built;
    built.push_back(std::move(whole));
    std::vector<std::pair<Vector3, Vector3>> planes; // 法線・通過点

    for (int i = 0; i < planeCount; ++i) {
        const float ang = angDist(rng);
        const Vector3 n = Normalize(Vector3 { std::cos(ang), std::sin(ang), tiltDist(rng) });
        const Vector3 p0 = meshCenter + Vector3 { offDist(rng) * radius, offDist(rng) * radius, 0.0f };
        planes.emplace_back(n, p0);

        // 今ある破片をそれぞれ平面で2つに割る（片側が空なら割らずに残す。上限に達したら以降は割らない）
        std::vector<PieceBuild> next;
        for (auto& piece : built) {
            if (static_cast<int>(next.size()) >= kMaxPieces - 1) {
                next.push_back(std::move(piece));
                continue;
            }
            PieceBuild front, back;
            SplitPiece(piece, n, p0, front, back);
            if (front.tris.empty() || back.tris.empty()) {
                next.push_back(std::move(piece));
            } else {
                next.push_back(std::move(front));
                next.push_back(std::move(back));
            }
        }
        built = std::move(next);
    }

    // 破片ごとの頂点範囲と運動パラメータを確定する
    std::uniform_real_distribution<float> jitterDist(-jitter, jitter);
    std::uniform_real_distribution<float> speedDist(speedMin, speedMax);
    std::uniform_real_distribution<float> upDist(1.0f, upSpeedMax);
    std::uniform_real_distribution<float> spinDist(-spinSpeed, spinSpeed);

    // 破片ごとに頂点を1つの配列へ並べ、重心・ずれる向き・速度・回転速度を決める
    outVertices.clear();
    pieces_.clear();
    for (auto& pb : built) {
        Piece pc;
        pc.vertexOffset = static_cast<uint32_t>(outVertices.size());
        pc.vertexCount = static_cast<uint32_t>(pb.tris.size());
        outVertices.insert(outVertices.end(), pb.tris.begin(), pb.tris.end());

        pc.centroid = centroidOf(pb);

        // ずれる方向 = 各切断面のどちら側にいるかを合成したもの
        Vector3 dir = { };
        for (const auto& [n, p0] : planes) {
            const float side = (Dot(Subtract(pc.centroid, p0), n) >= 0.0f) ? 1.0f : -1.0f;
            dir = dir + n * side;
        }
        dir = dir + Vector3 { jitterDist(rng), jitterDist(rng), jitterDist(rng) };
        if (Length(dir) < kDirectionEpsilon) {
            dir = { 0.0f, 1.0f, 0.0f };
        }
        pc.slideDir = Normalize(dir);

        // 飛び散る速さはメッシュの大きさに比例させ、少し上向きの速度を足す
        pc.velocity = pc.slideDir * (radius * speedDist(rng)) + Vector3 { 0.0f, radius * upDist(rng), 0.0f };
        pc.angularVel = { spinDist(rng), spinDist(rng), spinDist(rng) };

        pieces_.push_back(pc);
    }
}

/** @brief 頂点列をGPUバッファへ転送する（容量不足時は作り直す） */
void MeshSliceEffect::UploadVertices(const std::vector<SliceVertex>& vertices) {
    const uint32_t count = static_cast<uint32_t>(vertices.size());
    if (count > vertexCapacity_) {
        vertexBuffer_ = dxCommon_->CreateBufferResource(sizeof(SliceVertex) * count);
        vertexBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData_));
        vertexCapacity_ = count;
    }
    std::copy(vertices.begin(), vertices.end(), vertexData_);

    vbv_.BufferLocation = vertexBuffer_->GetGPUVirtualAddress();
    vbv_.SizeInBytes = static_cast<UINT>(sizeof(SliceVertex) * count);
    vbv_.StrideInBytes = sizeof(SliceVertex);
}

// 更新・描画

/** @brief 切断演出のタイムラインと破片の動きを進め、定数バッファへ書き込む（毎フレーム呼ぶ） */
void MeshSliceEffect::Update(float dt, Camera* camera) {
    if (!active_ || camera == nullptr) {
        return;
    }

    // 止まる・ずれる・飛び散る の各時間と光り方は GraphicsTuning から読む
    const float holdTime = GraphicsTuning::Float("meshSlice.holdTime", kHoldTime);
    const float slideTime = GraphicsTuning::Positive("meshSlice.slideTime", kSlideTime);
    const float burstTime = GraphicsTuning::Positive("meshSlice.burstTime", kBurstTime);
    const float gravityScale = GraphicsTuning::Float("meshSlice.gravityScale", kGravityScale);
    const float burstGlow = GraphicsTuning::Float("meshSlice.burstGlow", kBurstGlow);
    const float initialGlow = GraphicsTuning::Float("meshSlice.initialGlow", kInitialGlow);
    const float slideGlow = GraphicsTuning::Float("meshSlice.slideGlowGain", kSlideGlowGain);
    const float tintGain = GraphicsTuning::Float("meshSlice.tintGain", kTintGain);
    const Vector4 glowColor = GraphicsTuning::Color("meshSlice.glowColor", { kGlowRed, kGlowGreen, 1.0f, 1.0f });
    // 経過時間から、ずれと飛び散りの進み具合（0〜1）を求める
    timer_ += dt;
    const float burstStart = holdTime + slideTime;
    const float endTime = burstStart + burstTime;

    const float slideT = std::clamp((timer_ - holdTime) / slideTime, 0.0f, 1.0f);
    const float burstT = std::clamp((timer_ - burstStart) / burstTime, 0.0f, 1.0f);

    // ずれは減速で止まり、消滅は加速で消える
    const float slideEase = 1.0f - (1.0f - slideT) * (1.0f - slideT) * (1.0f - slideT);
    const float alpha = 1.0f - burstT * burstT;

    // 飛び散り中は重力で落としながら回し、それまでは切断面に沿ってずらすだけ
    if (timer_ >= burstStart) {
        for (auto& pc : pieces_) {
            pc.velocity.y -= meshRadius_ * gravityScale * dt;
            pc.offset = pc.offset + pc.velocity * dt;
            pc.rotation = pc.rotation + pc.angularVel * dt;
        }
    } else {
        for (auto& pc : pieces_) {
            pc.offset = pc.slideDir * (slideEase * slideDist_);
        }
    }

    // 断面の発光はずれと同時に立ち上がり、飛散中は最大
    const float glowStrength = (timer_ >= burstStart) ? burstGlow : initialGlow + slideEase * slideGlow;
    const float tint = 1.0f + slideEase * tintGain * (1.0f - burstT);

    // 破片ごとの行列と色を定数バッファへ書く
    const Matrix4x4 viewProj = Multiply(camera->GetViewMatrix(), camera->GetProjectionMatrix());
    const Matrix4x4 modelMat = MakeAffineMatrix(scale_, Vector3 { }, worldPos_);

    for (size_t i = 0; i < pieces_.size(); ++i) {
        const Piece& pc = pieces_[i];
        const Vector3 negC = { -pc.centroid.x, -pc.centroid.y, -pc.centroid.z };

        // 重心を支点に回転させてからずらし、モデルのワールド変換を掛ける
        const Matrix4x4 local = Multiply(MakeTranslateMatrix(negC),
            MakeAffineMatrix({ 1.0f, 1.0f, 1.0f }, pc.rotation, pc.centroid + pc.offset));
        const Matrix4x4 world = Multiply(local, modelMat);

        auto* params = reinterpret_cast<PieceParams*>(pieceCBData_ + i * kCBSlotSize);
        params->wvp = Multiply(world, viewProj);
        params->world = world;
        params->color = { tint, tint, tint, alpha };
        params->glow = { glowColor.x, glowColor.y, glowColor.z, glowStrength * glowColor.w };
    }

    // 飛び散りが終わったら止める
    if (timer_ >= endTime) {
        active_ = false;
    }
}

/** @brief 全破片の描画コマンドを積む3D描画パス内で呼ぶ */
void MeshSliceEffect::Draw() {
    if (!active_ || pieces_.empty()) {
        return;
    }

    ID3D12GraphicsCommandList* cmd = dxCommon_->GetCommandList();

    pipelineHelper::SetTrianglePipeline(cmd, rootSignature_.Get(), pipelineState_.Get());
    cmd->IASetVertexBuffers(0, 1, &vbv_);

    SrvManager::GetInstance()->PreDraw();
    D3D12_GPU_DESCRIPTOR_HANDLE texHandle = TextureManager::GetInstance()->GetSrvHandleGPU(textureFilePath_);
    cmd->SetGraphicsRootDescriptorTable(pipelineHelper::effectMeshRootSlot::kTexture, texHandle);

    const D3D12_GPU_VIRTUAL_ADDRESS cbBase = pieceCB_->GetGPUVirtualAddress();
    for (size_t i = 0; i < pieces_.size(); ++i) {
        cmd->SetGraphicsRootConstantBufferView(pipelineHelper::effectMeshRootSlot::kConstants, cbBase + i * kCBSlotSize);
        cmd->DrawInstanced(pieces_[i].vertexCount, 1, pieces_[i].vertexOffset, 0);
    }
}

/** @brief 破片描画用のルートシグネチャ・PSOを生成する */
void MeshSliceEffect::CreatePipeline() {
    ID3D12Device* device = dxCommon_->GetDevice();

    D3D12_STATIC_SAMPLER_DESC sampler = pipelineHelper::MakePixelSampler(D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP);

    rootSignature_ = pipelineHelper::CreateConstantTextureSignature(device, D3D12_SHADER_VISIBILITY_ALL,
        sampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
    ENGINE_ASSERT(rootSignature_);

    D3D12_INPUT_ELEMENT_DESC inputElems[] = {
        pipelineHelper::kPositionElement,
        pipelineHelper::kTexcoordElement,
        pipelineHelper::kNormalElement,
        { "TEXCOORD", 1, DXGI_FORMAT_R32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = pipelineHelper::MakeEffectMeshPsoDesc(BlendMode::kAlpha, D3D12_DEPTH_WRITE_MASK_ALL);
    psoDesc.pRootSignature = rootSignature_.Get();
    psoDesc.InputLayout = { inputElems, _countof(inputElems) };

    // 通常アルファブレンド（破片本体はほぼ不透明で、最後だけフェードする）

    // 切断で生じる裏面や断面も見えるので両面描画

    // 深度書き込みあり（破片同士の前後関係をZバッファで解決する）

    pipelineState_ = pipelineHelper::CreateGraphicsPipeline(dxCommon_, psoDesc,
        L"Resources/shaders/meshslice/MeshSlice.VS.hlsl", L"Resources/shaders/meshslice/MeshSlice.PS.hlsl");
}

} // namespace engine::graphics
