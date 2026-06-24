#include "Object3d.hlsli"

Texture2D<float4> gTexture : register(t0);
Texture2D<float> gShadowMap : register(t1); // シャドウマップ（深度テクスチャ）
TextureCube<float4> gCubemap : register(t2); // キューブマップ（天球用）
Texture2D<float4> gNormalMap : register(t3); // 法線マップ
SamplerState gSampler : register(s0);
SamplerComparisonState gShadowSampler : register(s1); // 比較サンプラー（PCF用）

// =====================================================
// Material
// =====================================================
struct Material
{
    float4 color;
    int enableLighting;
    int shadingType; // 1:Lambert  2:HalfLambert  3:Lambert+Phong  4:HalfLambert+Phong
    int useCubemap; // 1:キューブマップサンプリング（天球用）
    int useTexture; // 0:テクスチャ色なし（白=1,1,1,1 として扱う）
    float4x4 uvTransform;
    float3 specularColor;
    float shininess;
    float3 cameraWorldPos;
    float envMapIntensity; // 環境マップ反射強度（0=なし, 1=フル反射）
    // ---- リムライト ----
    float3 rimColor;      // リムライトの色
    float  rimPower;      // 鋭さ（大きいほど細いリム、推奨 2〜6）
    float  rimIntensity;  // 強さ（0=無効、1=通常、2以上=強調）
    int    enableRim;     // 1=有効、0=無効
    int    useNormalMap;  // 1=法線マップ有効
    // ---- PBR (shadingType==5) ----
    float  metallic;      // メタリック度 (0=非金属, 1=金属)
    float  roughness;     // 粗さ (0=鏡面, 1=拡散)
    float3 _pbr_pad;      // 16 バイトアライン用パディング
};
ConstantBuffer<Material> gMaterial : register(b0);

// =====================================================
// DirectionalLight  (register b1)
// =====================================================
struct DirectionalLight
{
    float4 color;
    float3 direction;
    float intensity;
    float3 ambientColor;
    float ambientIntensity;
};
ConstantBuffer<DirectionalLight> gDirectionalLight : register(b1);

// =====================================================
// PointLight  (register b2)
// =====================================================
struct PointLight
{
    float3 position; // ワールド座標
    float radius; // 減衰距離
    float4 color; // 光の色（α未使用）
    float intensity; // 明るさ倍率
    float3 pad; // 16バイトアライン用
};

struct PointLightBuffer
{
    uint count; // 有効なライト数（0 なら処理なし）
    float3 pad;
    PointLight lights[8]; // 最大 Object3dCommon::kMaxPointLights 個
};
ConstantBuffer<PointLightBuffer> gPointLights : register(b2);

// =====================================================
// 出力
// =====================================================
struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

// =====================================================
// PCF シャドウ（3×3 カーネル）
// 戻り値: 0.0=完全に影, 1.0=完全に照らされている
// =====================================================
// ShadowManager::kShadowMapSize (C++) と値を一致させること
static const float kShadowMapSize = 2048.0f;
// 深度アクネを防ぐオフセット。シャドウの縞模様が出たら増やし、影の浮きが出たら減らす
static const float kShadowBias = 0.002f;

float GetShadowFactor(float4 lightSpacePos)
{
    // パースペクティブ除算
    float3 proj = lightSpacePos.xyz / lightSpacePos.w;

    // NDC → UV 変換（DirectX は Y 反転）
    proj.x = proj.x * 0.5f + 0.5f;
    proj.y = -proj.y * 0.5f + 0.5f;

    // シャドウマップ範囲外なら照らされている
    if (proj.x < 0.0f || proj.x > 1.0f ||
        proj.y < 0.0f || proj.y > 1.0f ||
        proj.z > 1.0f)
    {
        return 1.0f;
    }

    float compareDepth = proj.z - kShadowBias;
    float texelSize = 1.0f / kShadowMapSize;

    // PCF 3×3
    float shadow = 0.0f;
    [unroll]
    for (int x = -1; x <= 1; x++)
    {
        [unroll]
        for (int y = -1; y <= 1; y++)
        {
            shadow += gShadowMap.SampleCmpLevelZero(
                gShadowSampler,
                proj.xy + float2(x, y) * texelSize,
                compareDepth);
        }
    }
    return shadow / 9.0f;
}

// =====================================================
// PBR ヘルパー関数（Cook-Torrance BRDF）
// =====================================================
static const float kPI = 3.14159265358979f;

