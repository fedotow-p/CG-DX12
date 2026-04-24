Texture2D gDiffuseMap : register(t0);
SamplerState gSampler : register(s0);

cbuffer cbPerObject : register(b0)
{
    float4x4 mWorldViewProj;
    float4 mUVTransform;  // x = scaleU, y = scaleV, z = offsetU, w = offsetV
    float4 mTime;
};

struct VSInput
{
    float3 Pos : POSITION;
    float3 Normal : NORMAL;
    float2 Tex : TEXCOORD;
};

struct PSInput
{
    float4 PosH : SV_POSITION; //SYstem value pos
    float2 TexC : TEXCOORD;
};

float gIsFlag : register(b1);

PSInput VS(VSInput vin)
{
    PSInput vout;

    float3 modifiedPos = vin.Pos;

    if (gIsFlag > 0.5f)
    {
        float wave = sin(vin.Pos.x * 3.0f - mTime.x * 4.0f);
        modifiedPos.z += wave * 0.03f;

        float waveY = sin(vin.Pos.x * 5.0f - mTime.x * 3.0f) * 0.02f;
        modifiedPos.y += waveY;
    }

    vout.PosH = mul(float4(modifiedPos, 1.0f), mWorldViewProj);
    vout.TexC = vin.Tex * mUVTransform.xy + mUVTransform.zw;

    return vout;
}

float4 PS(PSInput pin) : SV_Target
{
    float4 tex = gDiffuseMap.Sample(gSampler, pin.TexC);


    return tex;
}