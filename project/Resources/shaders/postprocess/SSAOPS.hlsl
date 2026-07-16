// SSAO PS: ノーマル+深度バッファからアンビエントオクルージョンを計算する

Texture2D<float4> gNormalDepth : register(t0);
SamplerState gSampler : register(s0);

cbuffer SSAOParams : register(b0)
{
    float4x4 gProjection;
    float4x4 gProjectionInverse;
    float gRadius;
    float gStrength;
    float gBias;
    float gTexW;
    float gTexH;
    int gNumSamples;
    float2 gPad;
    float4 gKernel[16];
}

struct VSOut
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

// UV + 線形ビュー深度 → ビュー空間位置を再構築
// プロジェクション逆行列を使ってスクリーン UV からビュー空間 xy を求める
float3 ReconstructViewPos(float2 uv, float viewZ)
{
    // UV → NDC [-1,1]
    float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    // NDC 平面上(z=0相当)でプロジェクション逆変換して xy スケールを取得
    float4 clipH = float4(ndc.x, ndc.y, 0.0f, 1.0f);
    float4 viewH = mul(clipH, gProjectionInverse);
    float2 xyUnit = viewH.xy / viewH.w; // Z=1 のときの視錐台 x,y
    // 実際の深度にスケール
    return float3(xyUnit * viewZ, viewZ);
}

float4 main(VSOut input) : SV_TARGET
{
    float2 uv = input.texcoord;
    float4 nd = gNormalDepth.Sample(gSampler, uv);
    float3 N = nd.xyz;
    float depth = nd.w;

    // 未書き込みピクセル（スカイ等）はスキップ → AO=1.0
    if (dot(N, N) < 0.01f)
        return float4(1.0f, 1.0f, 1.0f, 1.0f);
    N = normalize(N);

    float3 origin = ReconstructViewPos(uv, depth);

    // ランダムな接線（UV 座標から疑似乱数）
    float3 randomVec = normalize(float3(
        frac(uv.x * 19.19f + uv.y * 7.7f),
        frac(uv.x * 31.7f - uv.y * 13.1f),
        0.5f));
    float3 tangent = normalize(randomVec - N * dot(randomVec, N));
    float3 bitangent = cross(N, tangent);
    float3x3 TBN = float3x3(tangent, bitangent, N);

    float occlusion = 0.0f;
    [unroll(16)]
    for (int i = 0; i < gNumSamples; ++i)
    {
        float3 s = mul(gKernel[i].xyz, TBN);
        float3 sPos = origin + s * gRadius;

        // サンプル位置をスクリーン UV に投影
        float4 proj = mul(float4(sPos, 1.0f), gProjection);
        proj.xyz /= proj.w;
        float2 sUV = float2(proj.x * 0.5f + 0.5f, -proj.y * 0.5f + 0.5f);
        if (sUV.x < 0.0f || sUV.x > 1.0f || sUV.y < 0.0f || sUV.y > 1.0f)
            continue;

        float sDepth = gNormalDepth.Sample(gSampler, sUV).w;
        float rangeChk = smoothstep(0.0f, 1.0f, gRadius / max(abs(depth - sDepth), 0.001f));
        // sDepth < sPos.z: サーフェスがサンプル点よりカメラ寄り → サンプルがジオメトリ内部 → 閉塞
        occlusion += (sDepth <= sPos.z - gBias ? 1.0f : 0.0f) * rangeChk;
    }

    float ao = saturate(1.0f - (occlusion / float(gNumSamples)) * gStrength);
    return float4(ao, ao, ao, 1.0f);
}
