#pragma once

#include <cstdint>
#include <string>

namespace Trident
{
    /**
     * @brief Describes the texture bound to an entity-driven renderable.
     *
     * Entities store the texture path alongside the resolved renderer slot so edits performed in tooling can trigger
     * lazy reloads. The dirty flag allows the renderer and editor to coordinate reuploads without reloading the same
     * asset every frame. Future iterations can extend the component with sampling parameters or streaming references so
     * unused slots can be reclaimed automatically.
     */
    struct TextureComponent
    {
        /// Original authoring path that identifies the texture asset on disk.
        std::string m_TexturePath{};
        /// Renderer-managed slot index resolved from the texture cache. -1 indicates that no slot has been assigned yet.
        int32_t m_TextureSlot{ -1 };
        /// When true the renderer should attempt to resolve m_TexturePath again before issuing draw commands.
        bool m_IsDirty{ true };
    };
}