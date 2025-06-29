#pragma once

#include "Geometry/Mesh.h"

#include <string>
#include <vector>

namespace Trident
{
    namespace Loader
    {
        class ModelLoader
        {
        public:
            static std::vector<Geometry::Mesh> Load(const std::string& filePath);
        };
    }
}