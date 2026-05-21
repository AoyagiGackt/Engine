struct PSInput {
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD;
};

cbuffer HsvFilterParams : register(b0) {
    float hueShift;   // 度単位: -180 〜 +180
    float saturation; // 乗数: 0=グレー, 1=そのまま, 2=2倍
    float value;      // 乗数: 0=黒, 1=そのまま, 2=2倍
    float pad;
};

Texture2D<float4> gTexture : register(t0);
SamplerState      gSampler : register(s0);

float3 RGBtoHSV(float3 rgb)
{
    float cmax  = max(rgb.r, max(rgb.g, rgb.b));
    float cmin  = min(rgb.r, min(rgb.g, rgb.b));
    float delta = cmax - cmin;

    float h = 0.0;
    if (delta > 1e-5) {
        if (cmax == rgb.r)
            h = 60.0 * fmod((rgb.g - rgb.b) / delta, 6.0);
        else if (cmax == rgb.g)
            h = 60.0 * ((rgb.b - rgb.r) / delta + 2.0);
        else
            h = 60.0 * ((rgb.r - rgb.g) / delta + 4.0);
    }
    if (h < 0.0) h += 360.0;

    float s = (cmax > 1e-5) ? delta / cmax : 0.0;
    return float3(h, s, cmax);
}

float3 HSVtoRGB(float3 hsv)
{
    float h = hsv.x;
    float s = hsv.y;
    float v = hsv.z;

    float c = v * s;
    float x = c * (1.0 - abs(fmod(h / 60.0, 2.0) - 1.0));
    float m = v - c;

    float3 rgb;
    if      (h < 60.0)  rgb = float3(c, x, 0);
    else if (h < 120.0) rgb = float3(x, c, 0);
    else if (h < 180.0) rgb = float3(0, c, x);
    else if (h < 240.0) rgb = float3(0, x, c);
    else if (h < 300.0) rgb = float3(x, 0, c);
    else                rgb = float3(c, 0, x);

    return rgb + m;
}

float4 main(PSInput input) : SV_TARGET
{
    float4 color = gTexture.Sample(gSampler, input.uv);

    float3 hsv = RGBtoHSV(color.rgb);

    hsv.x  = fmod(hsv.x + hueShift + 360.0, 360.0);
    hsv.y  = saturate(hsv.y * saturation);
    hsv.z  = saturate(hsv.z * value);

    color.rgb = HSVtoRGB(hsv);
    return color;
}
