cbuffer cbShadow : register(b0)
{
    float4x4 mLightViewProj;
};

struct VSInput
{
    float3 Pos : POSITION;
};

float4 VS(VSInput vin) : SV_POSITION
{
    return mul(float4(vin.Pos, 1.0f), mLightViewProj);
}
