#pragma once

#include "Geometry/Mesh.h"
#include "Geometry/Material.h"
#include "Animation/AnimationData.h"

#include <limits>
#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace Trident
{
    namespace Loader
    {
        /**
         * @brief Placement information that describes how a mesh is instanced in the source scene.
         *
         * The model loader keeps geometry buffers and their transforms separate so that multiple
         * nodes in the Assimp scene graph can reference the same mesh without duplicating vertex
         * data. Each instance records the resolved mesh index, the world transform composed while
         * traversing the node hierarchy, and the original node name for editor tooling. Future
         * iterations could extend this to capture parent/child relationships or visibility flags.
         */
        struct MeshInstance
        {
            size_t m_MeshIndex{ std::numeric_limits<size_t>::max() };  // Index into ModelData::m_Meshes
            glm::mat4 m_ModelMatrix{ 1.0f };                           // World transform accumulated from the node hierarchy
            std::string m_NodeName{};                                  // Authoring name to aid debugging and tagging
        };

        struct ModelData
        {
            std::vector<Geometry::Mesh> m_Meshes;                  // Geometry buffers extracted from the source document
            std::vector<Geometry::Material> m_Materials;           // Companion material table referenced by Mesh::MaterialIndex
            std::vector<std::string> m_Textures;                   // Normalized texture paths shared by materials in the same order as their indices
            Animation::Skeleton m_Skeleton;                        // Skeleton hierarchy constructed from the imported bones
            std::vector<Animation::AnimationClip> m_AnimationClips;// Animation clips parsed from the asset for runtime playback
            std::vector<MeshInstance> m_MeshInstances;             // Node-instanced meshes with their accumulated transforms
        };

        class ModelLoader
        {
        public:
            // Load a model (currently supports glTF/glb/FBX) and return its meshes together with their material table
            static ModelData Load(const std::string& filePath);
        };
    }
}