/**
 * @file PipelineHelper.h
 * @brief RootSignature / PSO 生成の定型コードをまとめたヘルパー
 * @note ModelCommon / SpriteCommon / SkinCommon がそれぞれ持っていた
 * 同一のボイラープレート（SRVレンジ・ルートパラメータ・静的サンプラー・
 * ブレンド設定・PSOひな形）を一本化したもの
 */
#pragma once
#include "BlendMode.h"
#include "EngineAssert.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <d3d12.h>
#include <wrl.h>
#include <dxcapi.h>

namespace engine { class DirectXCommon; }
namespace engine::graphics { class SrvManager; class Object3dCommon; class ShadowManager; }

namespace engine::graphics::pipelineHelper {

/**
 * @brief ポストエフェクト用のオフスクリーン描画先を消去する色（RGBA）
 * @note 描画先を作る時の最適化用の消去値と、毎フレームの消去で同じ値を使う必要があるので、ここ1か所で持つ
 */
inline constexpr float kOffscreenClearColor[4] = { 0.1f, 0.25f, 0.5f, 1.0f };

/** @brief 共通の描画設定から全ブレンドモードのPSOを生成し、診断名を付ける */
void CreateBlendPipelines(ID3D12Device* device, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc,
    const std::wstring& namePrefix,
    Microsoft::WRL::ComPtr<ID3D12PipelineState> (&pipelines)[static_cast<size_t>(BlendMode::kCount)]);

// パイプラインごとのルート引数の契約。シェーダーレジスタ番号とは区別する。
namespace modelRootSlot {
inline constexpr UINT kMaterial = 0;
inline constexpr UINT kTransform = 1;
inline constexpr UINT kTexture = 2;
inline constexpr UINT kDirectionalLight = 3;
inline constexpr UINT kShadow = 4;
inline constexpr UINT kEnvironment = 5;
inline constexpr UINT kPointLights = 6;
inline constexpr UINT kNormalMap = 7;
}

namespace skinnedRootSlot {
inline constexpr UINT kPalette = 6;
inline constexpr UINT kPointLights = 7;
inline constexpr UINT kNormalMap = 8;
}

namespace fullscreenRootSlot {
inline constexpr UINT kConstants = 0;
inline constexpr UINT kTexture = 1;
inline constexpr UINT kAuxiliaryTexture = 2;
}

namespace shadowRootSlot {
inline constexpr UINT kTransform = 0;
}

namespace outlineRootSlot {
inline constexpr UINT kParameters = 0;
inline constexpr UINT kTransform = 1;
}

namespace effectMeshRootSlot {
inline constexpr UINT kConstants = 0;
inline constexpr UINT kTexture = 1;
}

namespace vertexColorRootSlot {
inline constexpr UINT kScene = 0;
}

namespace spriteRootSlot {
inline constexpr UINT kMaterial = 0;
inline constexpr UINT kTransform = 1;
inline constexpr UINT kTexture = 2;
}

namespace particleRootSlot {
inline constexpr UINT kInstances = 0;
inline constexpr UINT kTexture = 1;
}

namespace normalPrepassRootSlot {
inline constexpr UINT kTransform = 0;
inline constexpr UINT kNormals = 1;
}

namespace bloomCompositeRootSlot {
inline constexpr UINT kScene = 0;
inline constexpr UINT kBloom = 1;
}

namespace singleTextureRootSlot {
inline constexpr UINT kTexture = 0;
}

namespace overlayRootSlot {
inline constexpr UINT kConstants = 0;
}


/** @brief 頂点の位置（float4）の入力要素 */
inline constexpr D3D12_INPUT_ELEMENT_DESC kPositionElement = { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
/** @brief 頂点のテクスチャ座標（float2）の入力要素 */
inline constexpr D3D12_INPUT_ELEMENT_DESC kTexcoordElement = { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
/** @brief 頂点の法線（float3）の入力要素 */
inline constexpr D3D12_INPUT_ELEMENT_DESC kNormalElement = { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
/** @brief 頂点の接線（float3）の入力要素 */
inline constexpr D3D12_INPUT_ELEMENT_DESC kTangentElement = { "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };

/** @brief 3D標準頂点レイアウト（POSITION / TEXCOORD / NORMAL / TANGENT） */
inline constexpr D3D12_INPUT_ELEMENT_DESC kStandardInputLayout[] = { kPositionElement, kTexcoordElement, kNormalElement, kTangentElement };

/** @brief 接線を持たないエフェクト用メッシュ・パーティクルの頂点レイアウト（POSITION / TEXCOORD / NORMAL） */
inline constexpr D3D12_INPUT_ELEMENT_DESC kEffectMeshInputLayout[] = { kPositionElement, kTexcoordElement, kNormalElement };

/**
 * @brief t{baseRegister} を1つ参照するSRVディスクリプタレンジを作る
 * @param baseRegister シェーダーレジスタ番号（t0なら0）
 */
D3D12_DESCRIPTOR_RANGE MakeSrvRange(UINT baseRegister);

/** @brief 通常モデルのルート引数の数（マテリアル・変換行列・テクスチャ・平行光源・影・キューブマップ・点光源・法線マップ） */
inline constexpr size_t kModelRootParameterCount = 8;
/** @brief 通常モデルが使うテクスチャのSRVレンジの数（t0〜t3） */
inline constexpr size_t kModelTextureRangeCount = 4;

/** @brief 指定した記述と初期状態で既定ヒープのリソースを作る */
Microsoft::WRL::ComPtr<ID3D12Resource> CreateDefaultResource(ID3D12Device* device,
    const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES state, const D3D12_CLEAR_VALUE* clearValue = nullptr);

/** @brief 深度書き込み用の単一ミップテクスチャを作る */
Microsoft::WRL::ComPtr<ID3D12Resource> CreateDepthTexture(ID3D12Device* device,
    UINT width, UINT height, UINT16 arraySize, DXGI_FORMAT format);

/** @brief 深度付きシーン描画先を設定して既定色で消去する */
void BindSceneTarget(DirectXCommon* dxCommon, const D3D12_CPU_DESCRIPTOR_HANDLE& rtv);

/** @brief 描画先と全画面パスの定数・入力画像を設定する */
void BindFullscreenPass(ID3D12GraphicsCommandList* cmd, ID3D12RootSignature* signature,
    ID3D12PipelineState* pipeline, const D3D12_CPU_DESCRIPTOR_HANDLE& rtv,
    const D3D12_VIEWPORT& viewport, const D3D12_RECT& scissor,
    D3D12_GPU_VIRTUAL_ADDRESS constants, const D3D12_GPU_DESCRIPTOR_HANDLE& texture);

/** @brief 指定レジスタを参照する全段用のルートUAVを作る */
D3D12_ROOT_PARAMETER MakeUavParam(UINT shaderRegister);

/** @brief 標準モデルが使うテクスチャ範囲の配列を作る */
std::array<D3D12_DESCRIPTOR_RANGE, kModelTextureRangeCount> MakeModelTextureRanges();

/**
 * @brief 通常モデル用のルート引数を既定のスロット順で作る
 * @param ranges t0〜t3 のSRVレンジ（ルート引数がこの配列を指すので、シグネチャを作り終えるまで残しておく）
 */
std::array<D3D12_ROOT_PARAMETER, kModelRootParameterCount> MakeModelRootParameters(
    const std::array<D3D12_DESCRIPTOR_RANGE, kModelTextureRangeCount>& ranges);

/** @brief 単一ミップ・単一サンプルの描画用2Dテクスチャ記述を作る */
D3D12_RESOURCE_DESC MakeRenderTextureDesc(UINT width, UINT height, DXGI_FORMAT format);

/** @brief 描画先テクスチャの形式（リソース本体・RTV・SRV で別の形式を使えるようにする） */
struct RenderTargetFormats {
    DXGI_FORMAT resource; ///< テクスチャ本体（TYPELESS にすると RTV と SRV を SRGB で共有できる）
    DXGI_FORMAT rtv; ///< 描き込む時の形式
    DXGI_FORMAT srv; ///< シェーダーから読む時の形式
};

/** @brief 作った描画先テクスチャとビューの置き場所 */
struct RenderTargetViews {
    Microsoft::WRL::ComPtr<ID3D12Resource>& texture; ///< テクスチャ本体
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>& rtvHeap; ///< RTV 1つぶんのヒープ
    D3D12_CPU_DESCRIPTOR_HANDLE& rtvHandle; ///< RTV のハンドル
    uint32_t& srvIndex; ///< SrvManager で確保した SRV の番号
};

/**
 * @brief 描画先テクスチャを作り、RTV と SRV を1つずつ作る
 * @param device デバイス
 * @param srvManager SRV を確保する SrvManager
 * @param width 幅（ピクセル）
 * @param height 高さ（ピクセル）
 * @param formats 本体・RTV・SRV の形式
 * @param clearColor 最適化用の消去値（毎フレームの消去と同じ色にする）
 * @param out 作ったテクスチャとビューの書き込み先
 * @param initialState 作った直後のリソースの状態（既定は描き込める状態）
 */
void CreateRenderTarget(ID3D12Device* device, SrvManager* srvManager, UINT width, UINT height,
    const RenderTargetFormats& formats, const float (&clearColor)[4], const RenderTargetViews& out,
    D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_RENDER_TARGET);

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

/** @brief ルート引数・サンプラーの順番とフラグを保ってシグネチャを作る */
Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateRootSignature(
    ID3D12Device* device, const D3D12_ROOT_PARAMETER* parameters, UINT parameterCount,
    const D3D12_STATIC_SAMPLER_DESC* samplers, UINT samplerCount,
    D3D12_ROOT_SIGNATURE_FLAGS flags);

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

/** @brief 深度なし・カリングなしの全画面パス用PSOの既定値を作る */
D3D12_GRAPHICS_PIPELINE_STATE_DESC MakeFullscreenPsoDesc();

/** @brief 深度比較を使う演出メッシュのブレンドと深度書き込みを設定する */
D3D12_GRAPHICS_PIPELINE_STATE_DESC MakeEffectMeshPsoDesc(
    BlendMode blendMode, D3D12_DEPTH_WRITE_MASK depthWriteMask);

/** @brief 頂点色を加算合成する刃・軌跡用の描画パイプラインを作る */
void CreateVertexColorEffectPipeline(DirectXCommon* dxCommon,
    Microsoft::WRL::ComPtr<ID3D12RootSignature>& rootSignature,
    Microsoft::WRL::ComPtr<ID3D12PipelineState>& pipelineState);

/**
 * @brief CreateVertexColorEffectPipeline で作ったパイプラインで、頂点色の三角形の並びを描く
 * @param sceneConstants ビュー・プロジェクション行列の定数バッファ（b0）
 */
void DrawVertexColorEffect(ID3D12GraphicsCommandList* cmd, ID3D12RootSignature* rootSignature,
    ID3D12PipelineState* pipelineState, const D3D12_VERTEX_BUFFER_VIEW& vertexBuffer,
    D3D12_GPU_VIRTUAL_ADDRESS sceneConstants, UINT vertexCount);

/** @brief シェーダーのパスと確定済み設定からグラフィックスPSOを生成する */
Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateGraphicsPipeline(
    DirectXCommon* dxCommon, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc,
    const wchar_t* vertexShaderPath, const wchar_t* pixelShaderPath);

/** @brief コンピュートシェーダーが書き込むバッファ（DEFAULT ヒープ・UAV 可）を COMMON 状態で確保する */
Microsoft::WRL::ComPtr<ID3D12Resource> CreateUnorderedAccessBuffer(ID3D12Device* device, UINT64 size);

/** @brief 指定サイズのアップロードバッファをGENERIC_READ状態で確保する */
Microsoft::WRL::ComPtr<ID3D12Resource> CreateUploadBuffer(ID3D12Device* device, UINT64 size);

/**
 * @brief 配列の中身をちょうど入る大きさのアップロード用バッファへ書き込んで返す（頂点やインデックスの初期化用）
 * @param device デバイス
 * @param elements 書き込む要素の並び
 */
template <class Element>
Microsoft::WRL::ComPtr<ID3D12Resource> CreateUploadBufferFrom(ID3D12Device* device, const std::vector<Element>& elements) {
    auto resource = CreateUploadBuffer(device, sizeof(Element) * elements.size());
    Element* mapped = nullptr;
    const HRESULT hr = resource->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
    ENGINE_ASSERT(SUCCEEDED(hr));
    std::copy(elements.begin(), elements.end(), mapped);
    resource->Unmap(0, nullptr);
    return resource;
}

/**
 * @brief 単一要素・単一ミップの行優先バッファ記述を作る（UAV などのフラグは呼び出し側で足す）
 * @param size バッファの大きさ（バイト）
 */
D3D12_RESOURCE_DESC MakeBufferDesc(UINT64 size);

/** @brief マテリアルが確保済みなら指定したフィールドだけを書き換える */
template <typename Material, typename Value>
void SetMaterialValue(Material* material, Value Material::* const& member, const Value& value) {
    if (material) {
        material->*member = value;
    }
}

/** @brief 指定した設定を変更せずにグラフィックスPSOを生成する */
Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateGraphicsPipeline(
    ID3D12Device* device, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc);

/** @brief 指定したシェーダーをコンパイルし、コンピュートPSOを生成する */
Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateComputePipeline(
    DirectXCommon* dxCommon, ID3D12RootSignature* rootSignature, const wchar_t* shaderPath);

/** @brief フィルターと各軸のアドレス方式を指定してピクセル用サンプラーを作る */
D3D12_STATIC_SAMPLER_DESC MakePixelSampler(D3D12_FILTER filter,
    D3D12_TEXTURE_ADDRESS_MODE addressU, D3D12_TEXTURE_ADDRESS_MODE addressV,
    D3D12_TEXTURE_ADDRESS_MODE addressW);

/** @brief 全画面パスでよく使う、リニア補間・端を伸ばす（クランプ）ピクセル用サンプラー s0 を作る */
D3D12_STATIC_SAMPLER_DESC MakeLinearClampSampler();

/** @brief 三角形描画のルートシグネチャ・PSO・トポロジーを設定する */
void SetTrianglePipeline(ID3D12GraphicsCommandList* cmd,
    ID3D12RootSignature* rootSignature, ID3D12PipelineState* pipelineState);

/** @brief SV_VertexIDで生成する全画面三角形を1枚描く */
void DrawFullscreenTriangle(ID3D12GraphicsCommandList* cmd);

/** @brief 指定領域にビューポートとシザーを揃える */
void SetViewportAndScissor(ID3D12GraphicsCommandList* cmd,
    const D3D12_VIEWPORT& viewport, const D3D12_RECT& scissor);

/** @brief 今のバックバッファを深度なしの描画先にし、ビューポートとシザーもバックバッファに合わせる */
void BindBackBufferTarget(DirectXCommon* dxCommon, ID3D12GraphicsCommandList* cmd);

/** @brief ビューポートとシザーを内部解像度（WinApp::kClientWidth × kClientHeight）全体に合わせる */
void SetClientViewportAndScissor(ID3D12GraphicsCommandList* cmd);

/**
 * @brief バックバッファと同じ形式・大きさの、画面を写し取るためのテクスチャと SRV を作る
 * @param outSrvIndex 確保した SRV の番号を入れる先
 * @return シェーダーから参照できる状態のテクスチャ（CaptureRenderTarget の写し先に使う）
 */
Microsoft::WRL::ComPtr<ID3D12Resource> CreateBackBufferCopyTexture(DirectXCommon* dxCommon,
    SrvManager* srvManager, uint32_t& outSrvIndex);

/** @brief 描画済みテクスチャをコピーし、コピー前の描画・参照状態へ戻す */
void CaptureRenderTarget(ID3D12GraphicsCommandList* cmd,
    ID3D12Resource* source, ID3D12Resource* destination);

/** @brief 標準モデルのライトと影を描画コマンドへ再設定する */
void BindCommonLighting(ID3D12GraphicsCommandList* cmd, Object3dCommon* objectCommon, ShadowManager* shadowManager);

/** @brief マップした定数バッファと演出用パイプラインを順に解放する */
template <typename Constants>
void ReleaseEffectResources(Microsoft::WRL::ComPtr<ID3D12Resource>& resource, Constants*& data,
    Microsoft::WRL::ComPtr<ID3D12PipelineState>& pipeline, Microsoft::WRL::ComPtr<ID3D12RootSignature>& signature) {
    // Unmapを終えてからバッファと描画状態への参照を手放す
    if (data) {
        resource->Unmap(0, nullptr);
        data = nullptr;
    }
    resource.Reset();
    pipeline.Reset();
    signature.Reset();
}

/** @brief CBV b0と画像t0を使う全画面用ルートシグネチャを作る */
Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateCapturedFullscreenSignature(ID3D12Device* device);

/** @brief b0の定数とt0のテクスチャを使うシグネチャを、可視範囲とサンプラーを保って作る */
Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateConstantTextureSignature(ID3D12Device* device,
    D3D12_SHADER_VISIBILITY constantVisibility, const D3D12_STATIC_SAMPLER_DESC& sampler,
    D3D12_ROOT_SIGNATURE_FLAGS flags);

/** @brief 全画面シェーダーとブレンド方式から描画パイプラインを作る */
Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateFullscreenPipeline(DirectXCommon* dxCommon,
    ID3D12RootSignature* signature, const wchar_t* pixelShaderPath, BlendMode blendMode);

/** @brief 現在の画面をコピーし、指定した全画面エフェクトで描き戻す */
void CaptureAndDrawFullscreen(bool active, DirectXCommon* dxCommon, SrvManager* srvManager,
    ID3D12Resource* captureTexture, ID3D12RootSignature* signature, ID3D12PipelineState* pipeline,
    ID3D12Resource* constantsResource, uint32_t textureSrvIndex);

/** @brief キャプチャ画像を深度なしでバックバッファ全体へ合成する */
void DrawCapturedFullscreen(engine::DirectXCommon* dxCommon, SrvManager* srvManager,
    ID3D12RootSignature* signature, ID3D12PipelineState* pipeline,
    D3D12_GPU_VIRTUAL_ADDRESS constants, uint32_t textureSrvIndex);

} // namespace engine::graphics::PipelineHelper
