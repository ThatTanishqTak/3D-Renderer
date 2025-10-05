#pragma once

#include <glm/glm.hpp>

namespace Trident
{
    /**
     * @brief Basic spatial component shared across the engine.
     *
     * The transform stores position, Euler rotation (in degrees), and scale values
     * for an entity. Systems compose these vectors into matrices when needed,
     * keeping the component compact and easy to serialize. Future revisions could
     * cache the composed matrix or track a dirty flag to minimise recalculations
     * in large scenes.
     */
    struct Transform
    {
        /// World-space translation applied to the entity.
        glm::vec3 Position{ 0.0f };
        /// XYZ Euler rotation in degrees to align with authoring expectations.
        glm::vec3 Rotation{ 0.0f };
        /// Non-uniform scaling factor for each axis.
        glm::vec3 Scale{ 1.0f };
    };
}