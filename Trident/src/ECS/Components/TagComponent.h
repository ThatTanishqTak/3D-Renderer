#pragma once

#include <string>

namespace Trident
{
    /**
     * @brief Lightweight identifier shared between editor tooling and serialization.
     *
     * The tag component stores a display name for an entity so scenes saved to disk
     * and runtime debugging overlays can present readable labels. The string is
     * intentionally small in scope—future revisions might extend this with unique
     * identifiers or localisation tokens but the current implementation keeps
     * serialization straightforward.
     */
    struct TagComponent
    {
        /// Human readable label that appears in hierarchy panels and logs.
        std::string m_Tag{ "Entity" };
    };
}