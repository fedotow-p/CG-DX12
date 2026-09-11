// Post-processing pass: depth of field + chromatic aberration.
Texture2D gSceneColor : register(t0);
Texture2D gDepthMap : register(t1);
SamplerState gSampler : register(s0);

cbuffer cbPostProcess : register(b0)
{
    float gFocusDistance;          // Distance from camera where the image is sharp.
    float gFocusRange;             // Half-width of the sharp depth range.
    float gMaxBlurRadiusPixels;    // Maximum blur radius in pixels.
    float gChromaticAberrationPixels;
    float2 gInvScreenSize;
    float2 gPostProcessPadding;
};

// Layout matches CameraConstants bound at b1.
cbuffer cbCamera : register(b1)
{
    float4x4 mInvViewProj;
    float4x4 mView;
    float4x4 mCascadeViewProj[4];
    float3 mCameraPos;
    float padding1;
    float2 mScreenSize;
    float2 padding2;
    float4 mCascadeSplits;
};

struct VSInput
{
    uint VertexId : SV_VertexID;
};

struct PSInput
{
    float4 PosH : SV_POSITION;
    float2 TexC : TEXCOORD;
};

PSInput VS(VSInput vin)
{
    PSInput output;
    float2 uv = float2(vin.VertexId & 1, vin.VertexId >> 1);
    output.TexC = uv;
    output.PosH = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, 0.0f, 1.0f);
    return output;
}

float3 ReconstructWorldPosition(float2 uv, float depth)
{
    float4 clipPosition = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, depth, 1.0f);
    float4 worldPosition = mul(clipPosition, mInvViewProj);
    return worldPosition.xyz / worldPosition.w;
}

float3 SampleChromatic(float2 uv)
{
    float2 radialDirection = uv - 0.5f;
    float radialLength = max(length(radialDirection), 1.0e-5f);
    float2 chromaticOffset = radialDirection / radialLength
        * gChromaticAberrationPixels * gInvScreenSize;

    // Red and blue channels shift in opposite directions; green stays centered.
    float red = gSceneColor.Sample(gSampler, uv + chromaticOffset).r;
    float green = gSceneColor.Sample(gSampler, uv).g;
    float blue = gSceneColor.Sample(gSampler, uv - chromaticOffset).b;
    return float3(red, green, blue);
}

float4 PS(PSInput pin) : SV_Target
{
    float depth = gDepthMap.Sample(gSampler, pin.TexC).r;
    if (depth > 0.99999f)
        return float4(SampleChromatic(pin.TexC), 1.0f);

    float3 worldPosition = ReconstructWorldPosition(pin.TexC, depth);
    float cameraDistance = distance(worldPosition, mCameraPos);
    float coc = saturate(abs(cameraDistance - gFocusDistance) / max(gFocusRange, 0.001f));
    float blurRadius = coc * gMaxBlurRadiusPixels;

    // A compact 3x3 kernel. The radius changes with the circle of confusion.
    float3 color = 0.0f;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
            color += SampleChromatic(pin.TexC + float2(x, y) * blurRadius * gInvScreenSize);
    }

    return float4(color / 9.0f, 1.0f);
//     return float4(coc, coc, coc, 1.0f);
}
