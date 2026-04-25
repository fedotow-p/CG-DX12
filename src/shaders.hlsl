Texture2D gDiffuseMap : register(t0);
Texture2D gSecondaryMap : register(t1);
SamplerState gSampler : register(s0);

cbuffer cbPerObject : register(b0)
{
    float4x4 mWorld;
    float4x4 mWorldViewProj;
    float4 mUVTransform;
    float4 mChessboardParams;
};

struct VSInput
{
    float3 Pos : POSITION;
    float3 Normal : NORMAL;
    float2 Tex : TEXCOORD;
};

struct VSOutput
{
    float4 PosH : SV_POSITION;
    float3 WorldPos : POSITION0;
    float3 Normal : NORMAL0;
    float2 TexC : TEXCOORD0;
};

struct PSOutput
{
    float4 Albedo : SV_Target0;
    float4 Normal : SV_Target1;
    float Depth : SV_Target2;   // глубина в NDC (0..1)
};

VSOutput VS(VSInput vin)
{
    VSOutput vout;

    float4 worldPos = mul(float4(vin.Pos, 1.0f), mWorld);
    vout.PosH = mul(float4(vin.Pos, 1.0f), mWorldViewProj);
    vout.WorldPos = worldPos.xyz;
    vout.Normal = normalize(mul(vin.Normal, (float3x3)mWorld));
    vout.TexC = vin.Tex * mUVTransform.xy + mUVTransform.zw;

    return vout;
}

PSOutput PS(VSOutput pin)
{
    PSOutput pout;

    float4 albedo = gDiffuseMap.Sample(gSampler, pin.TexC);
    float4 tex2 = gSecondaryMap.Sample(gSampler, pin.TexC);
    float tileSize = mChessboardParams.x;
    float2 chessPos = pin.TexC / tileSize;
    int2 cell = floor(chessPos);
    int isEven = (cell.x + cell.y) % 2;

    pout.Albedo = (isEven == 1) ? tex2 : albedo;
    pout.Normal = float4(pin.Normal, 1.0f);
    pout.Depth = pin.PosH.z;          // глубина в NDC [0,1]

    return pout;
}