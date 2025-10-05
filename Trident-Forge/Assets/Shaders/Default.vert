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

layout(push_constant) uniform PushConstant
{
    mat4 Model;
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
    vec4 worldPosition = pc.Model * vec4(inPosition, 1.0);
    outWorldPosition = worldPosition.xyz;

    mat3 normalMatrix = transpose(inverse(mat3(pc.Model)));
    outNormal = normalize(normalMatrix * inNormal);
    outTangent = normalize(normalMatrix * inTangent);
    outBitangent = normalize(normalMatrix * inBitangent);
    outTexCoord = inTexCoord;
    outVertexColor = inColor;

    gl_Position = g_Global.Projection * g_Global.View * worldPosition;
}