#version 450

#extension GL_KHR_vulkan_glsl : enable

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vColor;

layout(location = 0) out vec4 oColor;

layout(set = 0, binding = 0) uniform sampler2D uFontAtlas;

void main()
{
    vec4 l_Sample = texture(uFontAtlas, vUV);
    float l_Alpha = l_Sample.a;
    oColor = vec4(vColor.rgb * l_Alpha, vColor.a * l_Alpha);
    // TODO: Introduce configurable text effects (drop shadows, outlines) once the style system matures.
}