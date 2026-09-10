cbuffer ShadowPassConstants : register(b0)
{
    float4x4 gLightViewProj;
};

struct VSInput
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC : TEXCOORD;
};

struct VSOutput
{
    float4 PosH : SV_POSITION;
};

VSOutput VS(VSInput vin)
{
    VSOutput vout;
    vout.PosH = mul(float4(vin.PosL, 1.0f), gLightViewProj);
    return vout;
}
