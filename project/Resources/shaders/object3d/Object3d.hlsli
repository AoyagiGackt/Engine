struct VertexShaderOutput
{
    float4 position      : SV_POSITION;
    float2 texcoord      : TEXCOORD0;
    float3 normal        : TEXCOORD3; // NORMAL0 は VS 出力として無効なため TEXCOORD を使用
    float3 worldPos      : TEXCOORD1; // ワールド座標（スペキュラ計算用）
    float4 lightSpacePos : TEXCOORD2; // ライト空間座標（シャドウ計算用）
};
