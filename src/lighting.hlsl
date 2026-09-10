Texture2D gAlbedoMap : register(t0);
Texture2D gNormalMap : register(t1);
Texture2D gDepthMap : register(t2);
Texture2DArray gShadowMap : register(t3);
SamplerState gSampler : register(s0);
SamplerComparisonState gShadowSampler : register(s1);

cbuffer cbLighting : register(b0)
{
    float3 gLightPos;
    float gLightIntensity;
    float3 gLightColor;
    float gLightRange;
    float3 gLightDir;
    float gSpotAngle;
    float3 gAmbientColor;
    int gLightType;
    float3 gCameraPos;
    float padding;
};

cbuffer cbCamera : register(b1)
{
    float4x4 mInvViewProj;
    float3 mCameraPos;
    float padding1;
    float2 mScreenSize;
    float2 padding2;
};

static const int CASCADE_COUNT = 4;

cbuffer cbShadow : register(b2)
{
    float4x4 gLightViewProj[CASCADE_COUNT];
    float4 gCascadeSplits;   // view-space far distance of each cascade
    float4 gShadowMapSize;   // x = size, y = 1/size
    float4 gLightDirShadow;  // xyz = light direction (world space)
};


// Константы для типов света
static const int LIGHT_AMBIENT = 0;
static const int LIGHT_DIRECTIONAL = 1;
static const int LIGHT_POINT = 2;
static const int LIGHT_SPOT = 3;

struct VSInput
{
    uint vertexId : SV_VertexID;
};

struct PSInput
{
    float4 PosH : SV_POSITION;
    float2 TexC : TEXCOORD;
};

int SelectCascade(float viewDepth)
{
    for (int i = 0; i < CASCADE_COUNT - 1; ++i)
    {
        if (viewDepth < gCascadeSplits[i])
            return i;
    }
    return CASCADE_COUNT - 1;
}

// 3x3 Percentage-Closer Filtering: averages hardware depth-comparison taps
// around the shadow texel to soften cascade edges and reduce aliasing.
float SampleShadowPCF(int cascade, float3 shadowCoord)
{
    float shadow = 0.0f;
    const float texelSize = gShadowMapSize.y;

    [unroll]
    for (int x = -1; x <= 1; ++x)
    {
        [unroll]
        for (int y = -1; y <= 1; ++y)
        {
            float2 offset = float2(x, y) * texelSize;
            shadow += gShadowMap.SampleCmpLevelZero(
                gShadowSampler,
                float3(shadowCoord.xy + offset, cascade),
                shadowCoord.z);
        }
    }

    return shadow / 9.0f;
}

float CalculateShadowFactor(float3 worldPos, float viewDepth, float3 normal)
{
    int cascade = SelectCascade(viewDepth);

    float4 shadowClip = mul(float4(worldPos, 1.0f), gLightViewProj[cascade]);
    float3 shadowNdc = shadowClip.xyz / shadowClip.w;

    float3 shadowCoord;
    shadowCoord.x = shadowNdc.x * 0.5f + 0.5f;
    shadowCoord.y = 1.0f - (shadowNdc.y * 0.5f + 0.5f);
    shadowCoord.z = shadowNdc.z;

    if (shadowCoord.x < 0.0f || shadowCoord.x > 1.0f ||
        shadowCoord.y < 0.0f || shadowCoord.y > 1.0f ||
        shadowCoord.z < 0.0f || shadowCoord.z > 1.0f)
    {
        return 1.0f;
    }

    // Slope-scaled bias reduces shadow acne on surfaces facing away from the light.
    float nDotL = saturate(dot(normal, -normalize(gLightDirShadow.xyz)));
    float bias = lerp(0.0025f, 0.0004f, nDotL);
    shadowCoord.z -= bias;

    return SampleShadowPCF(cascade, shadowCoord);
}

float3 ReconstructWorldPos(float2 texCoord, float depth, float4x4 invViewProj)
{
    // Конвертируем texCoord в NDC [-1, 1]
    float x = texCoord.x * 2.0f - 1.0f;
    float y = (1.0f - texCoord.y) * 2.0f - 1.0f;  // Инвертируем Y

    // Восстанавливаем позицию в клип-пространстве
    float4 clipPos = float4(x, y, depth, 1.0f);

    // Переводим в мировое пространство
    float4 worldPos = mul(clipPos, invViewProj);
    return worldPos.xyz / worldPos.w;
}

PSInput VS(VSInput vin)
{
    PSInput vout;
    float2 texCoord = float2((vin.vertexId << 1) & 2, vin.vertexId & 2);
    vout.TexC = texCoord;
    vout.PosH = float4(texCoord.x * 2.0f - 1.0f, -(texCoord.y * 2.0f - 1.0f), 0.0f, 1.0f);
    return vout;
}

float4 PS(PSInput pin) : SV_Target
{
    float4 albedo = gAlbedoMap.Sample(gSampler, pin.TexC);
    float4 normalData = gNormalMap.Sample(gSampler, pin.TexC);
    float depth = gDepthMap.Sample(gSampler, pin.TexC).r;  // Только красный канал

    // Восстанавливаем мировую позицию
    float3 worldPos = ReconstructWorldPos(pin.TexC, depth, mInvViewProj);
    float3 normal = normalize(normalData.xyz);

    float3 viewDir = normalize(mCameraPos - worldPos);
    // Проверка на фон (по глубине)
    if (depth > 0.99999f)
    {
        if (gLightType == LIGHT_AMBIENT)
            return float4(0.53f, 0.81f, 0.98f, 1.0f);

        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    }

    float3 result = float3(0, 0, 0);

    // Расчет освещения в зависимости от типа
    if (gLightType == LIGHT_AMBIENT)
    {
        result = albedo.rgb * gAmbientColor;
    }
    if (gLightType == LIGHT_DIRECTIONAL)
    {
        float3 lightDir = normalize(-gLightDir);
        float diff = max(dot(normal, lightDir), 0.0f);

        float viewDepth = length(worldPos - mCameraPos);
        float shadow = CalculateShadowFactor(worldPos, viewDepth, normal);

        result = diff * gLightColor * gLightIntensity * albedo.rgb * shadow;
    }
    if (gLightType == LIGHT_POINT)
    {
        float3 lightDir = gLightPos - worldPos;
        float distance = length(lightDir);
        lightDir = normalize(lightDir);

        float attenuation = 1.0f - saturate(distance / gLightRange);
        attenuation = attenuation * attenuation;

        float diff = max(dot(normal, lightDir), 0.0f);
        result = diff * gLightColor * gLightIntensity * albedo.rgb * attenuation;
    }
    if (gLightType == LIGHT_SPOT)
    {
        float3 lightDir = gLightPos - worldPos;
        float distance = length(lightDir);
        lightDir = normalize(lightDir);

        float3 spotDir = normalize(gLightDir);
        float cosAngle = dot(lightDir, spotDir);
        float cosCone = cos(gSpotAngle / 2.0f);

        if (cosAngle > cosCone)
        {
            float attenuation = 1.0f - saturate(distance / gLightRange);
            attenuation = attenuation * attenuation;

            float spotFactor = saturate((cosAngle - cosCone) / (1.0f - cosCone));
            float diff = max(dot(normal, lightDir), 0.0f);
            result = diff * gLightColor * gLightIntensity * albedo.rgb * attenuation * spotFactor;
        }
    }

    return float4(result, 0.0f);
}
