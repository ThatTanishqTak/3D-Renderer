#version 450

#extension GL_EXT_nonuniform_qualifier : require // Needed for dynamic indexing into the sampler array.

layout(location = 0) in vec3 inWorldPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inTangent;
layout(location = 3) in vec3 inBitangent;
layout(location = 4) in vec2 inTexCoord;
layout(location = 5) in vec3 inVertexColor;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform RenderablePushConstant
{
    mat4 ModelMatrix;          // Object to world transform (unused here but kept for parity).
    vec4 TintColor;            // Per-draw tint multiplier applied after lighting.
    vec2 TextureScale;         // UV scale baked into the vertex stage (retained for layout match).
    vec2 TextureOffset;        // UV offset baked into the vertex stage (retained for layout match).
    float TilingFactor;        // Additional tiling scalar (applied in vertex shader).
    int TextureSlot;           // Index selecting the appropriate base-color sampler.
    int UseMaterialOverride;   // Signals material override usage (future extension point).
    float SortBias;            // Reserved depth bias to match CPU structure.
    int MaterialIndex;         // Material lookup index for extended shading data.
    int Padding0;              // Padding maintained for std140 alignment.
    int Padding1;              // Padding maintained for std140 alignment.
    int Padding2;              // Padding maintained for std140 alignment.
} pc;

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

layout(set = 0, binding = 2) uniform sampler2D BaseColorSamplers[]; // Array lets us bind many textures while reusing the shader.

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
    vec3 l_Tangent = normalize(inTangent);
    vec3 l_Bitangent = normalize(inBitangent);
    vec3 l_Normal = normalize(inNormal);
    mat3 l_TBN = mat3(l_Tangent, l_Bitangent, l_Normal);
    vec3 l_ShadingNormal = normalize(l_TBN * vec3(0.0, 0.0, 1.0));

    vec3 l_ViewDirection = normalize(g_Global.CameraPosition.xyz - inWorldPosition);

    int l_TextureSlot = pc.TextureSlot; // Copy to a local so we can mark the index non-uniform for Vulkan descriptor indexing.
    vec4 l_SampledColor = texture(BaseColorSamplers[nonuniformEXT(l_TextureSlot)], inTexCoord); // Use the slot pushed from the renderer.
    vec3 l_Albedo = l_SampledColor.rgb * g_Material.BaseColorFactor.rgb * pc.TintColor.rgb * inVertexColor;
    float l_Metallic = clamp(g_Material.MaterialFactors.x, 0.0, 1.0);
    float l_Roughness = clamp(g_Material.MaterialFactors.y, 0.045, 1.0);
    float l_AmbientStrength = clamp(g_Material.MaterialFactors.z, 0.0, 1.0);

    vec3 l_F0 = mix(vec3(0.04), l_Albedo, l_Metallic);

    vec3 l_Direct = vec3(0.0);

    if (g_Global.LightCounts.x > 0u)
    {
        vec3 l_LightDirection = normalize(-g_Global.DirectionalLightDirection.xyz);
        vec3 l_Radiance = g_Global.DirectionalLightColor.rgb * g_Global.DirectionalLightColor.w;
        l_Direct += EvaluatePBRLighting(l_LightDirection, l_Radiance, l_ShadingNormal, l_ViewDirection, l_Albedo, l_Metallic, l_Roughness, l_F0);
    }

    uint l_PointCount = min(g_Global.LightCounts.y, uint(8));
    for (uint l_LightIndex = 0u; l_LightIndex < l_PointCount; ++l_LightIndex)
    {
        PointLightUniform l_Light = g_Global.PointLights[l_LightIndex];
        vec3 l_ToLight = l_Light.PositionRange.xyz - inWorldPosition;
        float l_DistanceToLight = length(l_ToLight);
        if (l_DistanceToLight <= 1e-4)
        {
            continue;
        }

        vec3 l_LightDirection = l_ToLight / l_DistanceToLight;
        float l_Radius = max(l_Light.PositionRange.w, 1e-4);
        float l_NormalizedDistance = clamp(l_DistanceToLight / l_Radius, 0.0, 1.0);
        float l_Attenuation = 1.0 - l_NormalizedDistance;
        l_Attenuation *= l_Attenuation; // Smooth roll-off toward the light's edge.

        vec3 l_Radiance = l_Light.ColorIntensity.rgb * l_Light.ColorIntensity.w * l_Attenuation;
        l_Direct += EvaluatePBRLighting(l_LightDirection, l_Radiance, l_ShadingNormal, l_ViewDirection, l_Albedo, l_Metallic, l_Roughness, l_F0);
    }

    vec3 l_Ambient = g_Global.AmbientColorIntensity.rgb * g_Global.AmbientColorIntensity.w * l_Albedo * l_AmbientStrength;

    vec3 l_Color = l_Ambient + l_Direct;
    l_Color = l_Color / (l_Color + vec3(1.0));
    l_Color = pow(l_Color, vec3(1.0 / 2.2));

    outColor = vec4(l_Color, g_Material.BaseColorFactor.a * pc.TintColor.a * l_SampledColor.a);

    // Future enhancement: once the base-color flow is proven we can wire up metallic/roughness and normal textures here.
}