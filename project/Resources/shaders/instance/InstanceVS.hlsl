// GPU Instancing 頂点シェーダー
// SV_InstanceID を使い、インスタンスバッファからワールド行列を取得

#include "../object3d/Object3d.hlsli"

// カメラ VP 行列（全インスタンス共通）
cbuffer CameraVP : register(b0)
{
    float4x4 gViewProjection;
    float4x4 gLightVP;         // シャドウ用（CascadedShadowMap カスケード 0）
    float3   gCameraWorldPos;
    float    _pad;
};

// インスタンスごとのワールド行列バッファ (SRV, t0)
StructuredBuffer<float4x4> gInstanceWorlds : register(t0);

struct VertexShaderInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal   : NORMAL0;
    float3 tangent  : TANGENT0;
};

VertexShaderOutput main(VertexShaderInput input, uint instanceID : SV_InstanceID)
{
    float4x4 world               = gInstanceWorlds[instanceID];
    float4x4 worldInvTranspose   = transpose(/* 簡略化のため world そのものを使用（均一スケール前提） */ world);

    float4 worldPos = mul(input.position, world);

    VertexShaderOutput output;
    output.position      = mul(worldPos, gViewProjection);
    output.texcoord      = input.texcoord;
    output.worldPos      = worldPos.xyz;
    output.lightSpacePos = mul(worldPos, gLightVP);

    float3 N = normalize(mul(input.normal, (float3x3)world));
    float3 T = normalize(mul(input.tangent, (float3x3)world));
    T = normalize(T - N * dot(N, T));
    float3 B = cross(N, T);

    output.normal    = N;
    output.tangent   = T;
    output.bitangent = B;
    return output;
}
