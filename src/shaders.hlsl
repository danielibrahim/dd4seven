SamplerState SampleType;
Texture2D shaderTexture;

struct VOut
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

VOut VShader(float4 position : POSITION, float2 texcoord : TEXCOORD0)
{
    VOut output;

    output.position = position;
    output.texcoord = texcoord;

    return output;
}


float4 PShader(float4 position : SV_POSITION, float2 texcoord : TEXCOORD0) : SV_TARGET
{
    return shaderTexture.Sample(SampleType, texcoord);
}

