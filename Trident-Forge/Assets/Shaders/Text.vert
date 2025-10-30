#version 450

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inColor;

layout(location = 0) out vec2 vUV;
layout(location = 1) out vec4 vColor;

layout(push_constant) uniform TextPushConstants
{
    vec2 uViewportSize;
} PC;

void main()
{
    vec2 l_Normalised = inPosition / PC.uViewportSize;
    vec2 l_Ndc = vec2(l_Normalised.x * 2.0 - 1.0, 1.0 - l_Normalised.y * 2.0);
    gl_Position = vec4(l_Ndc, 0.0, 1.0);
    vUV = inUV;
    vColor = inColor;
}