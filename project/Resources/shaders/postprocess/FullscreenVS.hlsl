struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD;
};

VSOutput main(uint vertexID : SV_VertexID)
{
    VSOutput output;
    // vertexID 0,1,2 で画面全体を覆う1枚の大きな三角形を生成するビットトリック
    // ID=0 → (0,0), ID=1 → (2,0), ID=2 → (0,2) → クリップ空間 [-1,3] × [-3,1] をカバー
    float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
    output.uv = uv;
    output.position = float4(uv.x * 2.0f - 1.0f, -(uv.y * 2.0f - 1.0f), 0.0f, 1.0f);
    return output;
}