// GGX 法線分布関数 (NDF)
float D_GGX(float NdotH, float roughness)
{
    float a  = roughness * roughness;
    float a2 = a * a;
    float d  = NdotH * NdotH * (a2 - 1.0f) + 1.0f;
    return a2 / (kPI * d * d);
}

// Smith-Schlick-GGX 幾何減衰
float G_SchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0f;
    float k = (r * r) / 8.0f;
    return NdotV / (NdotV * (1.0f - k) + k);
}
float G_Smith(float NdotV, float NdotL, float roughness)
{
    return G_SchlickGGX(NdotV, roughness) * G_SchlickGGX(NdotL, roughness);
}

// Fresnel-Schlick 近似
float3 F_Schlick(float cosTheta, float3 F0)
{
    return F0 + (1.0f - F0) * pow(saturate(1.0f - cosTheta), 5.0f);
}

// Cook-Torrance BRDF ライティング（1灯分）
float3 PBR_DirectLight(float3 N, float3 V, float3 L,
                        float3 lightColor, float lightIntensity,
                        float3 albedo, float metallic, float roughness)
{
    float3 H    = normalize(V + L);
    float NdotV = max(dot(N, V), 0.001f);
    float NdotL = max(dot(N, L), 0.0f);
    float NdotH = max(dot(N, H), 0.0f);
    float VdotH = max(dot(V, H), 0.0f);

    // F0: 非金属は 0.04、金属はアルベドで補間
    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);

    float  D = D_GGX(NdotH, roughness);
    float  G = G_Smith(NdotV, NdotL, roughness);
    float3 F = F_Schlick(VdotH, F0);

    float3 specular = (D * G * F) / max(4.0f * NdotV * NdotL, 0.001f);

    // エネルギー保存: 拡散は kD = (1-F)*(1-metallic)
    float3 kD = (1.0f - F) * (1.0f - metallic);
    float3 diffuse = kD * albedo / kPI;

    return (diffuse + specular) * lightColor * lightIntensity * NdotL;
}

