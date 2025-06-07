#pragma once

#include "Renderer/Vertex.h"

#include <vector>

namespace Trident
{
    namespace Geometry
    {
        inline const std::vector<Vertex> CubeVertices =
        {
            { { -0.5f, -0.5f,  0.5f }, { 1.0f, 0.0f, 0.0f } },
            { {  0.5f, -0.5f,  0.5f }, { 0.0f, 1.0f, 0.0f } },
            { {  0.5f,  0.5f,  0.5f }, { 0.0f, 0.0f, 1.0f } },
            { { -0.5f,  0.5f,  0.5f }, { 1.0f, 1.0f, 1.0f } },

            { { -0.5f, -0.5f, -0.5f }, { 1.0f, 0.0f, 1.0f } },
            { {  0.5f, -0.5f, -0.5f }, { 0.0f, 1.0f, 1.0f } },
            { {  0.5f,  0.5f, -0.5f }, { 1.0f, 1.0f, 1.0f } },
            { { -0.5f,  0.5f, -0.5f }, { 1.0f, 1.0f, 1.0f } }
        };

        inline const std::vector<uint16_t> CubeIndices =
        {
            // Front
            0, 1, 2, 2, 3, 0,
            // Right
            1, 5, 6, 6, 2, 1,
            // Back
            5, 4, 7, 7, 6, 5,
            // Left
            4, 0, 3, 3, 7, 4,
            // Top
            3, 2, 6, 6, 7, 3,
            // Bottom
            4, 5, 1, 1, 0, 4
        };
    }
}