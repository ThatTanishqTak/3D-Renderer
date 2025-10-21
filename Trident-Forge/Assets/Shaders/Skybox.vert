#version 450

layout(location = 0) in vec3 inPosition;

layout(location = 0) out vec3 outDirection;

layout(push_constant) uniform PushConstants
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
    vec3 worldPosition = (pc.Model * vec4(inPosition, 1.0)).xyz;

    mat3 viewRotation = mat3(g_Global.View);
    mat4 viewNoTranslation = mat4(viewRotation);

    vec4 clipPosition = g_Global.Projection * viewNoTranslation * vec4(worldPosition, 1.0);
    gl_Position = vec4(clipPosition.xy, clipPosition.w, clipPosition.w);

    outDirection = viewRotation * worldPosition;
}