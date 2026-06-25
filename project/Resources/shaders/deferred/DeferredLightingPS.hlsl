// ディファードライティングパス
// G-Buffer から読み取り、ライティング結果をバックバッファへ出力

Texture2D<float4>     gAlbedo        : register(t0);
Texture2D<float4>     gNormal        : register(t1);
Texture2D<float4>     gMaterial      : register(t2);
Texture2D<float>      gDepth         : register(t3);
Texture2DArray<float> gCascadedShadow: register(t4);
SamplerState          gSampler       : register(s0);
SamplerComparisonState gShadowSampler: register(s1);

cbuffer LightingParams : register(b0)
{
    // 平行光源
    float4 gLightColor;
    float3 gLightDirection;
    float  gLightIntensity;
    float3 gAmbientColor;
    float  gAmbientIntensity;

    // カメラ
    float3 gCameraWorldPos;
    float  _pad0;

    // CSM
    float4x4 gCascadeVP[3];
    float    gCascadeSplits[3];
    float    gNumCascades;
    float3   _csmPad;

    // プロジェクション逆行列（深度 → ワールド座標復元）
    float4x4 gInvViewProjection;
    float2   gScreenSize;
    float2   _pad1;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD;
};

static const float kShadowMapSize = 2048.0f;
static const float kShadowBias    = 0.002f;
static const float kPI            = 3.14159265358979f;

float3 ReconstructWorldPos(float2 uv, float depth)
{
    float4 ndc = float4(uv.x * 2.0f - 1.0f, (1.0f - uv.y) * 2.0f - 1.0f, depth, 1.0f);
    float4 world = mul(ndc, gInvViewProjection);
    return world.xyz / world.w;
}

float GetShadowFactor(float3 worldPos)
{
    float camDist = length(worldPos - gCameraWorldPos);
    uint cascade = 0;
    if (gNumCascades >= 2.0f && camDist > gCascadeSplits[0]) cascade = 1;
    if (gNumCascades >= 3.0f && camDist > gCascadeSplits[1]) cascade = 2;

    float4 lsPos = mul(float4(worldPos, 1.0f), gCascadeVP[cascade]);
    float3 proj  = lsPos.xyz / lsPos.w;
    proj.x = proj.x *  0.5f + 0.5f;
    proj.y = proj.y * -0.5f + 0.5f;

    if (proj.x < 0.0f || proj.x > 1.0f || proj.y < 0.0f || proj.y > 1.0f || proj.z > 1.0f)
        return 1.0f;

    float compareDepth = proj.z - kShadowBias;
    float texelSize    = 1.0f / kShadowMapSize;
    float shadow       = 0.0f;
    [unroll]
    for (int x = -1; x <= 1; x++) {
        [unroll]
        for (int y = -1; y <= 1; y++) {
            shadow += gCascadedShadow.SampleCmpLevelZero(gShadowSampler,
                float3(proj.xy + float2(x, y) * texelSize, float(cascade)), compareDepth);
        }
    }
    return shadow / 9.0f;
}

// GGX NDF
float D_GGX(float NdotH, float roughness) {
    float a = roughness * roughness; float a2 = a * a;
    float d = NdotH * NdotH * (a2 - 1.0f) + 1.0f;
    return a2 / (kPI * d * d);
}
float G_Schlick(float NdotV, float roughness) {
    float r = roughness + 1.0f; float k = r * r / 8.0f;
    return NdotV / (NdotV * (1.0f - k) + k);
}
float3 F_Schlick(float cosTheta, float3 F0) {
    return F0 + (1.0f - F0) * pow(saturate(1.0f - cosTheta), 5.0f);
}

float4 main(VSOutput input) : SV_TARGET
{
    float4 albedoSample   = gAlbedo.Sample(gSampler, input.uv);
    float4 normalSample   = gNormal.Sample(gSampler, input.uv);
    float4 materialSample = gMaterial.Sample(gSampler, input.uv);
    float  depth          = gDepth.Sample(gSampler, input.uv).r;

    // 空ピクセル（normalが 0.5 かつ depth が 1.0）をスキップ
    if (depth >= 1.0f) discard;

    float3 albedo   = albedoSample.rgb;
    float3 N        = normalize(normalSample.xyz * 2.0f - 1.0f);
    float  metallic = materialSample.r;
    float  roughness= materialSample.g;

    float3 worldPos = ReconstructWorldPos(input.uv, depth);
    float3 V = normalize(gCameraWorldPos - worldPos);
    float3 L = normalize(-gLightDirection);

    float shadow = GetShadowFactor(worldPos);

    // Cook-Torrance BRDF
    float3 H    = normalize(V + L);
    float NdotV = max(dot(N, V), 0.001f);
    float NdotL = max(dot(N, L), 0.0f);
    float NdotH = max(dot(N, H), 0.0f);
    float VdotH = max(dot(V, H), 0.0f);

    float3 F0  = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);
    float  D   = D_GGX(NdotH, roughness);
    float  G   = G_Schlick(NdotV, roughness) * G_Schlick(NdotL, roughness);
    float3 F   = F_Schlick(VdotH, F0);

    float3 specular = (D * G * F) / max(4.0f * NdotV * NdotL, 0.001f);
    float3 kD       = (1.0f - F) * (1.0f - metallic);
    float3 diffuse  = kD * albedo / kPI;

    float3 litColor = (diffuse + specular) * gLightColor.rgb * gLightIntensity * NdotL * shadow;

    // アンビエント
    float3 F0amb = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);
    litColor += lerp(albedo, F0amb, metallic) * gAmbientColor * gAmbientIntensity;

    return float4(litColor, albedoSample.a);
}
