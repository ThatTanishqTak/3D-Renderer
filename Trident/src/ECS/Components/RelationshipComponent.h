#pragma once

#include "ECS/Entity.h"

#include <vector>
#include <limits>

namespace Trident
{
    namespace ECS
    {
        /**
         * @brief Component that tracks parent/child entity relationships for hierarchical transforms.
         *
         * The registry uses the stored parent identifier to rebuild child vectors and propagate
         * transform updates through the entity tree. The sentinel value returned by
         * GetInvalidEntity() represents a detached/root entity. Future revisions can extend the
         * component with sibling iteration helpers or metadata for scene graph tools.
         */
        struct RelationshipComponent
        {
            static constexpr Entity GetInvalidEntity()
            {
                return std::numeric_limits<Entity>::max();
            }

            Entity m_Parent{ GetInvalidEntity() };          ///< Parent entity or GetInvalidEntity() when detached.
            std::vector<Entity> m_Children;                 ///< Cached child list to accelerate hierarchy traversals.
        };
    }
}