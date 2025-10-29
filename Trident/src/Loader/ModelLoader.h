#pragma once

#include "Geometry/Mesh.h"
#include "Geometry/Material.h"
#include "Animation/AnimationData.h"

#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>
#include <variant>
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

        /**
         * @brief Represents a metadata value extracted from Assimp/FBX.
         *
         * Assimp exposes metadata as a loose set of key/value pairs with primitive
         * payloads.  The loader retains every value using a variant so editor tools
         * can surface the original authoring information even if the renderer does
         * not currently consume it.
         */
        struct MetadataValue
        {
            enum class Type
            {
                Boolean,
                Int32,
                UInt64,
                Float,
                Double,
                String,
                Vector3,
                Color4,
                Binary
            };

            Type m_Type{ Type::Binary };                                        // Type of the stored payload.
            std::variant<std::monostate, bool, int32_t, uint64_t, float, double, std::string, glm::vec3, glm::vec4> m_Data{}; // Primary decoded data when possible.
            std::vector<uint8_t> m_RawData{};                                   // Raw bytes captured for unsupported or future data types.
        };

        /**
         * @brief Convenience structure mapping a metadata key to its decoded value.
         */
        struct MetadataEntry
        {
            std::string m_Key;                                                  // Name exposed by the FBX document.
            MetadataValue m_Value;                                              // Fully preserved metadata payload.
        };

        /**
         * @brief Captures the entire node hierarchy so tooling can reconstruct the
         * original scene organisation.
         */
        struct SceneNode
        {
            std::string m_Name;                                                 // Node display name.
            glm::mat4 m_LocalTransform{ 1.0f };                                 // Local transform relative to the parent.
            glm::mat4 m_GlobalTransform{ 1.0f };                                // Absolute transform composed during load.
            int m_ParentIndex{ -1 };                                            // Parent node index (-1 when root).
            std::vector<size_t> m_Children{};                                   // Children indices for hierarchy traversal.
            std::vector<unsigned int> m_MeshIndices{};                          // Mesh indices referenced by this node.
            std::vector<MetadataEntry> m_Metadata{};                            // Authoring metadata values attached to the
            // node (FBX custom properties, etc.).
        };

        /**
         * @brief Describes every texture reference declared on an FBX material.
         */
        struct TextureReference
        {
            std::string m_TexturePath;                                          // Resolved file path or embedded identifier.
            int m_AssimpType{ 0 };                                              // aiTextureType value retained for debugging.
            unsigned int m_TextureIndex{ 0 };                                   // Texture slot index within the aiMaterial.
            unsigned int m_UVChannel{ 0 };                                      // UV set referenced by this texture.
            float m_BlendFactor{ 1.0f };                                        // Blend weight supplied by the authoring tool.
            int m_WrapModeU{ 0 };                                               // aiTextureMapMode for the U axis.
            int m_WrapModeV{ 0 };                                               // aiTextureMapMode for the V axis.
            int m_WrapModeW{ 0 };                                               // aiTextureMapMode for the W axis (3D textures).
            bool m_IsEmbedded{ false };                                         // True when the texture is embedded in the FBX.
        };

        /**
         * @brief Stores the raw aiMaterial properties so specialised pipelines can
         * reconstruct shading models beyond the default PBR approximation.
         */
        struct MaterialProperty
        {
            std::string m_Key;                                                  // Property key provided by Assimp.
            unsigned int m_Semantic{ 0 };                                       // Semantic value (e.g. texture type).
            unsigned int m_Index{ 0 };                                          // Texture index for keyed properties.
            int m_Type{ 0 };                                                    // aiPropertyTypeInfo enumerant retained.
            std::vector<uint8_t> m_Data{};                                      // Raw property payload copied verbatim.
        };

        /**
         * @brief Collects FBX specific information that accompanies a converted material.
         */
        struct MaterialExtra
        {
            std::vector<TextureReference> m_Textures{};                         // Complete texture table including embedded
            // sources and UV routing.
            std::vector<MaterialProperty> m_Properties{};                       // All raw aiMaterial properties.
            std::vector<MetadataEntry> m_Metadata{};                            // Metadata extracted from the material.
        };

        /**
         * @brief Stores additional vertex channels and morph targets that exist on
         * the source FBX mesh.
         */
        struct MeshExtra
        {
            std::string m_Name;                                                 // Mesh name retained for debugging.
            unsigned int m_PrimitiveTypes{ 0 };                                 // Bitmask describing primitive topology.
            std::vector<std::vector<glm::vec3>> m_AdditionalTexCoords{};        // UV channels beyond the first.
            std::vector<unsigned int> m_TexCoordComponentCounts{};              // Component count for each stored UV channel.
            std::vector<std::vector<glm::vec4>> m_VertexColorSets{};            // All authored vertex colour sets.

            struct MorphTarget
            {
                std::string m_Name;                                             // Name of the morph target (if any).
                std::vector<glm::vec3> m_Positions{};                           // Position deltas stored by the FBX.
                std::vector<glm::vec3> m_Normals{};                             // Normal deltas stored by the FBX.
                std::vector<glm::vec3> m_Tangents{};                            // Tangent deltas (when supplied).
            };

            std::vector<MorphTarget> m_MorphTargets{};                          // Blend shape/morph target data.
        };

        /**
         * @brief Represents an embedded texture payload found inside the FBX document.
         */
        struct EmbeddedTexture
        {
            std::string m_Name;                                                 // Assimp identifier (e.g. "*0").
            std::vector<uint8_t> m_Data{};                                      // Raw byte contents.
            unsigned int m_Width{ 0 };                                          // Width in pixels for uncompressed payloads.
            unsigned int m_Height{ 0 };                                         // Height in pixels for uncompressed payloads.
            bool m_IsCompressed{ false };                                       // True when the payload stores a compressed image.
        };

        /**
         * @brief Captures the properties of an FBX camera for offline tools and debugging.
         */
        struct CameraData
        {
            std::string m_Name;                                                 // Camera name in the source document.
            glm::mat4 m_NodeTransform{ 1.0f };                                  // World transform resolved from the node hierarchy.
            glm::vec3 m_Position{ 0.0f };                                       // Local camera position.
            glm::vec3 m_Up{ 0.0f, 1.0f, 0.0f };                                 // Local up vector.
            glm::vec3 m_LookAt{ 0.0f, 0.0f, -1.0f };                            // Local look-at vector.
            float m_HorizontalFov{ 0.0f };                                      // Horizontal field of view in radians.
            float m_Aspect{ 1.0f };                                             // Aspect ratio when provided.
            float m_NearClip{ 0.0f };                                           // Near clip plane distance.
            float m_FarClip{ 0.0f };                                            // Far clip plane distance.
            float m_OrthographicWidth{ 0.0f };                                  // Width for orthographic cameras.
        };

        /**
         * @brief Captures an FBX light definition.
         */
        struct LightData
        {
            std::string m_Name;                                                 // Light name as authored.
            glm::mat4 m_NodeTransform{ 1.0f };                                  // World transform resolved from the scene graph.
            int m_Type{ 0 };                                                    // aiLightSourceType retained for inspection.
            glm::vec3 m_ColorDiffuse{ 1.0f };                                   // Diffuse colour contribution.
            glm::vec3 m_ColorSpecular{ 1.0f };                                  // Specular colour contribution.
            glm::vec3 m_ColorAmbient{ 0.0f };                                   // Ambient colour contribution.
            float m_AttenuationConstant{ 1.0f };                                // Constant attenuation term.
            float m_AttenuationLinear{ 0.0f };                                  // Linear attenuation term.
            float m_AttenuationQuadratic{ 0.0f };                               // Quadratic attenuation term.
            float m_InnerConeAngle{ 0.0f };                                     // Spotlight inner cone angle.
            float m_OuterConeAngle{ 0.0f };                                     // Spotlight outer cone angle.
        };

        /**
         * @brief Represents mesh selection animation channels and morph weight keys.
         */
        struct AnimationExtra
        {
            struct MeshChannelKey
            {
                float m_TimeSeconds{ 0.0f };                                    // Timestamp in seconds.
                unsigned int m_Value{ 0 };                                      // Mesh index referenced by the key.
            };

            struct MeshChannel
            {
                std::string m_Name;                                             // Channel name in the animation.
                unsigned int m_MeshId{ 0 };                                     // Mesh identifier targeted by the channel.
                std::vector<MeshChannelKey> m_Keys{};                           // Mesh selection keyframes.
            };

            struct MorphWeightKey
            {
                float m_TimeSeconds{ 0.0f };                                    // Timestamp in seconds.
                std::vector<unsigned int> m_Values{};                           // Morph target indices.
                std::vector<float> m_Weights{};                                 // Weight values per morph target.
            };

            struct MorphChannel
            {
                std::string m_Name;                                             // Morph channel name.
                unsigned int m_MeshId{ 0 };                                     // Mesh identifier targeted by the channel.
                std::vector<MorphWeightKey> m_Keys{};                           // Weight keyframes for the morph target.
            };

            uint32_t m_Flags{ 0u };                                             // aiAnimation::mFlags retained for debugging.
            std::vector<MeshChannel> m_MeshChannels{};                          // Mesh channel animation data.
            std::vector<MorphChannel> m_MorphChannels{};                        // Morph weight animation data.
        };

        struct ModelData
        {
            std::vector<Geometry::Mesh> m_Meshes;                  // Geometry buffers extracted from the source document
            std::vector<Geometry::Material> m_Materials;           // Companion material table referenced by Mesh::MaterialIndex
            std::vector<std::string> m_Textures;                   // Normalized texture paths shared by materials in the same order as their indices
            Animation::Skeleton m_Skeleton;                        // Skeleton hierarchy constructed from the imported bones
            std::vector<Animation::AnimationClip> m_AnimationClips;// Animation clips parsed from the asset for runtime playback
            std::vector<MeshInstance> m_MeshInstances;             // Node-instanced meshes with their accumulated transforms
            std::vector<MeshExtra> m_MeshExtras;                   // Rich mesh information (UV sets, colours, morph targets)
            std::vector<MaterialExtra> m_MaterialExtras;           // Raw material properties and texture declarations
            std::vector<SceneNode> m_SceneNodes;                   // Full FBX node hierarchy
            std::unordered_map<std::string, size_t> m_NodeNameToIndex; // Quick lookup to locate nodes by name
            std::vector<MetadataEntry> m_SceneMetadata;            // Scene-level metadata preserved from the FBX
            std::vector<EmbeddedTexture> m_EmbeddedTextures;       // Embedded texture payloads copied from the document
            std::vector<CameraData> m_Cameras;                     // Cameras declared in the FBX scene
            std::vector<LightData> m_Lights;                       // Lights declared in the FBX scene
            std::vector<AnimationExtra> m_AnimationExtras;         // Additional animation channels (mesh/morph weights)
        };

        class ModelLoader
        {
        public:
            // Load a model (currently supports glTF/glb/FBX) and return its meshes together with their material table
            static ModelData Load(const std::string& filePath);
        };
    }
}