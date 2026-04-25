Texture2D gDiffuseMap : register(t0);
Texture2D gSecondaryMap : register(t1);
SamplerState gSampler : register(s0);

cbuffer cbPerObject : register(b0)
{
    float4x4 mWorld;
    float4x4 mWorldViewProj;
    float4 mUVTransform;
    float4 mChessboardParams;
    float4 mTime;
};

float gIsFlag : register(b1);

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

    float3 modifiedPos = vin.Pos;

    if (gIsFlag > 0.5f)
    {
        float anchor = saturate(vin.Tex.x);
        float anchor2 = anchor * anchor;
        float wavePhase = mTime.x * 1.8f;
        float primaryWave = sin(vin.Pos.x * 2.4f - wavePhase * 2.2f);
        float secondaryWave = sin(vin.Pos.x * 5.1f - wavePhase * 3.4f + vin.Tex.y * 1.7f);

        modifiedPos.z += (primaryWave * 0.08f + secondaryWave * 0.025f) * anchor2;
        modifiedPos.y += sin(vin.Pos.x * 3.3f - wavePhase * 2.6f + vin.Tex.y * 2.1f) * 0.03f * anchor;
    }

    float4 worldPos = mul(float4(modifiedPos, 1.0f), mWorld);
    vout.PosH = mul(float4(modifiedPos, 1.0f), mWorldViewProj);
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
