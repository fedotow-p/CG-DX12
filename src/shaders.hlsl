Texture2D gDiffuseMap : register(t0);
Texture2D gNormalMap : register(t1);
Texture2D gHeightMap : register(t2);
SamplerState gSampler : register(s0);

cbuffer cbPerObject : register(b0)
{
    float4x4 mWorld;
    float4x4 mWorldViewProj;
    float4 mUVTransform;
    float4 mChessboardParams;
    float4 mTime;
    float4 mCameraPos;
    float4 mTessellationParams;
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
    float3 PosL : POSITION0;
    float3 NormalL : NORMAL0;
    float2 TexC : TEXCOORD0;
};

struct HSControlPoint
{
    float3 PosL : POSITION0;
    float3 NormalL : NORMAL0;
    float2 TexC : TEXCOORD0;
};

struct HSConstants
{
    float Edges[3] : SV_TessFactor;
    float Inside : SV_InsideTessFactor;
};

struct DSOutput
{
    float4 PosH : SV_POSITION;
    float3 WorldPos : POSITION0;
    float3 WorldNormal : NORMAL0;
    float2 TexC : TEXCOORD0;
};

VSOutput VS(VSInput vin)
{
    VSOutput vout;
    vout.PosL = vin.Pos;
    vout.NormalL = vin.Normal;
    vout.TexC = vin.Tex * mUVTransform.xy + mUVTransform.zw;
    return vout;
}

float3 ApplyFlagAnimation(float3 position, float2 texcoord)
{
    if (gIsFlag <= 0.5f)
    {
        return position;
    }

    float3 modifiedPos = position;
    float anchor = saturate(texcoord.x);
    float anchor2 = anchor * anchor;
    float wavePhase = mTime.x * 1.8f;
    float primaryWave = sin(position.x * 2.4f - wavePhase * 2.2f);
    float secondaryWave = sin(position.x * 5.1f - wavePhase * 3.4f + texcoord.y * 1.7f);

    modifiedPos.z += (primaryWave * 0.08f + secondaryWave * 0.025f) * anchor2;
    modifiedPos.y += sin(position.x * 3.3f - wavePhase * 2.6f + texcoord.y * 2.1f) * 0.03f * anchor;
    return modifiedPos;
}

HSConstants CalcHSPatchConstants(InputPatch<VSOutput, 3> patch, uint patchId : SV_PrimitiveID)
{
    HSConstants output;

    float3 p0 = ApplyFlagAnimation(patch[0].PosL, patch[0].TexC);
    float3 p1 = ApplyFlagAnimation(patch[1].PosL, patch[1].TexC);
    float3 p2 = ApplyFlagAnimation(patch[2].PosL, patch[2].TexC);
    float3 patchCenter = (p0 + p1 + p2) / 3.0f;
    float3 worldCenter = mul(float4(patchCenter, 1.0f), mWorld).xyz;

    float distanceToCamera = distance(worldCenter, mCameraPos.xyz);
    float maxTess = mTessellationParams.x;
    float minTess = mTessellationParams.y;
    float nearDist = mTessellationParams.z;
    float farDist = mTessellationParams.w;
    float tessAlpha = saturate((distanceToCamera - nearDist) / max(0.001f, farDist - nearDist));
    float tessLevel = lerp(maxTess, minTess, tessAlpha);

    output.Edges[0] = tessLevel;
    output.Edges[1] = tessLevel;
    output.Edges[2] = tessLevel;
    output.Inside = tessLevel;
    return output;
}

[domain("tri")]
[partitioning("fractional_odd")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("CalcHSPatchConstants")]
HSControlPoint HS(InputPatch<VSOutput, 3> patch, uint pointId : SV_OutputControlPointID, uint patchId : SV_PrimitiveID)
{
    HSControlPoint output;
    output.PosL = patch[pointId].PosL;
    output.NormalL = patch[pointId].NormalL;
    output.TexC = patch[pointId].TexC;
    return output;
}

[domain("tri")]
DSOutput DS(HSConstants hsConstants, const OutputPatch<HSControlPoint, 3> patch, float3 bary : SV_DomainLocation)
{
    DSOutput output;

    float3 posL =
        patch[0].PosL * bary.x +
        patch[1].PosL * bary.y +
        patch[2].PosL * bary.z;

    float3 normalL = normalize(
        patch[0].NormalL * bary.x +
        patch[1].NormalL * bary.y +
        patch[2].NormalL * bary.z);

    float2 texcoord =
        patch[0].TexC * bary.x +
        patch[1].TexC * bary.y +
        patch[2].TexC * bary.z;

    posL = ApplyFlagAnimation(posL, texcoord);

    float heightSample = gHeightMap.SampleLevel(gSampler, texcoord, 0).r;
    float displacementStrength = 0.12f;
    posL += normalL * ((heightSample - 0.5f) * displacementStrength);

    float4 worldPos = mul(float4(posL, 1.0f), mWorld);
    output.PosH = mul(float4(posL, 1.0f), mWorldViewProj);
    output.WorldPos = worldPos.xyz;
    output.WorldNormal = normalize(mul(normalL, (float3x3)mWorld));
    output.TexC = texcoord;

    return output;
}

float3x3 BuildCotangentFrame(float3 normal, float3 worldPos, float2 uv)
{
    float3 dp1 = ddx(worldPos);
    float3 dp2 = ddy(worldPos);
    float2 duv1 = ddx(uv);
    float2 duv2 = ddy(uv);

    float3 dp2perp = cross(dp2, normal);
    float3 dp1perp = cross(normal, dp1);
    float3 tangent = dp2perp * duv1.x + dp1perp * duv2.x;
    float3 bitangent = dp2perp * duv1.y + dp1perp * duv2.y;

    float invMax = rsqrt(max(dot(tangent, tangent), dot(bitangent, bitangent)));
    return float3x3(tangent * invMax, bitangent * invMax, normal);
}

struct PSOutput
{
    float4 Albedo : SV_Target0;
    float4 Normal : SV_Target1;
    float Depth : SV_Target2;
};

PSOutput PS(DSOutput pin)
{
    PSOutput pout;

    float4 albedo = gDiffuseMap.Sample(gSampler, pin.TexC);
    float3 normalSample = gNormalMap.Sample(gSampler, pin.TexC).xyz * 2.0f - 1.0f;
    float3 baseNormal = normalize(pin.WorldNormal);
    float3x3 tbn = BuildCotangentFrame(baseNormal, pin.WorldPos, pin.TexC);
    float3 mappedNormal = normalize(mul(normalSample, tbn));

    pout.Albedo = albedo;
    pout.Normal = float4(mappedNormal, 1.0f);
    pout.Depth = pin.PosH.z;

    return pout;
}
