#version 450

#extension GL_KHR_vulkan_glsl : enable

layout(location = 0) in vec3 inWorldPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inTangent;
layout(location = 3) in vec3 inBitangent;
layout(location = 4) in vec2 inTexCoord;
layout(location = 5) in vec3 inVertexColor;

layout(location = 0) out vec4 outColor;

struct PointLightUniform
{
    vec4 PositionRange;   // xyz = position, w = radius
    vec4 ColorIntensity;  // rgb = colour, w = intensity multiplier
};

layout(set = 0, binding = 0) uniform GlobalUniformBuffer
{
    mat4 View;
    mat4 Projection;
    vec4 CameraPosition;
    vec4 AmbientColorIntensity;
    vec4 DirectionalLightDirection;
    vec4 DirectionalLightColor;
    uvec4 LightCounts;
    PointLightUniform PointLights[8];
} g_Global;

layout(set = 0, binding = 1) uniform MaterialUniformBuffer
{
    vec4 BaseColorFactor;
    vec4 MaterialFactors; // x = metallic, y = roughness, z = ambient strength, w reserved
} g_Material;

layout(set = 0, binding = 2) uniform sampler2D BaseColorSampler;

const float PI = 3.14159265359;

float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    return a2 / (PI * denom * denom);
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;

    float denom = NdotV * (1.0 - k) + k;
    return NdotV / max(denom, 1e-4);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

vec3 FresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 EvaluatePBRLighting(vec3 lightDirection, vec3 radiance, vec3 shadingNormal, vec3 viewDirection, vec3 albedo, float metallic, float roughness, vec3 F0)
{
    vec3 halfVector = normalize(viewDirection + lightDirection);

    float NDF = DistributionGGX(shadingNormal, halfVector, roughness);
    float geometry = GeometrySmith(shadingNormal, viewDirection, lightDirection, roughness);
    vec3 fresnel = FresnelSchlick(max(dot(halfVector, viewDirection), 0.0), F0);

    vec3 numerator = NDF * geometry * fresnel;
    float denominator = max(4.0 * max(dot(shadingNormal, viewDirection), 0.0) * max(dot(shadingNormal, lightDirection), 0.0), 1e-4);
    vec3 specular = numerator / denominator;

    vec3 kS = fresnel;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);
    float NdotL = max(dot(shadingNormal, lightDirection), 0.0);

    return (kD * albedo / PI + specular) * radiance * NdotL;
}

void main()
{
    // Transform the default tangent-space normal into world space.
    vec3 T = normalize(inTangent);
    vec3 B = normalize(inBitangent);
    vec3 N = normalize(inNormal);
    mat3 TBN = mat3(T, B, N);
    vec3 shadingNormal = normalize(TBN * vec3(0.0, 0.0, 1.0));

    vec3 viewDirection = normalize(g_Global.CameraPosition.xyz - inWorldPosition);

    vec4 sampledColor = texture(BaseColorSampler, inTexCoord);
    vec3 albedo = sampledColor.rgb * g_Material.BaseColorFactor.rgb * inVertexColor;
    float metallic = clamp(g_Material.MaterialFactors.x, 0.0, 1.0);
    float roughness = clamp(g_Material.MaterialFactors.y, 0.045, 1.0);
    float ambientStrength = clamp(g_Material.MaterialFactors.z, 0.0, 1.0);

    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    vec3 direct = vec3(0.0);

    if (g_Global.LightCounts.x > 0u)
    {
        vec3 lightDirection = normalize(-g_Global.DirectionalLightDirection.xyz);
        vec3 radiance = g_Global.DirectionalLightColor.rgb * g_Global.DirectionalLightColor.w;
        direct += EvaluatePBRLighting(lightDirection, radiance, shadingNormal, viewDirection, albedo, metallic, roughness, F0);
    }

    uint pointCount = min(g_Global.LightCounts.y, uint(8));
    for (uint i = 0u; i < pointCount; ++i)
    {
        PointLightUniform light = g_Global.PointLights[i];
        vec3 toLight = light.PositionRange.xyz - inWorldPosition;
        float distanceToLight = length(toLight);
        if (distanceToLight <= 1e-4)
        {
            continue;
        }

        vec3 lightDirection = toLight / distanceToLight;
        float radius = max(light.PositionRange.w, 1e-4);
        float normalizedDistance = clamp(distanceToLight / radius, 0.0, 1.0);
        float attenuation = 1.0 - normalizedDistance;
        attenuation *= attenuation; // Smooth roll-off toward the light's edge.

        vec3 radiance = light.ColorIntensity.rgb * light.ColorIntensity.w * attenuation;
        direct += EvaluatePBRLighting(lightDirection, radiance, shadingNormal, viewDirection, albedo, metallic, roughness, F0);
    }

    vec3 ambient = g_Global.AmbientColorIntensity.rgb * g_Global.AmbientColorIntensity.w * albedo * ambientStrength;

    vec3 color = ambient + direct;
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));

    outColor = vec4(color, g_Material.BaseColorFactor.a * sampledColor.a);
}