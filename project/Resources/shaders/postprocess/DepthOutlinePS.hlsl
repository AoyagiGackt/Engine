struct PSInput {
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD;
};

cbuffer OutlineParams : register(b0) {
    float2 texelSize;
    float  threshold;
    float  edgeStrength;
    float4 outlineColor;
    float  depthScale;
    float3 pad;
};

Texture2D<float4> gTexture : register(t0);
Texture2D<float>  gDepth   : register(t1);
SamplerState      gSampler : register(s0);

float4 main(PSInput input) : SV_TARGET
{
    float2 ts = texelSize;
    float2 uv = input.uv;

    float d[3][3];
    [unroll] for (int dy = -1; dy <= 1; dy++) {
        [unroll] for (int dx = -1; dx <= 1; dx++) {
            d[dy + 1][dx + 1] = gDepth.Sample(gSampler, uv + float2(dx, dy) * ts).r;
        }
    }

    // 深度値に Prewitt カーネルを適用してエッジ勾配を計算
    float gx = (-d[0][0] + d[0][2]) + (-d[1][0] + d[1][2]) + (-d[2][0] + d[2][2]);
    float gy = (-d[0][0] - d[0][1] - d[0][2]) + (d[2][0] + d[2][1] + d[2][2]);

    float edge = saturate(sqrt(gx * gx + gy * gy) * edgeStrength * depthScale - threshold);
    float4 scene = gTexture.Sample(gSampler, uv);
    return lerp(scene, float4(outlineColor.rgb, scene.a), edge);
}
