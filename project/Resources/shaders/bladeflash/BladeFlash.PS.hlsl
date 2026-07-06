struct PSInput
{
    float4 position : SV_POSITION;
    float4 color    : COLOR0;
};

struct PSOutput
{
    float4 color : SV_TARGET0;
};

PSOutput main(PSInput input)
{
    PSOutput output;
    output.color = input.color;
    if (output.color.a <= 0.001f)
    {
        discard;
    }
    return output;
}
