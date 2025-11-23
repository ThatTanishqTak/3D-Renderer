#pragma once

#include "Geometry/Mesh.h"
#include "Geometry/Material.h"

#include <string>
#include <vector>

namespace Trident
{
    namespace Loader
    {
        struct SceneData
        {
            std::vector<Geometry::Mesh> Meshes;
            std::vector<Geometry::Material> Materials;
            size_t ModelCount = 0;
            size_t TriangleCount = 0;
        };

        class SceneLoader
        {
        public:
            static SceneData Load(const std::string& directoryPath);
        };
    }
}