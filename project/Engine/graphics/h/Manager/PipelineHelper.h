/**
 * @file PipelineHelper.h
 * @brief RootSignature / PSO 生成の定型コードをまとめたヘルパー
 * @note ModelCommon / SpriteCommon / SkinCommon がそれぞれ持っていた
 * 同一のボイラープレート（SRVレンジ・ルートパラメータ・静的サンプラー・
 * ブレンド設定・PSOひな形）を一本化したもの
 */
#pragma once
#include "BlendMode.h"
#include <array>
#include <d3d12.h>
#include <wrl.h>

namespace engine::graphics::PipelineHelper {

/// @brief 3D標準頂点レイアウト（POSITION / TEXCOORD / NORMAL / TANGENT）
inline constexpr D3D12_INPUT_ELEMENT_DESC kStandardInputLayout[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
};

/**
 * @brief t{baseRegister} を1つ参照するSRVディスクリプタレンジを作る
 * @param baseRegister シェーダーレジスタ番号（t0なら0）
 */
D3D12_DESCRIPTOR_RANGE MakeSrvRange(UINT baseRegister);

/**
 * @brief CBV1つのルートパラメータを作る
 * @param shaderRegister シェーダーレジスタ番号（b0なら0）
 * @param visibility     参照するシェーダーステージ
 */
D3D12_ROOT_PARAMETER MakeCbvParam(UINT shaderRegister, D3D12_SHADER_VISIBILITY visibility);

/**
 * @brief ディスクリプタレンジ1つのテーブル型ルートパラメータを作る
 * @param range      参照するレンジ（ルートシグネチャ生成が終わるまで生存させること）
 * @param visibility 参照するシェーダーステージ
 */
D3D12_ROOT_PARAMETER MakeSrvTableParam(const D3D12_DESCRIPTOR_RANGE* range, D3D12_SHADER_VISIBILITY visibility);

/**
 * @brief 標準の静的サンプラー2本（s0: リニアWRAP、s1: シャドウマップPCF比較）を作る
 * @param s0MaxLod s0のMaxLOD。0=常に最高解像度ミップのみ / D3D12_FLOAT32_MAX=全ミップ使用
 */
std::array<D3D12_STATIC_SAMPLER_DESC, 2> MakeDefaultSamplers(float s0MaxLod);

/**
 * @brief ルートシグネチャをシリアライズして生成する
 * @return 生成したルートシグネチャ（失敗時は nullptr）
 */
Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateRootSignature(
    ID3D12Device* device, const D3D12_ROOT_SIGNATURE_DESC& desc);

/**
 * @brief ブレンドモード別のレンダーターゲットブレンド設定を作る
 * @param mode 適用するブレンドモード
 */
D3D12_RENDER_TARGET_BLEND_DESC MakeBlendDesc(BlendMode mode);

/**
 * @brief 3D描画の標準PSO設定を埋めたひな形を作る
 * @note カリングなし・深度書き込みあり(LESS_EQUAL/D24S8)・SRGBレンダーターゲット1枚・三角形リスト。
 * VS / PS / RootSignature / InputLayout / BlendState は呼び出し側で設定する
 */
D3D12_GRAPHICS_PIPELINE_STATE_DESC MakeDefault3dPsoDesc();

} // namespace engine::graphics::PipelineHelper
