// NormalCapture パス PS
// Object3dVS の出力を受け取り、view-space normal (xyz) + linear depth (w) を書き込む

struct VertexShaderOutput
{
    float4 position      : SV_POSITION;
    float2 texcoord      : TEXCOORD0;
    float3 normal        : TEXCOORD3;
    float3 worldPos      : TEXCOORD1;
    float4 lightSpacePos : TEXCOORD2;
    float3 tangent       : TEXCOORD4;
    float3 bitangent     : TEXCOORD5;
};

cbuffer NormalCaptureCB : register(b1)
{
    float4x4 gView; // ワールド→ビュー変換
}

float4 main(VertexShaderOutput input) : SV_TARGET
{
    float3 viewNormal  = normalize(mul(float4(input.normal, 0.0f), gView).xyz);
    float  linearDepth = mul(float4(input.worldPos, 1.0f), gView).z;
    return float4(viewNormal, linearDepth);
}
