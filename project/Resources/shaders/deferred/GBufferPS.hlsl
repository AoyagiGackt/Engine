// G-Buffer 書き出しパス
// MRT 出力: Albedo(RGBA8), Normal(RGBA16F), Material(RGBA8)

Texture2D<float4> gTexture   : register(t0);
Texture2D<float4> gNormalMap : register(t1);
SamplerState      gSampler   : register(s0);

cbuffer Material : register(b0)
{
    float4    gColor;
    int       gEnableLighting;
    int       gShadingType;
    int       gUseCubemap;
    int       gUseTexture;
    float4x4  gUvTransform;
    float3    gSpecularColor;
    float     gShininess;
    float3    gCameraWorldPos;
    float     gEnvMapIntensity;
    float3    gRimColor;
    float     gRimPower;
    float     gRimIntensity;
    int       gEnableRim;
    int       gUseNormalMap;
    float     gMetallic;
    float     gRoughness;
    float3    _pbr_pad;
};

struct VSOutput
{
    float4 position  : SV_POSITION;
    float2 texcoord  : TEXCOORD0;
    float3 normal    : TEXCOORD3;
    float3 worldPos  : TEXCOORD1;
    float4 unused    : TEXCOORD2;
    float3 tangent   : TEXCOORD4;
    float3 bitangent : TEXCOORD5;
};

struct GBufferOutput
{
    float4 albedo   : SV_TARGET0; // RGB = albedo, A = alpha
    float4 normal   : SV_TARGET1; // RGB = world normal (encoded), A = unused
    float4 material : SV_TARGET2; // R = metallic, G = roughness, B = shadingType/8, A = flags
};

GBufferOutput main(VSOutput input)
{
    GBufferOutput output;

    // Albedo
    float2 uv = mul(float4(input.texcoord, 0, 1), gUvTransform).xy;
    float4 texColor = (gUseTexture != 0) ? gTexture.Sample(gSampler, uv) : float4(1,1,1,1);
    output.albedo = gColor * texColor;

    if (output.albedo.a < 0.01f) discard;

    // Normal (TBN または頂点法線)
    float3 N;
    if (gUseNormalMap != 0) {
        float3 ns = gNormalMap.Sample(gSampler, uv).xyz * 2.0f - 1.0f;
        float3 T  = normalize(input.tangent);
        float3 B  = normalize(input.bitangent);
        float3 Nv = normalize(input.normal);
        N = normalize(ns.x * T + ns.y * B + ns.z * Nv);
    } else {
        N = normalize(input.normal);
    }
    // [-1, 1] → [0, 1] に格納
    output.normal = float4(N * 0.5f + 0.5f, 1.0f);

    // Material パラメータ
    output.material = float4(gMetallic, gRoughness, float(gShadingType) / 8.0f, float(gEnableRim));

    return output;
}
