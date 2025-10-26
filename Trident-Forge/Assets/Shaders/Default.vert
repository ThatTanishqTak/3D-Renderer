#version 450

// Vertex attributes provided by the mesh loader.
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inTangent;
layout(location = 3) in vec3 inBitangent;
layout(location = 4) in vec3 inColor;
layout(location = 5) in vec2 inTexCoord;

// Interpolated data consumed by the fragment shader.
layout(location = 0) out vec3 outWorldPosition;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec3 outTangent;
layout(location = 3) out vec3 outBitangent;
layout(location = 4) out vec2 outTexCoord;
layout(location = 5) out vec3 outVertexColor;

layout(push_constant) uniform RenderablePushConstant
{
    mat4 ModelMatrix;          // Object to world transform shared with the CPU side struct.
    vec4 TintColor;            // Per-draw tint multiplier applied in the fragment shader.
    vec2 TextureScale;         // UV scale applied before sampling the texture atlas.
    vec2 TextureOffset;        // UV offset supporting atlas layouts and sprite sheets.
    float TilingFactor;        // Additional scalar to modulate tiling beyond TextureScale.
    int TextureSlot;           // Index into the bindless base-color sampler array.
    int UseMaterialOverride;   // Non-zero when material overrides should be honored (reserved).
    float SortBias;            // Depth bias reserved for transparent layering (unused here).
    int MaterialIndex;         // Material lookup index for extended shading data.
    int Padding0;              // Padding to mirror RenderablePushConstant's std140 layout.
    int Padding1;              // Padding to keep 16-byte alignment intact.
    int Padding2;              // Padding reserved for future expansion.
} pc;

struct PointLightUniform
{
    vec4 PositionRange;
    vec4 ColorIntensity;
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

void main()
{
    vec4 l_WorldPosition = pc.ModelMatrix * vec4(inPosition, 1.0);
    outWorldPosition = l_WorldPosition.xyz;

    mat3 l_NormalMatrix = transpose(inverse(mat3(pc.ModelMatrix)));
    outNormal = normalize(l_NormalMatrix * inNormal);
    outTangent = normalize(l_NormalMatrix * inTangent);
    outBitangent = normalize(l_NormalMatrix * inBitangent);

    vec2 l_TiledTexCoord = (inTexCoord * pc.TextureScale * pc.TilingFactor) + pc.TextureOffset; // Apply atlas transforms up front.
    outTexCoord = l_TiledTexCoord;
    outVertexColor = inColor;

    gl_Position = g_Global.Projection * g_Global.View * l_WorldPosition;
}