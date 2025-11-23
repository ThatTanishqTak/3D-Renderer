#pragma once

#include "Geometry/Material.h"

#include <vector>

namespace Trident
{
    namespace Geometry
    {
        struct Mesh
        {
            std::vector<uint32_t> Indices;
            int MaterialIndex = -1; // Index into the material table populated during loading (-1 when unassigned)
        };
    }
}