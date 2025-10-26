#version 450

#extension GL_KHR_vulkan_glsl : enable

layout(location = 0) in vec3 inDirection;
layout(location = 0) out vec4 outColor;

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

layout(set = 0, binding = 1) uniform samplerCube u_Skybox;

void main()
{
    vec3 sampleDirection = normalize(inDirection);
    vec3 skyColor = texture(u_Skybox, sampleDirection).rgb;

    outColor = vec4(skyColor, 1.0);
    // TODO: Blend HDR skyboxes here once image-based lighting is wired up.
}