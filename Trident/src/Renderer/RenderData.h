#pragma once

#include <glm/glm.hpp>

namespace Trident
{
    /**
     * @brief Push-constant payload shared by mesh and sprite draws.
     *
     * Keeping the structure here allows both the renderer and pipeline setup to agree on
     * the exact layout without hard-coding magic numbers. The struct is intentionally
     * compact (<= 128 bytes) so it remains compatible with Vulkan push constant limits.
     */
    struct alignas(16) RenderablePushConstant
    {
        glm::mat4 m_ModelMatrix{ 1.0f };   ///< Object to world transform.
        glm::vec4 m_TintColor{ 1.0f };     ///< Per-draw colour multiplier.
        glm::vec2 m_TextureScale{ 1.0f };  ///< UV scale applied in the shader.
        glm::vec2 m_TextureOffset{ 0.0f }; ///< UV offset supporting atlas layouts.
        float m_TilingFactor{ 1.0f };      ///< Additional tiling factor exposed to materials.
        int32_t m_TextureSlot{ 0 };        ///< Slot inside the renderer's texture array (0 == default white texture).
        int32_t m_UseMaterialOverride{ 0 };///< Non-zero when material overrides should be used.
        float m_SortBias{ 0.0f };          ///< Depth bias reserved for transparent layering.
        int32_t m_MaterialIndex{ -1 };     ///< Material lookup written per draw so the fragment shader can fetch shading data.
        int32_t m_Padding0{ 0 };           ///< Reserved for future expansion (keeps std140 alignment intact).
        int32_t m_Padding1{ 0 };           ///< Reserved for future expansion (keeps std140 alignment intact).
        int32_t m_Padding2{ 0 };           ///< Reserved for future expansion (keeps std140 alignment intact).
    };

    static_assert(sizeof(RenderablePushConstant) <= 128, "Push constant payload exceeds Vulkan limits");
}