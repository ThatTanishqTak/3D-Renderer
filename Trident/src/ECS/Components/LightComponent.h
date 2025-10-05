#pragma once

#include <cstdint>

#include <glm/glm.hpp>

namespace Trident
{
    /**
     * Component describing how an entity contributes lighting to the scene.
     * - m_Type selects the light category (directional or point) so renderers can branch appropriately.
     * - m_Color stores the RGB tint applied to emitted light energy.
     * - m_Intensity scales the overall brightness without changing colour balance.
     * - m_Direction encodes the facing vector for directional lights (ignored for point lights).
     * - m_Range represents the effective radius for point lights (ignored for directional lights).
     * - m_Enabled allows editor tooling to toggle participation without deleting the component.
     * Future revisions can extend this component with shadow map handles or clustered-lighting metadata.
     */
    struct LightComponent
    {
        enum class Type : uint32_t
        {
            Directional = 0,
            Point = 1,
        };

        Type m_Type = Type::Directional;           ///< Light classification guiding shading logic.
        glm::vec3 m_Color{ 1.0f, 0.98f, 0.92f };   ///< Emissive colour applied to the light.
        float m_Intensity = 5.0f;                  ///< Scalar brightness multiplier.
        glm::vec3 m_Direction{ -0.5f, -1.0f, -0.3f }; ///< Facing vector for directional lights.
        float m_Range = 10.0f;                     ///< Influence radius for point lights (units in world space).
        bool m_Enabled = true;                     ///< Simple toggle so lights can be muted without deletion.
        bool m_ShadowCaster = false;               ///< Reserved for shadow settings integration.
        bool m_Reserved0 = false;                  ///< Padding + placeholder for clustered shading controls.
        bool m_Reserved1 = false;                  ///< Additional padding for std140 friendliness if promoted later.
    };
}