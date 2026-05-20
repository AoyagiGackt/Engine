struct VSOutput {
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD;
};

VSOutput main(uint vertexID : SV_VertexID)
{
    VSOutput output;
    // Fullscreen triangle: vertexID 0,1,2 → covers entire clip space
    float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
    output.uv       = uv;
    output.position = float4(uv.x * 2.0f - 1.0f, -(uv.y * 2.0f - 1.0f), 0.0f, 1.0f);
    return output;
}