// =====================================================
// メイン
// =====================================================
PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    // UV 変換（テクスチャ・法線マップ共通）
    float4 transformedUV4 = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    float2 uv = transformedUV4.xy;

    float4 textureColor;
    if (gMaterial.useCubemap != 0)
    {
        // キューブマップ: カメラから頂点への方向ベクトルでサンプリング
        float3 dir = normalize(input.worldPos - gMaterial.cameraWorldPos);
        float3 hdr = gCubemap.Sample(gSampler, dir).rgb;
        // Reinhard トーンマッピング（BC6H UF16 のHDR値を [0,1] に変換）
        hdr = hdr / (hdr + 1.0f);
        textureColor = float4(hdr, 1.0f);
    }
    else if (gMaterial.useTexture != 0)
    {
        textureColor = gTexture.Sample(gSampler, uv);
    }
    else
    {
        textureColor = float4(1.0f, 1.0f, 1.0f, 1.0f);
    }
    float4 baseColor = gMaterial.color * textureColor;

    if (gMaterial.enableLighting != 0)
    {
        // 法線マップが有効な場合は TBN でワールド空間法線に変換
        float3 N;
        if (gMaterial.useNormalMap != 0)
        {
            float3 normalSample = gNormalMap.Sample(gSampler, uv).xyz * 2.0f - 1.0f;
            float3 T  = normalize(input.tangent);
            float3 B  = normalize(input.bitangent);
            float3 Nv = normalize(input.normal);
            N = normalize(normalSample.x * T + normalSample.y * B + normalSample.z * Nv);
        }
        else
        {
            N = normalize(input.normal);
        }
        float3 L = normalize(-gDirectionalLight.direction);
        float3 V = normalize(gMaterial.cameraWorldPos - input.worldPos);

        float shadowFactor = GetShadowFactor(input.lightSpacePos);
        float3 litColor;

        // =====================================================
        // shadingType 5 = PBR (Cook-Torrance BRDF)
        // =====================================================
        if (gMaterial.shadingType == 5)
        {
            float3 albedo = gMaterial.color.rgb * textureColor.rgb;

            // 平行光源
            litColor = PBR_DirectLight(N, V, L,
                gDirectionalLight.color.rgb, gDirectionalLight.intensity,
                albedo, gMaterial.metallic, gMaterial.roughness) * shadowFactor;

            // アンビエント（IBL 近似: 金属は反射率F0、非金属は定数）
            float3 F0     = lerp(float3(0.04f, 0.04f, 0.04f), albedo, gMaterial.metallic);
            float3 ambIBL = lerp(albedo, F0, gMaterial.metallic)
                          * gDirectionalLight.ambientColor * gDirectionalLight.ambientIntensity;
            litColor += ambIBL;

            // ポイントライト（PBR）
            for (uint i = 0; i < gPointLights.count; i++)
            {
                PointLight pl = gPointLights.lights[i];
                float3 toLight = pl.position - input.worldPos;
                float dist = length(toLight);
                if (dist >= pl.radius) continue;
                float3 L_pt = toLight / dist;
                float t = dist / pl.radius;
                float atten = (1.0f - t) * (1.0f - t);
                litColor += PBR_DirectLight(N, V, L_pt,
                    pl.color.rgb, pl.intensity * atten,
                    albedo, gMaterial.metallic, gMaterial.roughness);
            }
        }
        else
        {
            // =====================================================
            // 従来シェーディング（Lambert / HalfLambert / Blinn-Phong）
            // =====================================================
            float NdotL = dot(N, L);
            bool useHalfLambert = (gMaterial.shadingType == 2 || gMaterial.shadingType == 4);
            bool useSpecular    = (gMaterial.shadingType == 3 || gMaterial.shadingType == 4);

            float diffuse = useHalfLambert ? NdotL * 0.5f + 0.5f : max(NdotL, 0.0f);
            float3 diffuseColor =
                gMaterial.color.rgb * textureColor.rgb *
                gDirectionalLight.color.rgb * diffuse * gDirectionalLight.intensity;

            float3 H = normalize(V + L);
            float NdotH = max(dot(N, H), 0.0f);
            float spec = useSpecular
                ? pow(NdotH, max(gMaterial.shininess, 1.0f)) * step(0.0f, NdotL) : 0.0f;
            float3 specularColor = useSpecular
                ? gMaterial.specularColor * gDirectionalLight.color.rgb * spec * gDirectionalLight.intensity
                : float3(0.0f, 0.0f, 0.0f);

            float3 ambient =
                gMaterial.color.rgb * textureColor.rgb *
                gDirectionalLight.ambientColor * gDirectionalLight.ambientIntensity;

            if (gMaterial.envMapIntensity > 0.0f)
            {
                float3 R = reflect(-V, N);
                float3 envColor = gCubemap.Sample(gSampler, R).rgb;
                envColor = envColor / (envColor + 1.0f);
                float3 metalDiffuse = envColor * gMaterial.color.rgb;
                float3 metalAmbient = gMaterial.color.rgb * gDirectionalLight.ambientColor * gDirectionalLight.ambientIntensity;
                float3 normalLit = (diffuseColor + specularColor) * shadowFactor + ambient;
                float3 metalLit  = (metalDiffuse + specularColor) * shadowFactor + metalAmbient;
                litColor = lerp(normalLit, metalLit, gMaterial.envMapIntensity);
            }
            else
            {
                litColor = (diffuseColor + specularColor) * shadowFactor + ambient;
            }

            // ポイントライト（Blinn-Phong）
            for (uint i = 0; i < gPointLights.count; i++)
            {
                PointLight pl = gPointLights.lights[i];
                float3 toLight = pl.position - input.worldPos;
                float dist = length(toLight);
                if (dist >= pl.radius) continue;
                float3 L_pt = toLight / dist;
                float t = dist / pl.radius;
                float atten = (1.0f - t) * (1.0f - t);
                float NdotL_pt = dot(N, L_pt);
                float diffPt = useHalfLambert ? NdotL_pt * 0.5f + 0.5f : max(NdotL_pt, 0.0f);
                float3 H_pt = normalize(V + L_pt);
                float NdotH_pt = max(dot(N, H_pt), 0.0f);
                float specPt = useSpecular
                    ? pow(NdotH_pt, max(gMaterial.shininess, 1.0f)) * step(0.0f, NdotL_pt) : 0.0f;
                litColor +=
                    (gMaterial.color.rgb * textureColor.rgb * pl.color.rgb * diffPt
                   + gMaterial.specularColor * pl.color.rgb * specPt)
                    * pl.intensity * atten;
            }
        }

        // =====================================================
        // リムライト（両シェーディング共通）
        // =====================================================
        if (gMaterial.enableRim != 0)
        {
            float rim = 1.0f - saturate(dot(N, V));
            rim = pow(rim, max(gMaterial.rimPower, 0.001f));
            litColor += gMaterial.rimColor * rim * gMaterial.rimIntensity;
        }

        output.color.rgb = litColor;
        output.color.a = gMaterial.color.a * textureColor.a;
    }
    else
    {
        output.color = baseColor;
    }

    if (output.color.a == 0.0f)
    {
        discard;
    }

    return output;
}
