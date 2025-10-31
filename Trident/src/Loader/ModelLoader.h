#pragma once

#include "Animation/AnimationData.h"
#include "Loader/ModelSource.h"
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
         * @brief Stores placement metadata for meshes instantiated within a model hierarchy.
         */
        struct MeshInstance
        {
            size_t m_MeshIndex = std::numeric_limits<size_t>::max();   //!< Index into the mesh array produced by the importer.
            glm::mat4 m_ModelMatrix{ 1.0f };                           //!< Baked model transform (including parent hierarchy).
            std::string m_NodeName;                                    //!< Original node name for editor-facing diagnostics.
        };

        /**
         * @brief Aggregates the data required by the renderer and animation systems after import.
         */
        struct ModelData
        {
            std::vector<Geometry::Mesh> m_Meshes{};                    //!< Geometry generated from the imported asset.
            std::vector<Geometry::Material> m_Materials{};             //!< PBR material descriptions authored in the asset.
            std::vector<std::string> m_Textures{};                     //!< Texture paths referenced by the materials.
            std::vector<MeshInstance> m_MeshInstances{};               //!< Transform instances resolved from the node graph.
            Animation::Skeleton m_Skeleton{};                          //!< Extracted skeleton hierarchy (if present).
            std::vector<Animation::AnimationClip> m_AnimationClips{};  //!< Animation clips baked from the asset.
            std::string m_SourceIdentifier{};                          //!< Logical identifier for provenance and logging.
        };

        class ModelLoader
        {
        public:
            /// @brief Convenience overload preserving the legacy file-based API.
            static ModelData Load(const std::string& filePath);

            /// @brief Import a model from an arbitrary data source.
            static ModelData Load(const ModelSource& source);
        };
    }
}