#pragma once

#include "Geometry/Mesh.h"
#include "Geometry/Material.h"

#include <string>
#include <vector>

namespace Trident
{
    namespace Loader
    {
        struct ModelData
        {
            std::vector<Geometry::Mesh> Meshes;        // Geometry buffers extracted from a glTF document
            std::vector<Geometry::Material> Materials; // Companion material table referenced by Mesh::MaterialIndex
        };

        class ModelLoader
        {
        public:
            // Load a glTF model and return its meshes together with their material table
            static ModelData Load(const std::string& filePath);
        };
    }
}