struct PSInput {
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD;
};

cbuffer GrayscaleParams : register(b0) {
    float amount;
    float3 pad;
};

Texture2D<float4> gTexture : register(t0);
SamplerState      gSampler : register(s0);

float4 main(PSInput input) : SV_TARGET
{
    float4 color = gTexture.Sample(gSampler, input.uv);
    // ITU-R BT.601 luminance weights
    float  gray  = dot(color.rgb, float3(0.299f, 0.587f, 0.114f));
    color.rgb    = lerp(color.rgb, float3(gray, gray, gray), amount);
    return color;
}
