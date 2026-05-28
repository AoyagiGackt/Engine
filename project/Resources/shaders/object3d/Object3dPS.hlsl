#include "Object3d.hlsli"

Texture2D<float4> gTexture : register(t0);
Texture2D<float> gShadowMap : register(t1); // シャドウマップ（深度テクスチャ）
TextureCube<float4> gCubemap : register(t2); // キューブマップ（天球用）
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
static const float kShadowMapSize = 2048.0f;
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
// メイン
// =====================================================
PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

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
        float4 transformedUV = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
        textureColor = gTexture.Sample(gSampler, transformedUV.xy);
    }
    else
    {
        textureColor = float4(1.0f, 1.0f, 1.0f, 1.0f);
    }
    float4 baseColor = gMaterial.color * textureColor;

    if (gMaterial.enableLighting != 0)
    {
        float3 N = normalize(input.normal);
        float3 L = normalize(-gDirectionalLight.direction);
        float3 V = normalize(gMaterial.cameraWorldPos - input.worldPos);

        // ----- 拡散反射（Diffuse）-----
        // LightingMode: 1=Lambert, 2=HalfLambert, 3=Lambert+Phong, 4=HalfLambert+Phong
        float NdotL = dot(N, L);
        bool useHalfLambert = (gMaterial.shadingType == 2 || gMaterial.shadingType == 4);
        bool useSpecular    = (gMaterial.shadingType == 3 || gMaterial.shadingType == 4);

        float diffuse;
        if (useHalfLambert)
        {
            diffuse = NdotL * 0.5f + 0.5f;
        }
        else
        {
            diffuse = max(NdotL, 0.0f); // Lambert（mode 1, 3, またはデフォルト）
        }
        float3 diffuseColor =
            gMaterial.color.rgb * textureColor.rgb *
            gDirectionalLight.color.rgb * diffuse * gDirectionalLight.intensity;

        // ----- 鏡面反射（Specular / Blinn-Phong）Phong モード時のみ -----
        float3 H = normalize(V + L);
        float NdotH = max(dot(N, H), 0.0f);
        float spec = useSpecular
            ? pow(NdotH, max(gMaterial.shininess, 1.0f)) * step(0.0f, NdotL)
            : 0.0f;
        float3 specularColor = useSpecular
            ? gMaterial.specularColor * gDirectionalLight.color.rgb * spec * gDirectionalLight.intensity
            : float3(0.0f, 0.0f, 0.0f);

        // ----- 環境光（Ambient）-----
        float3 ambient =
            gMaterial.color.rgb * textureColor.rgb *
            gDirectionalLight.ambientColor * gDirectionalLight.ambientIntensity;

        // ----- シャドウ係数（PCF）-----
        float shadowFactor = GetShadowFactor(input.lightSpacePos);

        // ----- 環境マップ反射（金属感）-----
        float3 litColor;
        if (gMaterial.envMapIntensity > 0.0f)
        {
            float3 R = reflect(-V, N);
            float3 envColor = gCubemap.Sample(gSampler, R).rgb;
            envColor = envColor / (envColor + 1.0f); // Reinhard トーンマッピング

            float3 metalDiffuse = envColor * gMaterial.color.rgb;
            float3 metalAmbient = gMaterial.color.rgb * gDirectionalLight.ambientColor * gDirectionalLight.ambientIntensity;

            float3 normalLit = (diffuseColor + specularColor) * shadowFactor + ambient;
            float3 metalLit = (metalDiffuse + specularColor) * shadowFactor + metalAmbient;

            litColor = lerp(normalLit, metalLit, gMaterial.envMapIntensity);
        }
        else
        {
            litColor = (diffuseColor + specularColor) * shadowFactor + ambient;
        }

        // =====================================================
        // ポイントライト（距離減衰 + Blinn-Phong）
        // count == 0 のときはループしないのでコスト無し
        // =====================================================
        float3 pointContrib = float3(0.0f, 0.0f, 0.0f);
        for (uint i = 0; i < gPointLights.count; i++)
        {
            PointLight pl = gPointLights.lights[i];
            float3 toLight = pl.position - input.worldPos;
            float dist = length(toLight);
            if (dist >= pl.radius)
            {
                continue;
            }

            float3 L_pt = toLight / dist;

            // 二乗減衰（radius の端でゼロになる smooth fall-off）
            float t = dist / pl.radius;
            float atten = (1.0f - t) * (1.0f - t);

            // 拡散反射
            float NdotL_pt = dot(N, L_pt);
            float diffPt;
            if (useHalfLambert)
            {
                diffPt = NdotL_pt * 0.5f + 0.5f;
            }
            else
            {
                diffPt = max(NdotL_pt, 0.0f);
            }

            // 鏡面反射（Blinn-Phong）Phong モード時のみ
            float3 H_pt = normalize(V + L_pt);
            float NdotH_pt = max(dot(N, H_pt), 0.0f);
            float specPt = useSpecular
                ? pow(NdotH_pt, max(gMaterial.shininess, 1.0f)) * step(0.0f, NdotL_pt)
                : 0.0f;

            pointContrib +=
                (gMaterial.color.rgb * textureColor.rgb * pl.color.rgb * diffPt
               + gMaterial.specularColor * pl.color.rgb * specPt)
                * pl.intensity * atten;
        }
        
        litColor += pointContrib;

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
