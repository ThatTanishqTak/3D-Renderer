#pragma once

#include "Geometry/Mesh.h"

#include <string>

namespace Trident
{
    namespace Loader
    {
        class ModelLoader
        {
        public:
            static Geometry::Mesh Load(const std::string& filePath);
        };
    }
}