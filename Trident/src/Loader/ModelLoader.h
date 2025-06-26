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
            static Geometry::Mesh LoadOBJ(const std::string& filePath);
        };
    }
}