#include "Object3d.hlsli"

struct TransformationMatrix
{
    float4x4 WVP;
    float4x4 World;
    float4x4 WorldInverseTranspose; // 非均一スケール対応法線変換用
    float4x4 LightVP; // ライト空間のビュープロジェクション行列
};
ConstantBuffer<TransformationMatrix> gTransformationMatrix : register(b0);

struct VertexShaderInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
    float3 tangent : TANGENT0;
};

VertexShaderOutput main(VertexShaderInput input)
{
    VertexShaderOutput output;
    output.position = mul(input.position, gTransformationMatrix.WVP);
    output.texcoord = input.texcoord;
    output.worldPos = mul(input.position, gTransformationMatrix.World).xyz;
    output.lightSpacePos = mul(input.position, gTransformationMatrix.LightVP);

    float3 N = normalize(mul(input.normal, (float3x3) gTransformationMatrix.WorldInverseTranspose));
    float3 T = normalize(mul(input.tangent, (float3x3) gTransformationMatrix.World));
    T = normalize(T - N * dot(N, T)); // Gram-Schmidt 再直交化
    float3 B = cross(N, T);

    output.normal = N;
    output.tangent = T;
    output.bitangent = B;
    return output;
}
