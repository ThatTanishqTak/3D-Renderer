#pragma once

#include <string>

#include <glm/glm.hpp>

namespace Trident
{
    /**
     * @brief Describes the visual parameters required to render a 2D sprite.
     *
     * The component keeps the authoring data lightweight so it can be serialised easily
     * while still giving the renderer enough information to batch sprites efficiently.
     * Future iterations can expand the atlas and animation fields to stream flipbooks
     * or integrate with a dedicated animation graph.
     */
    struct SpriteComponent
    {
        /// Identifier or absolute path used to resolve the sprite texture asset.
        std::string m_TextureId{};
        /// Colour multiplier applied in the shader so artists can tint sprites at runtime.
        glm::vec4 m_TintColor{ 1.0f };
        /// UV scaling applied before sampling to support simple atlas layouts.
        glm::vec2 m_UVScale{ 1.0f };
        /// UV offset in the texture, allowing sprites to address atlas regions directly.
        glm::vec2 m_UVOffset{ 0.0f };
        /// Scalar tiling factor forwarded to shaders for repeating textures.
        float m_TilingFactor{ 1.0f };
        /// Toggle to quickly hide sprites in editor viewports without removing the component.
        bool m_Visible{ true };
        /// When true the renderer should favour the material override instead of the default sampler.
        bool m_UseMaterialOverride{ false };
        /// Optional material identifier so the sprite can borrow advanced shading settings later.
        std::string m_MaterialOverrideId{};
        /// Grid dimensions describing how many tiles exist in the bound atlas texture.
        glm::ivec2 m_AtlasTiles{ 1, 1 };
        /// Index of the active cell inside the atlas; future systems can animate this value.
        int m_AtlasIndex{ 0 };
        /// Playback rate for atlas based animations, stored for future timeline integrations.
        float m_AnimationSpeed{ 0.0f };
        /// Depth bias applied during sorting so designers can layer sprites without adjusting transforms.
        float m_SortOffset{ 0.0f };
    };
}