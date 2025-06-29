#pragma once

#include "Renderer/Vertex.h"

#include <vector>

namespace Trident
{
    namespace Geometry
    {
        struct Mesh
        {
            std::vector<Vertex> Vertices;
            std::vector<uint32_t> Indices;
        };
    }
}