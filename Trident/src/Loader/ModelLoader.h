#pragma once

#include "Geometry/Mesh.h"
#include "Geometry/Material.h"
#include "Animation/AnimationData.h"

#include <string>
#include <vector>

namespace Trident
{
    namespace Loader
    {
        struct ModelData
        {
            std::vector<Geometry::Mesh> m_Meshes;                  // Geometry buffers extracted from the source document
            std::vector<Geometry::Material> m_Materials;           // Companion material table referenced by Mesh::MaterialIndex
            std::vector<std::string> m_Textures;                   // Normalized texture paths shared by materials in the same order as their indices
            Animation::Skeleton m_Skeleton;                        // Skeleton hierarchy constructed from the imported bones
            std::vector<Animation::AnimationClip> m_AnimationClips;// Animation clips parsed from the asset for runtime playback
        };

        class ModelLoader
        {
        public:
            // Load a model (currently supports glTF/glb/FBX) and return its meshes together with their material table
            static ModelData Load(const std::string& filePath);
        };
    }
}