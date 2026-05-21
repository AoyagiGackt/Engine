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
SamplerState      gSampler : register(s0);

float Luminance(float3 c) { return dot(c, float3(0.299, 0.587, 0.114)); }

float4 main(PSInput input) : SV_TARGET
{
    float2 ts = texelSize;
    float2 uv = input.uv;

    float lum[3][3];
    [unroll] for (int dy = -1; dy <= 1; dy++) {
        [unroll] for (int dx = -1; dx <= 1; dx++) {
            lum[dy + 1][dx + 1] = Luminance(gTexture.Sample(gSampler, uv + float2(dx, dy) * ts).rgb);
        }
    }

    // Prewitt 横方向勾配 Gx: [[-1,0,1],[-1,0,1],[-1,0,1]]
    float gx = (-lum[0][0] + lum[0][2]) + (-lum[1][0] + lum[1][2]) + (-lum[2][0] + lum[2][2]);
    // Prewitt 縦方向勾配 Gy: [[-1,-1,-1],[0,0,0],[1,1,1]]
    float gy = (-lum[0][0] - lum[0][1] - lum[0][2]) + (lum[2][0] + lum[2][1] + lum[2][2]);

    float edge = saturate(sqrt(gx * gx + gy * gy) * edgeStrength - threshold);
    float4 scene = gTexture.Sample(gSampler, uv);
    return lerp(scene, float4(outlineColor.rgb, scene.a), edge);
}
