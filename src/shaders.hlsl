Texture2D gDiffuseMap : register(t0);
SamplerState gSampler : register(s0);

cbuffer cbPerObject : register(b0)
{
    float4x4 mWorldViewProj;
    float4 mUVTransform;  // x = scaleU, y = scaleV, z = offsetU, w = offsetV
};

struct VSInput
{
    float3 Pos : POSITION;
    float3 Normal : NORMAL;
    float2 Tex : TEXCOORD;
};

struct PSInput
{
    float4 PosH : SV_POSITION;
    float2 TexC : TEXCOORD;
};

PSInput VS(VSInput vin)
{
    PSInput vout;
    vout.PosH = mul(float4(vin.Pos, 1.0f), mWorldViewProj);

    vout.TexC = vin.Tex * mUVTransform.xy + mUVTransform.zw;

    return vout;
}

float4 PS(PSInput pin) : SV_Target
{
    float4 tex = gDiffuseMap.Sample(gSampler, pin.TexC);


    return tex;
}