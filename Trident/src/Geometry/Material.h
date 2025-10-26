#pragma once

#include <glm/glm.hpp>

namespace Trident
{
    namespace Geometry
    {
        // Simple material description used by the renderer to evaluate glTF PBR data
        struct Material
        {
            glm::vec4 BaseColorFactor{ 1.0f, 1.0f, 1.0f, 1.0f };   // RGBA tint coming from glTF's baseColorFactor
            float MetallicFactor = 1.0f;                           // Metallic scalar used for PBR shading
            float RoughnessFactor = 1.0f;                          // Roughness scalar used for PBR shading
            int BaseColorTextureIndex = -1;                        // Index of the base color texture in the glTF texture array
            int BaseColorTextureSlot = 0;                          // Resolved GPU texture slot (0 always points at the fallback white texture)
            int MetallicRoughnessTextureIndex = -1;                // Index of the metallic-roughness texture (if any)
            int NormalTextureIndex = -1;                           // Optional normal map texture index
        };
    }
}