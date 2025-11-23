#pragma once

#include "Animation/AnimationData.h"
#include "Geometry/Material.h"
#include "Geometry/Mesh.h"

#include <glm/mat4x4.hpp>

#include <limits>
#include <string>
#include <vector>

namespace Trident
{
    namespace Loader
    {
        /**
         * @brief Describes a mesh instance recorded in the source document.
         * Stores the mesh index alongside the baked transform and author-provided node name.
         */
        struct MeshInstance
        {
            size_t m_MeshIndex = std::numeric_limits<size_t>::max(); //!< Index into ModelData::m_Meshes resolved during import.
            glm::mat4 m_ModelMatrix{ 1.0f };                         //!< Transform baked from the Assimp scene graph.
            std::string m_NodeName;                                  //!< Original node identifier for editor display/debugging.
        };

        /**
         * @brief Aggregates the data extracted while importing a model.
         * Meshes, materials, textures, skeleton information, and animation clips are stored together so callers can consume the
         * subset they require.
         */
        struct ModelData
        {
            std::vector<Geometry::Mesh> m_Meshes;                        //!< Geometry buffers ready for GPU upload.
            std::vector<Geometry::Material> m_Materials;                 //!< PBR material descriptions.
            std::vector<std::string> m_Textures;                         //!< Normalised texture paths referenced by materials.
            std::vector<MeshInstance> m_MeshInstances;                   //!< Optional mesh placements sourced from the scene graph.
            Animation::Skeleton m_Skeleton;                              //!< Skeleton describing the bone hierarchy.
            std::vector<Animation::AnimationClip> m_AnimationClips;      //!< Animation clips authored in the source asset.
        };

        /**
         * @brief Loads supported 3D assets using Assimp and returns the converted model data.
         */
        class ModelLoader
        {
        public:
            static ModelData Load(const std::string& filePath);
        };
    }
}