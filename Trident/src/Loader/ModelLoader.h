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
            std::vector<Geometry::Mesh> Meshes;         // Geometry buffers extracted from the source document
            std::vector<Geometry::Material> Materials; // Companion material table referenced by Mesh::MaterialIndex
            std::vector<std::string> Textures;         // Normalized texture paths shared by materials in the same order as their indices
        };

        class ModelLoader
        {
        public:
            // Load a model (currently supports glTF/glb/FBX) and return its meshes together with their material table
            static ModelData Load(const std::string& filePath);
        };
    }
}