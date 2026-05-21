struct PSInput {
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD;
};

cbuffer FilterParams : register(b0) {
    float2 texelSize;   // テクセルサイズ（1/横幅, 1/縦幅）
    int    radius;      // カーネル半径（タップ数 = 2*radius+1）
    float  pad0;
    float4 kernel[5];  // CPU 側で事前計算した Gaussian 重み（最大 17 タップ）
    float2 direction;  // (1,0)=水平パス, (0,1)=垂直パス
    float2 pad1;
};

Texture2D<float4> gTexture : register(t0);
SamplerState      gSampler : register(s0);

float kernelAt(int i)
{
    float4 v = kernel[i >> 2];
    int    c = i & 3;
    if (c == 0) return v.x;
    if (c == 1) return v.y;
    if (c == 2) return v.z;
    return v.w;
}

float4 main(PSInput input) : SV_TARGET
{
    float4 result = 0.0;
    [loop]
    for (int k = -radius; k <= radius; ++k) {
        float2 offset = float2(k, k) * direction * texelSize;
        result += gTexture.Sample(gSampler, input.uv + offset) * kernelAt(k + radius);
    }
    return result;
}
