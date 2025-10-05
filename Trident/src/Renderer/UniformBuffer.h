#pragma once

#include <cstdint>

#include <glm/glm.hpp>

constexpr uint32_t kMaxPointLights = 8;    // Hard cap to keep uniform buffers compact for forward rendering.

// Uniform-friendly representation of a point light.
struct PointLightUniform
{
    glm::vec4 PositionRange;             // xyz = world position, w = influence radius
    glm::vec4 ColorIntensity;            // rgb = colour, w = intensity multiplier
};

// Global uniform data shared across the entire frame.
struct GlobalUniformBuffer
{
    glm::mat4 View;                       // Camera view matrix
    glm::mat4 Projection;                 // Camera projection matrix
    glm::vec4 CameraPosition;             // World-space position of the camera (w unused)
    glm::vec4 AmbientColorIntensity;      // RGB ambient tint and scalar intensity in w
    glm::vec4 DirectionalLightDirection;  // Directional light forward vector (w unused)
    glm::vec4 DirectionalLightColor;      // Directional light colour and intensity in w
    glm::uvec4 LightCounts;               // x = directional count, y = point count, z/w reserved
    PointLightUniform PointLights[kMaxPointLights]; // Packed array of active point lights
};

// Material parameters consumed by the fragment shader.
struct MaterialUniformBuffer
{
    glm::vec4 BaseColorFactor;            // Base color multiplier from the material definition
    glm::vec4 MaterialFactors;            // x = metallic, y = roughness, z = ambient strength, w reserved
};