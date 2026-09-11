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
    float4x4 mView;
    float4x4 mCascadeViewProj[4];
    float3 mCameraPos;
    float padding1;
    float2 mScreenSize;
    float2 padding2;
    float4 mCascadeSplits;
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

    // ID вершин для triangle strip: 0, 1, 2, 3.
    // Формируем UV (0,0), (1,0), (0,1), (1,1) прямо из ID,
    // поэтому входной вершинный буфер не требуется.
    float2 texCoord = float2(vin.vertexId & 1, vin.vertexId >> 1);
    vout.TexC = texCoord;
    vout.PosH = float4(texCoord.x * 2.0f - 1.0f, -(texCoord.y * 2.0f - 1.0f), 0.0f, 1.0f);
    return vout;
}

float GetShadowFactor(float3 worldPos)
{
    float viewDepth = abs(mul(float4(worldPos, 1.0f), mView).z);
    uint cascadeIndex = viewDepth <= mCascadeSplits.x ? 0 :
                        viewDepth <= mCascadeSplits.y ? 1 :
                        viewDepth <= mCascadeSplits.z ? 2 : 3;

    float4 lightPos = mul(float4(worldPos, 1.0f), mCascadeViewProj[cascadeIndex]);
    float3 shadowCoord = lightPos.xyz / lightPos.w;
    float2 uv = float2(shadowCoord.x * 0.5f + 0.5f, 1.0f - (shadowCoord.y * 0.5f + 0.5f));
    if (any(uv < 0.0f) || any(uv > 1.0f) || shadowCoord.z <= 0.0f || shadowCoord.z >= 1.0f)
        return 1.0f;

    // 3x3 PCF: each comparison is hardware-filtered by SampleCmpLevelZero.
    const float texelSize = 1.0f / 2048.0f;
    float visibility = 0.0f;
    [unroll] for (int y = -1; y <= 1; ++y)
    {
        [unroll] for (int x = -1; x <= 1; ++x)
            visibility += gShadowMap.SampleCmpLevelZero(gShadowSampler,
                float3(uv + float2(x, y) * texelSize, cascadeIndex), shadowCoord.z - 0.0015f);
    }
    return visibility / 9.0f;
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
        result = diff * gLightColor * gLightIntensity * albedo.rgb * GetShadowFactor(worldPos);
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
