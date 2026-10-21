/**
 * @file SkeletonOverlayRenderer.cpp
 * @brief スケルトンの関節とボーンのデバッグ表示（SkeletonOverlayRenderer）の実装
 */
#include "SkeletonOverlayRenderer.h"
#include "EngineAssert.h"
#include "PipelineHelper.h"
#include "GameConstants.h"
#ifdef USE_IMGUI

#include "Camera.h"
#include <cmath>

namespace engine::graphics {
namespace {

// ボーン描画の要素数
constexpr UINT kOverlayConstantCount = 20;
constexpr UINT kVerticesPerBone = 4;
constexpr UINT kIndicesPerBone = 6;
/** @brief 骨1本の四角形（4頂点）を2枚の三角形に分けるインデックスの並び */
constexpr uint16_t kQuadIndexOffsets[kIndicesPerBone] = { 0, 1, 2, 0, 2, 3 };
constexpr int kIndicesPerSphereCell = 6;

}

using namespace Microsoft::WRL;

static constexpr float kPi = GameConstants::kPi;

/** @brief ルートシグネチャ・PSO・ジョイント用球メッシュ・ボーン用動的頂点バッファを構築する */
void SkeletonOverlayRenderer::Initialize(DirectXCommon* dxCommon)
{
    dxCommon_ = dxCommon;
    ID3D12Device* device = dxCommon_->GetDevice();

    // ルートシグネチャ: 32ビット定数 20個 (16=WVP + 4=カラー)
    D3D12_ROOT_PARAMETER rp { };
    rp.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rp.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rp.Constants.ShaderRegister = 0;
    rp.Constants.RegisterSpace = 0;
    rp.Constants.Num32BitValues = kOverlayConstantCount;

    rootSignature_ = PipelineHelper::CreateRootSignature(device, &rp, 1,
        nullptr, 0, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
    ENGINE_ASSERT(rootSignature_);

    // シェーダー

    // 入力レイアウト
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0,
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    // 三角形 PSO (ボーン・球ともに使用)
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = PipelineHelper::MakeFullscreenPsoDesc();
    psoDesc.pRootSignature = rootSignature_.Get();
    psoDesc.InputLayout = { inputLayout, 1 };
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

    auto& b = psoDesc.BlendState.RenderTarget[0];
    b = PipelineHelper::MakeBlendDesc(BlendMode::Alpha);

    psoTri_ = PipelineHelper::CreateGraphicsPipeline(dxCommon_, psoDesc,
        L"Resources/shaders/diagnostics/SkeletonOverlayVS.hlsl", L"Resources/shaders/diagnostics/SkeletonOverlayPS.hlsl");

    BuildSphere();

    // ボーン用ビルボードクワッド（1ボーン = 4頂点 + 6インデックス）
    UINT vbSize = kMaxBones * kVerticesPerBone * static_cast<UINT>(sizeof(OverlayVertex));
    UINT ibSize = kMaxBones * kIndicesPerBone * static_cast<UINT>(sizeof(uint16_t));

    lineVB_ = PipelineHelper::CreateUploadBuffer(device, vbSize);
    lineIB_ = PipelineHelper::CreateUploadBuffer(device, ibSize);
    lineVB_->Map(0, nullptr, reinterpret_cast<void**>(&lineMapped_));
    lineIB_->Map(0, nullptr, reinterpret_cast<void**>(&lineIdxMapped_));

    lineVBV_ = { lineVB_->GetGPUVirtualAddress(), vbSize, sizeof(OverlayVertex) };
    lineIBV_ = { lineIB_->GetGPUVirtualAddress(), ibSize, DXGI_FORMAT_R16_UINT };
}

/** @brief ジョイント表示用の単位球メッシュ（UV球）の頂点・インデックスを生成しGPUバッファへ書き込む */
void SkeletonOverlayRenderer::BuildSphere()
{
    // 緯度・経度の格子で球の頂点を作る
    std::vector<OverlayVertex> verts;
    verts.reserve((kStacks + 1) * (kSlices + 1));
    for (int i = 0; i <= kStacks; ++i)
    {
        // 縦の角度は北極 0 から南極 π まで
        float phi = kPi * i / kStacks;
        for (int j = 0; j <= kSlices; ++j)
        {
            // 横の角度は 0 から 2π まで（最初と最後の列は同じ位置に重なる）
            float theta = 2.0f * kPi * j / kSlices;
            verts.push_back({ std::sin(phi) * std::cos(theta) * kSphereRadius,
                std::cos(phi) * kSphereRadius,
                std::sin(phi) * std::sin(theta) * kSphereRadius,
                1.0f });
        }
    }

    // 格子の1マスを三角形2枚で埋める
    std::vector<uint16_t> indices;
    indices.reserve(kStacks * kSlices * kIndicesPerSphereCell);
    for (int i = 0; i < kStacks; ++i)
    {
        for (int j = 0; j < kSlices; ++j)
        {
            // マスの4隅: a が左上、bv が右上、c が左下、d が右下
            auto a = static_cast<uint16_t>(i * (kSlices + 1) + j);
            auto bv = static_cast<uint16_t>(a + 1);
            auto c = static_cast<uint16_t>((i + 1) * (kSlices + 1) + j);
            auto d = static_cast<uint16_t>(c + 1);
            indices.insert(indices.end(), { a, c, bv, bv, c, d });
        }
    }
    sphereIndexCount_ = static_cast<uint32_t>(indices.size());

    // 球の頂点とインデックスは一度書いたら変えないので、アップロードヒープに置いたまま使う
    auto makeStatic = [&](size_t size) -> ComPtr<ID3D12Resource> {
        return PipelineHelper::CreateUploadBuffer(dxCommon_->GetDevice(), size);
    };

    sphereVB_ = makeStatic(verts.size() * sizeof(OverlayVertex));
    sphereIB_ = makeStatic(indices.size() * sizeof(uint16_t));

    // 頂点とインデックスを書き込み、描画用のビューを作る
    void* mapped;
    sphereVB_->Map(0, nullptr, &mapped);
    memcpy(mapped, verts.data(), verts.size() * sizeof(OverlayVertex));
    sphereVB_->Unmap(0, nullptr);

    sphereIB_->Map(0, nullptr, &mapped);
    memcpy(mapped, indices.data(), indices.size() * sizeof(uint16_t));
    sphereIB_->Unmap(0, nullptr);

    // ビューにはバッファの場所・全体の大きさ・1頂点の大きさ（インデックスは16bit）を入れる
    sphereVBV_ = { sphereVB_->GetGPUVirtualAddress(),
        static_cast<UINT>(verts.size() * sizeof(OverlayVertex)), sizeof(OverlayVertex) };
    sphereIBV_ = { sphereIB_->GetGPUVirtualAddress(),
        static_cast<UINT>(indices.size() * sizeof(uint16_t)), DXGI_FORMAT_R16_UINT };
}

/** @brief 深度テスト無効でスケルトンのジョイント（白球）とボーン（白ビルボード）を描画する */
void SkeletonOverlayRenderer::Draw(const Skeleton& skeleton, const Matrix4x4& worldMatrix, Camera* camera)
{
    if (!camera || skeleton.GetJointCount() == 0)
    {
        return;
    }

    ID3D12GraphicsCommandList* cmd = dxCommon_->GetCommandList();
    Matrix4x4 vp = Multiply(camera->GetViewMatrix(), camera->GetProjectionMatrix());

    // ジョイントのローカル座標 → ワールド座標
    auto toWorld = [&](const Matrix4x4& m) -> Vector3 {
        float lx = m.m[3][0], ly = m.m[3][1], lz = m.m[3][2];
        return {
            worldMatrix.m[0][0] * lx + worldMatrix.m[1][0] * ly + worldMatrix.m[2][0] * lz + worldMatrix.m[3][0],
            worldMatrix.m[0][1] * lx + worldMatrix.m[1][1] * ly + worldMatrix.m[2][1] * lz + worldMatrix.m[3][1],
            worldMatrix.m[0][2] * lx + worldMatrix.m[1][2] * ly + worldMatrix.m[2][2] * lz + worldMatrix.m[3][2]
        };
    };

    PipelineHelper::SetTrianglePipeline(cmd, rootSignature_.Get(), psoTri_.Get());

    const Vector3 camPos = camera->GetTranslate();

    // ボーン（ビルボードクワッド）
    {
        uint32_t vc = 0, ic = 0;

        for (size_t jointIndex = 0; jointIndex < skeleton.GetJointCount(); ++jointIndex)
        {
            const Joint& joint = skeleton.GetJoint(jointIndex);
            if (!joint.parent || vc + kVerticesPerBone > kMaxBones * kVerticesPerBone)
            {
                continue;
            }

            Vector3 p0 = toWorld(skeleton.GetJoint(*joint.parent).skeletonSpaceMatrix);
            Vector3 p1 = toWorld(joint.skeletonSpaceMatrix);

            // 骨の向き
            Vector3 boneDir = Normalize(Subtract(p1, p0));
            // 骨の中点からカメラへの向き
            Vector3 mid = { (p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f, (p0.z + p1.z) * 0.5f };
            Vector3 toCamera = Normalize(Subtract(camPos, mid));
            // 垂直方向（ビルボード法線）
            Vector3 perp = Normalize(Cross(boneDir, toCamera));
            float hw = kBoneHalfWidth;

            // 4頂点（p0側2, p1側2）
            auto base = static_cast<uint16_t>(vc);
            lineMapped_[vc++] = { p0.x + perp.x * hw, p0.y + perp.y * hw, p0.z + perp.z * hw, 1.f };
            lineMapped_[vc++] = { p0.x - perp.x * hw, p0.y - perp.y * hw, p0.z - perp.z * hw, 1.f };
            lineMapped_[vc++] = { p1.x - perp.x * hw, p1.y - perp.y * hw, p1.z - perp.z * hw, 1.f };
            lineMapped_[vc++] = { p1.x + perp.x * hw, p1.y + perp.y * hw, p1.z + perp.z * hw, 1.f };

            // 2三角形（クワッド）
            for (uint16_t offset : kQuadIndexOffsets)
            {
                lineIdxMapped_[ic++] = static_cast<uint16_t>(base + offset);
            }
        }

        if (ic > 0)
        {
            OverlayConstants cb { vp, { 1.0f, 1.0f, 1.0f, 1.0f } };
            cmd->IASetVertexBuffers(0, 1, &lineVBV_);
            cmd->IASetIndexBuffer(&lineIBV_);
            cmd->SetGraphicsRoot32BitConstants(PipelineHelper::OverlayRootSlot::kConstants, kOverlayConstantCount, &cb, 0);
            cmd->DrawIndexedInstanced(ic, 1, 0, 0, 0);
        }
    }

    // ジョイント（球）
    {
        cmd->IASetVertexBuffers(0, 1, &sphereVBV_);
        cmd->IASetIndexBuffer(&sphereIBV_);

        for (size_t jointIndex = 0; jointIndex < skeleton.GetJointCount(); ++jointIndex)
        {
            const Joint& joint = skeleton.GetJoint(jointIndex);
            Vector3 wp = toWorld(joint.skeletonSpaceMatrix);
            Matrix4x4 wvp = Multiply(MakeTranslateMatrix(wp), vp);
            OverlayConstants cb { wvp, { 1.0f, 1.0f, 1.0f, 1.0f } };
            cmd->SetGraphicsRoot32BitConstants(PipelineHelper::OverlayRootSlot::kConstants, kOverlayConstantCount, &cb, 0);
            cmd->DrawIndexedInstanced(sphereIndexCount_, 1, 0, 0, 0);
        }
    }
}

} // namespace engine::graphics

#endif // USE_IMGUI
