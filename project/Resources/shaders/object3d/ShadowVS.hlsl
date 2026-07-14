// ライトの視点から深度値だけを書き込む（ピクセルシェーダーなし）

struct TransformationMatrix
{
    float4x4 WVP;
    float4x4 World;
    float4x4 LightVP; // ライト空間のビュープロジェクション行列
};
ConstantBuffer<TransformationMatrix> gTransformationMatrix : register(b0);

struct VSInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
};

float4 main(VSInput input) : SV_POSITION
{
    return mul(input.position, gTransformationMatrix.LightVP);
}