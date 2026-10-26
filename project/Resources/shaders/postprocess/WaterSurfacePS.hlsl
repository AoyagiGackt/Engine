/** @file WaterSurfacePS.hlsl
 * @brief 点列の水面と屈折、動く光の網目、泡を背景へ合成する */
static const uint kMaxSamples = 128;
static const float kZero = 0.0;
static const float kOne = 1.0;
static const float kHalf = 0.5;
static const float kTwo = 2.0;
static const float kEpsilon = 0.0001;
static const uint kNext = 1;
static const int kNeighborMin = -1;
static const int kNeighborMax = 1;
static const float2 kHashX = float2(127.1, 311.7);
static const float2 kHashY = float2(269.5, 183.3);
static const float kHashScale = 43758.5453;
static const float kCellMotion = 0.28;
static const float kCellPhase = 6.2831853;
static const float kFarDistance = 100.0;
static const float kNetInner = 0.025;
static const float kNetOuter = 0.13;
static const float kCrossScale = 1.37;
static const float2 kCrossShift = float2(8.3, 2.7);
static const float kCrossTime = -0.73;
static const float kNetBlend = 0.45;
static const float kBendFrequencyX = 3.1;
static const float kBendFrequencyY = 5.7;
static const float kBendCrossSpeed = 0.81;
static const float kSlopeGain = 0.04;
static const float kDepthBendBase = 0.35;
static const float kDepthBendGain = 0.65;
static const float kDeepDarkening = 0.22;
static const float3 kLightColor = float3(0.62, 0.94, 1.0);
static const float3 kFoamColor = float3(0.93, 0.99, 1.0);
static const float kFoamOffset = 0.45;
static const float kFoamOuter = 1.8;
static const float kFoamBreakBase = 0.72;
static const float kFoamBreakGain = 0.28;
static const float kFoamFrequency = 12.0;
static const float kFoamSpeed = 1.7;

Texture2D<float4> backgroundImage : register(t0);
SamplerState imageSampler : register(s0);
cbuffer WaterParameters : register(b0) {
    float4 bounds;
    float4 view;
    float4 animation;
    float4 optical;
    float4 deepColor;
    float4 surfaceColor;
    float4 captureUv;
    float4 detail;
    float4 heights[kMaxSamples];
};

/** @brief 全画面三角形から届く位置と正規化座標 */
struct PixelInput {
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD;
};

/** @brief 格子ごとに時間で揺れる種の位置を求める */
float2 CellSeed(float2 cell, float clockTime) {
    float2 noise = frac(sin(float2(dot(cell, kHashX), dot(cell, kHashY))) * kHashScale);
    return kHalf + kCellMotion * sin(kCellPhase * noise + clockTime);
}

/** @brief 最短距離と次の距離の差からボロノイの境界を抽出する */
float CausticNet(float2 coordinate, float clockTime) {
    float2 baseCell = floor(coordinate);
    float2 local = frac(coordinate);
    float nearest = kFarDistance;
    float second = kFarDistance;
    [unroll]
    for (int row = kNeighborMin; row <= kNeighborMax; ++row) {
        [unroll]
        for (int column = kNeighborMin; column <= kNeighborMax; ++column) {
            float2 neighbor = float2(column, row);
            float2 delta = neighbor + CellSeed(baseCell + neighbor, clockTime) - local;
            float distanceSquared = dot(delta, delta);
            second = min(second, max(nearest, distanceSquared));
            nearest = min(nearest, distanceSquared);
        }
    }
    return kOne - smoothstep(kNetInner, kNetOuter, sqrt(second) - sqrt(nearest));
}

/** @brief 表示範囲の外や未コピー領域を参照せず背景を読む */
float3 ReadBackground(float2 uv) {
    uint width, height;
    backgroundImage.GetDimensions(width, height);
    float2 halfTexel = kHalf / float2(width, height);
    float2 sampleUv = captureUv.xy + saturate(uv) * captureUv.zw;
    return backgroundImage.SampleLevel(imageSampler, clamp(sampleUv, captureUv.xy + halfTexel,
        captureUv.xy + captureUv.zw - halfTexel), kZero).rgb;
}

/** @brief 補間した水面の内側だけで景色を歪めて深さに応じて吸収する */
float4 main(PixelInput input) : SV_TARGET {
    float2 world = view.xy + (input.uv * kTwo - kOne) * float2(view.z, -view.w);
    float3 original = ReadBackground(input.uv);
    if (world.x < bounds.x || world.x > bounds.y || world.y < bounds.w) {
        return float4(original, kOne);
    }
    uint count = clamp((uint)animation.y, kNext + kNext, kMaxSamples);
    float indexValue = saturate((world.x - bounds.x) / max(bounds.y - bounds.x, kEpsilon)) * (count - kNext);
    uint indexA = min((uint)indexValue, count - kNext - kNext);
    float surface = bounds.z + lerp(heights[indexA].x, heights[indexA + kNext].x, indexValue - indexA);
    float depth = surface - world.y;
    if (depth < kZero) {
        return float4(original, kOne);
    }
    float normalizedDepth = saturate(depth / max(surface - bounds.w, kEpsilon));
    float slope = (heights[indexA + kNext].x - heights[indexA].x) * (count - kNext) / max(bounds.y - bounds.x, kEpsilon);
    float clockTime = animation.x * animation.w;
    float2 distortion = float2(sin(world.y * kBendFrequencyY + clockTime),
        cos(world.x * kBendFrequencyX - clockTime * kBendCrossSpeed));
    distortion.x += slope * kSlopeGain;
    float2 shiftedUv = input.uv + distortion * animation.z * (kDepthBendBase + kDepthBendGain * normalizedDepth);
    // 水面より上の画素を屈折で引き込まないよう深さで縦方向を制限する。
    shiftedUv.y = max(shiftedUv.y, input.uv.y - depth / (kTwo * view.w));
    float3 refracted = ReadBackground(shiftedUv);
    float tintDepth = saturate(depth / max(detail.x, kEpsilon));
    float4 tint = lerp(surfaceColor, deepColor, tintDepth);
    float3 color = lerp(refracted, tint.rgb, saturate(tint.a));
    color *= kOne - kDeepDarkening * normalizedDepth;
    float2 netCoordinate = world * optical.x;
    float netTime = animation.x * optical.y;
    float netA = CausticNet(netCoordinate, netTime);
    float netB = CausticNet(netCoordinate * kCrossScale + kCrossShift, netTime * kCrossTime);
    color += kLightColor * netA * lerp(kOne, netB, kNetBlend) * optical.z * (kOne - normalizedDepth * kHalf);
    float foamWidth = max(optical.w, kEpsilon);
    float foam = kOne - smoothstep(foamWidth * kHalf, foamWidth * kFoamOuter, abs(depth - foamWidth * kFoamOffset));
    foam *= detail.y * (kFoamBreakBase + kFoamBreakGain * sin(world.x * kFoamFrequency + animation.x * kFoamSpeed));
    color = lerp(color, kFoamColor, saturate(foam));
    // 水面と側面の境界を画素幅でなじませて波形の輪郭を保つ。
    float coverage = saturate(depth / max(fwidth(depth), kEpsilon));
    float bankDistance = min(world.x - bounds.x, bounds.y - world.x);
    coverage *= saturate(bankDistance / max(fwidth(world.x), kEpsilon));
    return float4(lerp(original, color, coverage), kOne);
}
