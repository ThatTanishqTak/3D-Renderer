#version 450

layout(location = 0) in vec3 inWorldPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inTangent;
layout(location = 3) in vec3 inBitangent;
layout(location = 4) in vec2 inTexCoord;
layout(location = 5) in vec3 inVertexColor;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform GlobalUniformBuffer
{
    mat4 View;
    mat4 Projection;
    vec4 CameraPosition;
    vec4 LightDirection;
    vec4 LightColorIntensity;
    vec4 AmbientColorIntensity;
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

void main()
{
    // Transform the default tangent-space normal into world space.
    vec3 T = normalize(inTangent);
    vec3 B = normalize(inBitangent);
    vec3 N = normalize(inNormal);
    mat3 TBN = mat3(T, B, N);
    vec3 shadingNormal = normalize(TBN * vec3(0.0, 0.0, 1.0));

    vec3 viewDirection = normalize(g_Global.CameraPosition.xyz - inWorldPosition);
    vec3 lightDirection = normalize(-g_Global.LightDirection.xyz);
    vec3 halfVector = normalize(viewDirection + lightDirection);

    vec4 sampledColor = texture(BaseColorSampler, inTexCoord);
    vec3 albedo = sampledColor.rgb * g_Material.BaseColorFactor.rgb * inVertexColor;
    float metallic = clamp(g_Material.MaterialFactors.x, 0.0, 1.0);
    float roughness = clamp(g_Material.MaterialFactors.y, 0.045, 1.0);
    float ambientStrength = clamp(g_Material.MaterialFactors.z, 0.0, 1.0);

    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    float NDF = DistributionGGX(shadingNormal, halfVector, roughness);
    float geometry = GeometrySmith(shadingNormal, viewDirection, lightDirection, roughness);
    vec3 fresnel = FresnelSchlick(max(dot(halfVector, viewDirection), 0.0), F0);

    vec3 numerator = NDF * geometry * fresnel;
    float denominator = max(4.0 * max(dot(shadingNormal, viewDirection), 0.0) * max(dot(shadingNormal, lightDirection), 0.0), 1e-4);
    vec3 specular = numerator / denominator;

    vec3 kS = fresnel;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

    float NdotL = max(dot(shadingNormal, lightDirection), 0.0);
    vec3 radiance = g_Global.LightColorIntensity.rgb * g_Global.LightColorIntensity.w;

    vec3 direct = (kD * albedo / PI + specular) * radiance * NdotL;
    vec3 ambient = g_Global.AmbientColorIntensity.rgb * g_Global.AmbientColorIntensity.w * albedo * ambientStrength;

    vec3 color = ambient + direct;
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));

    outColor = vec4(color, g_Material.BaseColorFactor.a * sampledColor.a);
}