Texture2D gAlbedoMap : register(t0);
Texture2D gNormalMap : register(t1);
Texture2D gMaterialMap : register(t2);
Texture2D gDepthMap : register(t3);
Texture2DArray gShadowMap : register(t4);
TextureCube gIrradianceMap : register(t5);
Texture2D gBrdfIntegrationMap : register(t6);
TextureCube gPrefilteredEnvironmentMap : register(t7);
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

// Минимальный набор данных, получаемый пиксельным шейдером из G-buffer.
struct GBufferData
{
    float4 Albedo;
    float3 Normal;
    float Metallic;
    float Roughness;
    float Depth;
};

GBufferData ReadGBuffer(float2 texCoord)
{
    GBufferData data;
    data.Albedo = gAlbedoMap.Sample(gSampler, texCoord);
    data.Normal = gNormalMap.Sample(gSampler, texCoord).xyz;
    float2 material = gMaterialMap.Sample(gSampler, texCoord).rg;
    data.Metallic = saturate(material.x);
    data.Roughness = max(material.y, 0.045f);
    data.Depth = gDepthMap.Sample(gSampler, texCoord).r;
    return data;
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

static const float PI = 3.14159265359f;

float DistributionGGX(float3 N, float3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float nDotH = saturate(dot(N, H));
    float nDotH2 = nDotH * nDotH;
    float denominator = nDotH2 * (a2 - 1.0f) + 1.0f;
    return a2 / max(PI * denominator * denominator, 0.0001f);
}

float GeometrySchlickGGX(float nDotV, float roughness)
{
    float k = (roughness + 1.0f);
    k = (k * k) / 8.0f;
    return nDotV / max(nDotV * (1.0f - k) + k, 0.0001f);
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    return GeometrySchlickGGX(saturate(dot(N, V)), roughness)
         * GeometrySchlickGGX(saturate(dot(N, L)), roughness);
}

float3 FresnelSchlick(float cosine, float3 F0)
{
    return F0 + (1.0f - F0) * pow(1.0f - cosine, 5.0f);
}

float3 FresnelSchlickRoughness(float cosine, float3 F0, float roughness)
{
    return F0 + (max(1.0f - roughness, F0) - F0) * pow(1.0f - cosine, 5.0f);
}

float3 EvaluateDirectPbr(float3 N, float3 V, float3 L, float3 radiance,
    float3 albedo, float metallic, float roughness)
{
    float3 H = normalize(V + L);
    float3 F0 = lerp(0.04f.xxx, albedo, metallic);
    float3 F = FresnelSchlick(saturate(dot(H, V)), F0);
    float NDF = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    float denominator = max(4.0f * saturate(dot(N, V)) * saturate(dot(N, L)), 0.0001f);
    float3 specular = (NDF * G * F) / denominator;
    float3 kS = F;
    float3 kD = (1.0f - kS) * (1.0f - metallic);
    return (kD * albedo / PI + specular) * radiance * saturate(dot(N, L));
}

float3 EvaluateIBL(float3 N, float3 V, float3 albedo, float metallic, float roughness)
{
    float nDotV = saturate(dot(N, V));
    float3 F0 = lerp(0.04f.xxx, albedo, metallic);
    float3 F = FresnelSchlickRoughness(nDotV, F0, roughness);
    float3 kS = F;
    float3 kD = (1.0f - kS) * (1.0f - metallic);
    float3 irradiance = gIrradianceMap.Sample(gSampler, N).rgb;
    float3 diffuse = irradiance * albedo;
    float3 reflection = reflect(-V, N);
    const float maxReflectionLod = 11.0f;
    float3 prefiltered = gPrefilteredEnvironmentMap.SampleLevel(gSampler, reflection, roughness * maxReflectionLod).rgb;
    float2 brdf = gBrdfIntegrationMap.Sample(gSampler, float2(nDotV, roughness)).rg;
    float3 specular = prefiltered * (F * brdf.x + brdf.y);
    return kD * diffuse + specular;
}

float4 PS(PSInput pin) : SV_Target
{
    // Заготовка PS: считываем входные текстуры G-buffer по экранным UV.
    GBufferData gbuffer = ReadGBuffer(pin.TexC);
    float4 albedo = gbuffer.Albedo;
    float3 normal = normalize(gbuffer.Normal);
    float metallic = gbuffer.Metallic;
    float roughness = gbuffer.Roughness;
    float depth = gbuffer.Depth;

    // Восстанавливаем мировую позицию
    float3 worldPos = ReconstructWorldPos(pin.TexC, depth, mInvViewProj);

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
        result = EvaluateIBL(normal, viewDir, albedo.rgb, metallic, roughness);
    }
    if (gLightType == LIGHT_DIRECTIONAL)
    {
        float3 lightDir = normalize(-gLightDir);
        result = EvaluateDirectPbr(normal, viewDir, lightDir, gLightColor * gLightIntensity,
            albedo.rgb, metallic, roughness) * GetShadowFactor(worldPos);
    }
    if (gLightType == LIGHT_POINT)
    {
        float3 lightDir = gLightPos - worldPos;
        float distance = length(lightDir);
        lightDir = normalize(lightDir);

        float attenuation = 1.0f - saturate(distance / gLightRange);
        attenuation = attenuation * attenuation;

        result = EvaluateDirectPbr(normal, viewDir, lightDir, gLightColor * gLightIntensity * attenuation,
            albedo.rgb, metallic, roughness);
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
            result = EvaluateDirectPbr(normal, viewDir, lightDir,
                gLightColor * gLightIntensity * attenuation * spotFactor, albedo.rgb, metallic, roughness);
        }
    }

    return float4(result, 0.0f);
}
