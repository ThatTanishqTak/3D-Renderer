#pragma once

#include <glm/glm.hpp>

// Global uniform data shared across the entire frame.
struct GlobalUniformBuffer
{
    glm::mat4 View;                       // Camera view matrix
    glm::mat4 Projection;                 // Camera projection matrix
    glm::vec4 CameraPosition;             // World-space position of the camera (w unused)
    glm::vec4 LightDirection;             // Directional light direction (w unused)
    glm::vec4 LightColorIntensity;        // RGB light color and scalar intensity in w
    glm::vec4 AmbientColorIntensity;      // RGB ambient tint and scalar intensity in w
};

// Material parameters consumed by the fragment shader.
struct MaterialUniformBuffer
{
    glm::vec4 BaseColorFactor;            // Base color multiplier from the material definition
    glm::vec4 MaterialFactors;            // x = metallic, y = roughness, z = ambient strength, w reserved
};