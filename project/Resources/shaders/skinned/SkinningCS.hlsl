// SkinnedVS.hlsl で頂点ごとに行っていたスキニング計算を事前にGPUで一括処理する
// 入力: 元の頂点バッファ (SRV, t0) + スキニングパレット (CBV, b0)
// 出力: 計算済み頂点バッファ (UAV, u0) → 描画パスで頂点バッファとして使用

/** @brief 元のスキンメッシュの頂点とボーン影響 */
struct InputVertex {
    float4 position;
    float2 texcoord;
    float3 normal;
    uint4 boneIndices;
    float4 boneWeights;
};

// ModelCommon の入力レイアウト (POSITION/TEXCOORD/NORMAL/TANGENT) と一致させる
/** @brief 標準描画の入力と一致する、接線付きの計算結果 */
struct OutputVertex {
    float4 position;
    float2 texcoord;
    float3 normal;
    /** @brief 法線から構築する直交接線 */
    float3 tangent;
};

#define MAX_JOINTS 128
cbuffer SkinningPalette : register(b0)
{
    float4x4 gMatrixPalette[MAX_JOINTS];
};

// インラインルートSRV では GetDimensions が未定義動作になるため、
// 頂点数はルート32bitコンスタント (b1) で受け取る
cbuffer DispatchParams : register(b1)
{
    uint gNumVertices;
};

StructuredBuffer<InputVertex> gInput : register(t0);
RWStructuredBuffer<OutputVertex> gOutput : register(u0);

/** @brief 各頂点をスキニングし、標準描画用の頂点として出力する */
[numthreads(64, 1, 1)]
void main(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= gNumVertices) {
        return;
    }

    InputVertex input = gInput[dtid.x];

    float totalWeight = input.boneWeights.x + input.boneWeights.y
                      + input.boneWeights.z + input.boneWeights.w;
    float4 weights = (totalWeight > 0.0f) ? input.boneWeights / totalWeight
                                          : float4(1, 0, 0, 0);

    float4x4 skinMatrix =
        weights.x * gMatrixPalette[input.boneIndices.x] +
        weights.y * gMatrixPalette[input.boneIndices.y] +
        weights.z * gMatrixPalette[input.boneIndices.z] +
        weights.w * gMatrixPalette[input.boneIndices.w];

    OutputVertex output;
    output.position = mul(input.position, skinMatrix);
    output.texcoord = input.texcoord;
    output.normal = normalize(mul(input.normal, (float3x3) skinMatrix));

    // 元の頂点に接線が無いため、法線と平行でない軸から接線を構築する
    const float kParallelThreshold = 0.999f;
    float3 referenceAxis = abs(output.normal.y) < kParallelThreshold ? float3(0, 1, 0) : float3(1, 0, 0);
    output.tangent = normalize(cross(referenceAxis, output.normal));
    gOutput[dtid.x] = output;
}
