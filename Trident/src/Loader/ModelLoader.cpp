#include "Loader/ModelLoader.h"

#include "Core/Utilities.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/color4.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Trident
{
    namespace Loader
    {
        namespace
        {
            const glm::vec3 s_DefaultNormal{ 0.0f, 1.0f, 0.0f };
            const glm::vec3 s_DefaultTangent{ 1.0f, 0.0f, 0.0f };
            const glm::vec3 s_DefaultBitangent{ 0.0f, 0.0f, 1.0f };
            const glm::vec3 s_DefaultColor{ 1.0f, 1.0f, 1.0f };
            const glm::vec2 s_DefaultTexCoord{ 0.0f, 0.0f };

            Loader::MetadataValue ConvertMetadataEntry(const aiMetadataEntry& sourceEntry)
            {
                Loader::MetadataValue l_MetadataValue{};

                if (!sourceEntry.mData)
                {
                    return l_MetadataValue;
                }

                switch (sourceEntry.mType)
                {
                case AI_BOOL:
                {
                    l_MetadataValue.m_Type = Loader::MetadataValue::Type::Boolean;
                    l_MetadataValue.m_Data = *static_cast<bool*>(sourceEntry.mData);
                    break;
                }
                case AI_INT32:
                {
                    l_MetadataValue.m_Type = Loader::MetadataValue::Type::Int32;
                    l_MetadataValue.m_Data = *static_cast<int32_t*>(sourceEntry.mData);
                    break;
                }
                case AI_UINT64:
                {
                    l_MetadataValue.m_Type = Loader::MetadataValue::Type::UInt64;
                    l_MetadataValue.m_Data = *static_cast<uint64_t*>(sourceEntry.mData);
                    break;
                }
                case AI_FLOAT:
                {
                    l_MetadataValue.m_Type = Loader::MetadataValue::Type::Float;
                    l_MetadataValue.m_Data = *static_cast<float*>(sourceEntry.mData);
                    break;
                }
                case AI_DOUBLE:
                {
                    l_MetadataValue.m_Type = Loader::MetadataValue::Type::Double;
                    l_MetadataValue.m_Data = *static_cast<double*>(sourceEntry.mData);
                    break;
                }
                case AI_AISTRING:
                {
                    const aiString* l_String = static_cast<aiString*>(sourceEntry.mData);
                    l_MetadataValue.m_Type = Loader::MetadataValue::Type::String;
                    l_MetadataValue.m_Data = std::string(l_String ? l_String->C_Str() : "");
                    break;
                }
                case AI_AIVECTOR3D:
                {
                    const aiVector3D* l_Vector = static_cast<aiVector3D*>(sourceEntry.mData);
                    if (l_Vector)
                    {
                        l_MetadataValue.m_Type = Loader::MetadataValue::Type::Vector3;
                        l_MetadataValue.m_Data = glm::vec3(l_Vector->x, l_Vector->y, l_Vector->z);
                    }
                    break;
                }
                case AI_AICOLOR4D:
                {
                    const aiColor4D* l_Color = static_cast<aiColor4D*>(sourceEntry.mData);
                    if (l_Color)
                    {
                        l_MetadataValue.m_Type = Loader::MetadataValue::Type::Color4;
                        l_MetadataValue.m_Data = glm::vec4(l_Color->r, l_Color->g, l_Color->b, l_Color->a);
                    }
                    break;
                }
                default:
                {
                    l_MetadataValue.m_Type = Loader::MetadataValue::Type::Binary;
                    break;
                }
                }

                return l_MetadataValue;
            }

            void ExtractMetadata(const aiMetadata* sourceMetaData, std::vector<Loader::MetadataEntry>& target)
            {
                if (!sourceMetaData)
                {
                    return;
                }

                target.reserve(target.size() + sourceMetaData->mNumProperties);
                for (unsigned int it_Property = 0; it_Property < sourceMetaData->mNumProperties; ++it_Property)
                {
                    const aiString& l_Key = sourceMetaData->mKeys[it_Property];
                    const aiMetadataEntry& l_Entry = sourceMetaData->mValues[it_Property];

                    Loader::MetadataEntry l_Metadata{};
                    l_Metadata.m_Key = l_Key.C_Str();
                    l_Metadata.m_Value = ConvertMetadataEntry(l_Entry);

                    target.emplace_back(std::move(l_Metadata));
                }
            }

            Loader::EmbeddedTexture ExtractEmbeddedTexture(const aiTexture* texture, const std::string& name)
            {
                Loader::EmbeddedTexture l_Texture{};
                l_Texture.m_Name = name;

                if (!texture)
                {
                    return l_Texture;
                }

                if (texture->mHeight == 0)
                {
                    l_Texture.m_IsCompressed = true;
                    const size_t l_Size = static_cast<size_t>(texture->mWidth);
                    l_Texture.m_Data.resize(l_Size);
                    if (l_Size > 0 && texture->pcData)
                    {
                        std::memcpy(l_Texture.m_Data.data(), texture->pcData, l_Size);
                    }
                }
                else
                {
                    l_Texture.m_IsCompressed = false;
                    l_Texture.m_Width = texture->mWidth;
                    l_Texture.m_Height = texture->mHeight;
                    const size_t l_Size = static_cast<size_t>(texture->mWidth) * static_cast<size_t>(texture->mHeight) * sizeof(aiTexel);
                    l_Texture.m_Data.resize(l_Size);
                    if (l_Size > 0 && texture->pcData)
                    {
                        std::memcpy(l_Texture.m_Data.data(), texture->pcData, l_Size);
                    }
                }

                return l_Texture;
            }

            glm::mat4 ConvertMatrix(const aiMatrix4x4& source)
            {
                glm::mat4 l_Target{};
                l_Target[0][0] = source.a1; l_Target[1][0] = source.a2; l_Target[2][0] = source.a3; l_Target[3][0] = source.a4;
                l_Target[0][1] = source.b1; l_Target[1][1] = source.b2; l_Target[2][1] = source.b3; l_Target[3][1] = source.b4;
                l_Target[0][2] = source.c1; l_Target[1][2] = source.c2; l_Target[2][2] = source.c3; l_Target[3][2] = source.c4;
                l_Target[0][3] = source.d1; l_Target[1][3] = source.d2; l_Target[2][3] = source.d3; l_Target[3][3] = source.d4;
                return l_Target;
            }

            std::string NormalizeMixamoName(const std::string& sourceName)
            {
                std::string l_Normalized = sourceName;
                const std::string l_MixamoPrefix = "mixamorig:";
                if (l_Normalized.rfind(l_MixamoPrefix, 0) == 0)
                {
                    l_Normalized = l_Normalized.substr(l_MixamoPrefix.size());
                }

                std::string l_Lowercase = l_Normalized;
                std::transform(l_Lowercase.begin(), l_Lowercase.end(), l_Lowercase.begin(), [](unsigned char character)
                    {
                        return static_cast<char>(std::tolower(character));
                    });

                static const std::unordered_map<std::string, std::string> s_MixamoCanonicalNames =
                {
                    { "hips", "Hips" },
                    { "spine", "Spine" },
                    { "spine1", "Spine1" },
                    { "spine2", "Spine2" },
                    { "neck", "Neck" },
                    { "head", "Head" },
                    { "leftshoulder", "LeftShoulder" },
                    { "leftarm", "LeftArm" },
                    { "leftforearm", "LeftForeArm" },
                    { "lefthand", "LeftHand" },
                    { "rightshoulder", "RightShoulder" },
                    { "rightarm", "RightArm" },
                    { "rightforearm", "RightForeArm" },
                    { "righthand", "RightHand" },
                    { "leftupleg", "LeftUpLeg" },
                    { "leftleg", "LeftLeg" },
                    { "leftfoot", "LeftFoot" },
                    { "lefttoe", "LeftToeBase" },
                    { "rightupleg", "RightUpLeg" },
                    { "rightleg", "RightLeg" },
                    { "rightfoot", "RightFoot" },
                    { "righttoe", "RightToeBase" }
                };

                auto a_Found = s_MixamoCanonicalNames.find(l_Lowercase);
                if (a_Found != s_MixamoCanonicalNames.end())
                {
                    return a_Found->second;
                }

                return l_Normalized;
            }
        }

        ModelData ModelLoader::Load(const std::string& filePath)
        {
            ModelData l_ModelData{};
            std::string l_NormalizedPath = Utilities::FileManagement::NormalizePath(filePath);
            if (l_NormalizedPath.empty())
            {
                TR_CORE_CRITICAL("Provided model path is empty");
                return l_ModelData;
            }

            Assimp::Importer l_Importer{};
            const unsigned int l_PostProcessFlags =
                aiProcess_Triangulate |          // Ensure all faces are triangles so the renderer can index predictably
                aiProcess_GenSmoothNormals |     // Generate smooth normals when the source omits them
                aiProcess_CalcTangentSpace |     // Derive tangents and bitangents for normal mapping support
                aiProcess_JoinIdenticalVertices |
                aiProcess_ImproveCacheLocality |
                aiProcess_RemoveRedundantMaterials |
                aiProcess_GenUVCoords |
                aiProcess_TransformUVCoords |
                aiProcess_SortByPType;

            const aiScene* l_Scene = l_Importer.ReadFile(l_NormalizedPath, l_PostProcessFlags);
            if (!l_Scene)
            {
                TR_CORE_CRITICAL("Assimp failed to load model '{}': {}", filePath, l_Importer.GetErrorString());
                return l_ModelData;
            }

            if (!l_Scene->mRootNode)
            {
                TR_CORE_CRITICAL("Model '{}' does not contain a root node", filePath);
                return l_ModelData;
            }

            std::string l_BaseDirectory = Utilities::FileManagement::GetBaseDirectory(l_NormalizedPath);
            std::unordered_map<std::string, int> l_TextureLookup{};
            std::unordered_map<const aiMesh*, size_t> l_MeshCache{}; // Tracks converted meshes to avoid duplicating geometry buffers
            std::unordered_map<std::string, size_t> l_EmbeddedTextureLookup{};   // Embedded payload lookup for FBX textures
            Animation::Skeleton& l_Skeleton = l_ModelData.m_Skeleton;

            // Reserve mesh storage up front so instancing can reference stable indices created during traversal.
            l_ModelData.m_Meshes.reserve(l_Scene->mNumMeshes);
            l_ModelData.m_MeshInstances.reserve(l_Scene->mNumMeshes);
            l_ModelData.m_MeshExtras.reserve(l_Scene->mNumMeshes);

            ExtractMetadata(l_Scene->mMetaData, l_ModelData.m_SceneMetadata);

            if (l_Scene->mNumTextures > 0)
            {
                l_ModelData.m_EmbeddedTextures.reserve(l_Scene->mNumTextures);
                for (unsigned int it_Texture = 0; it_Texture < l_Scene->mNumTextures; ++it_Texture)
                {
                    const aiTexture* l_Embedded = l_Scene->mTextures[it_Texture];
                    if (!l_Embedded)
                    {
                        continue;
                    }

                    std::string l_TextureName = l_Embedded->mFilename.length > 0 ? l_Embedded->mFilename.C_Str() : ("*" + std::to_string(it_Texture));

                    Loader::EmbeddedTexture l_EmbeddedTexture = ExtractEmbeddedTexture(l_Embedded, l_TextureName);
                    const size_t l_EmbeddedIndex = l_ModelData.m_EmbeddedTextures.size();
                    l_ModelData.m_EmbeddedTextures.push_back(std::move(l_EmbeddedTexture));
                    l_EmbeddedTextureLookup.emplace(std::move(l_TextureName), l_EmbeddedIndex);
                }
            }

            const std::function<int(const std::string&, const aiNode*, const glm::mat4&, bool)> l_AcquireBoneIndex =
                [&](const std::string& sourceName, const aiNode* assimpNode, const glm::mat4& inverseBindMatrix, bool updateInverseBind) -> int
                {
                    const std::string l_NormalizedName = NormalizeMixamoName(sourceName);
                    int l_BoneIndex = -1;

                    auto a_Existing = l_Skeleton.m_NameToIndex.find(l_NormalizedName);
                    if (a_Existing == l_Skeleton.m_NameToIndex.end())
                    {
                        Animation::Bone l_Bone{};
                        l_Bone.m_Name = l_NormalizedName;
                        l_Bone.m_SourceName = sourceName;
                        l_Bone.m_InverseBindMatrix = inverseBindMatrix;
                        if (assimpNode)
                        {
                            l_Bone.m_LocalBindTransform = ConvertMatrix(assimpNode->mTransformation);
                        }

                        l_Skeleton.m_Bones.push_back(l_Bone);
                        l_BoneIndex = static_cast<int>(l_Skeleton.m_Bones.size()) - 1;
                        l_Skeleton.m_NameToIndex.emplace(l_NormalizedName, l_BoneIndex);
                        l_Skeleton.m_SourceNameToIndex.emplace(sourceName, l_BoneIndex);
                    }
                    else
                    {
                        l_BoneIndex = a_Existing->second;
                        Animation::Bone& l_Bone = l_Skeleton.m_Bones[l_BoneIndex];
                        l_Bone.m_SourceName = sourceName;
                        if (updateInverseBind)
                        {
                            l_Bone.m_InverseBindMatrix = inverseBindMatrix;
                        }
                        if (assimpNode)
                        {
                            l_Bone.m_LocalBindTransform = ConvertMatrix(assimpNode->mTransformation);
                        }
                        l_Skeleton.m_SourceNameToIndex.emplace(sourceName, l_BoneIndex);
                    }

                    const aiNode* l_BoneNode = assimpNode;
                    if (!l_BoneNode && l_Scene && l_Scene->mRootNode)
                    {
                        aiString l_AssimpName{ sourceName.c_str() };
                        l_BoneNode = l_Scene->mRootNode->FindNode(l_AssimpName);
                    }

                    if (l_BoneNode && l_BoneNode->mParent)
                    {
                        std::string l_ParentSourceName = l_BoneNode->mParent->mName.C_Str();
                        const glm::mat4 l_Identity{ 1.0f };
                        const int l_ParentIndex = l_AcquireBoneIndex(l_ParentSourceName, l_BoneNode->mParent, l_Identity, false);

                        Animation::Bone& l_CurrentBone = l_Skeleton.m_Bones[l_BoneIndex];
                        if (l_CurrentBone.m_ParentIndex < 0)
                        {
                            l_CurrentBone.m_ParentIndex = l_ParentIndex;
                        }

                        Animation::Bone& l_ParentBone = l_Skeleton.m_Bones[l_ParentIndex];
                        if (std::find(l_ParentBone.m_Children.begin(), l_ParentBone.m_Children.end(), l_BoneIndex) == l_ParentBone.m_Children.end())
                        {
                            l_ParentBone.m_Children.push_back(l_BoneIndex);
                        }
                    }
                    else if (l_Skeleton.m_RootBoneIndex < 0)
                    {
                        l_Skeleton.m_RootBoneIndex = l_BoneIndex;
                    }

                    return l_BoneIndex;
                };


            // Helper that records a texture reference and registers file-backed payloads for the renderer.
            const auto CaptureTextureSlot =
                [&](const aiMaterial* sourceMaterial, aiTextureType type, unsigned int textureIndex, Loader::MaterialExtra& matrialExtra) -> int
                {
                    if (!sourceMaterial)
                    {
                        return -1;
                    }

                    aiString l_TexturePath{};
                    aiTextureMapping l_Mapping = aiTextureMapping_UV;
                    unsigned int l_UVIndex = 0;
                    float l_Blend = 1.0f;
                    aiTextureOp l_Operation = aiTextureOp_Multiply;
                    aiTextureMapMode l_MapModes[3] = { aiTextureMapMode_Wrap, aiTextureMapMode_Wrap, aiTextureMapMode_Wrap };
                    unsigned int l_Flags = 0;

                    if (sourceMaterial->GetTexture(type, textureIndex, &l_TexturePath, &l_Mapping, &l_UVIndex, &l_Blend, &l_Operation, l_MapModes, &l_Flags) != aiReturn_SUCCESS)
                    {
                        return -1;
                    }

                    Loader::TextureReference l_Reference{};
                    l_Reference.m_AssimpType = static_cast<int>(type);
                    l_Reference.m_TextureIndex = textureIndex;
                    l_Reference.m_TexturePath = l_TexturePath.C_Str();
                    l_Reference.m_UVChannel = l_UVIndex;
                    l_Reference.m_BlendFactor = l_Blend;
                    l_Reference.m_WrapModeU = static_cast<int>(l_MapModes[0]);
                    l_Reference.m_WrapModeV = static_cast<int>(l_MapModes[1]);
                    l_Reference.m_WrapModeW = static_cast<int>(l_MapModes[2]);

                    int l_TextureIndex = -1;

                    if (!l_Reference.m_TexturePath.empty() && l_Reference.m_TexturePath[0] == '*')
                    {
                        l_Reference.m_IsEmbedded = true;
                        auto a_EmbeddedLookup = l_EmbeddedTextureLookup.find(l_Reference.m_TexturePath);
                        if (a_EmbeddedLookup == l_EmbeddedTextureLookup.end())
                        {
                            const aiTexture* l_EmbeddedTexture = l_Scene ? l_Scene->GetEmbeddedTexture(l_TexturePath.C_Str()) : nullptr;
                            if (l_EmbeddedTexture)
                            {
                                Loader::EmbeddedTexture l_EmbeddedPayload = ExtractEmbeddedTexture(l_EmbeddedTexture, l_Reference.m_TexturePath);
                                const size_t l_NewIndex = l_ModelData.m_EmbeddedTextures.size();
                                l_ModelData.m_EmbeddedTextures.push_back(std::move(l_EmbeddedPayload));
                                l_EmbeddedTextureLookup.emplace(l_Reference.m_TexturePath, l_NewIndex);
                            }
                        }
                    }
                    else if (!l_Reference.m_TexturePath.empty())
                    {
                        std::string l_ResolvedPath = l_BaseDirectory.empty()
                            ? Utilities::FileManagement::NormalizePath(l_Reference.m_TexturePath)
                            : Utilities::FileManagement::JoinPath(l_BaseDirectory, l_Reference.m_TexturePath);

                        auto a_Existing = l_TextureLookup.find(l_ResolvedPath);
                        if (a_Existing != l_TextureLookup.end())
                        {
                            l_TextureIndex = a_Existing->second;
                        }
                        else
                        {
                            l_TextureIndex = static_cast<int>(l_ModelData.m_Textures.size());
                            l_ModelData.m_Textures.push_back(l_ResolvedPath);
                            l_TextureLookup.emplace(std::move(l_ResolvedPath), l_TextureIndex);
                        }
                    }

                    matrialExtra.m_Textures.push_back(std::move(l_Reference));

                    return l_TextureIndex;
                };

            // Convert every material into the renderer-friendly representation before visiting the scene graph.
            l_ModelData.m_Materials.reserve(l_Scene->mNumMaterials);
            l_ModelData.m_MaterialExtras.reserve(l_Scene->mNumMaterials);
            for (unsigned int it_Material = 0; it_Material < l_Scene->mNumMaterials; ++it_Material)
            {
                const aiMaterial* l_AssimpMaterial = l_Scene->mMaterials[it_Material];
                Geometry::Material l_Material{};
                Loader::MaterialExtra l_MaterialExtra{};

                // Base color factor is preferred from PBR data, falling back to diffuse when unavailable.
                aiColor4D l_BaseColor{};
                if (l_AssimpMaterial && l_AssimpMaterial->Get(AI_MATKEY_BASE_COLOR, l_BaseColor) == aiReturn_SUCCESS)
                {
                    l_Material.BaseColorFactor = glm::vec4(l_BaseColor.r, l_BaseColor.g, l_BaseColor.b, l_BaseColor.a);
                }
                else if (l_AssimpMaterial && l_AssimpMaterial->Get(AI_MATKEY_COLOR_DIFFUSE, l_BaseColor) == aiReturn_SUCCESS)
                {
                    l_Material.BaseColorFactor = glm::vec4(l_BaseColor.r, l_BaseColor.g, l_BaseColor.b, l_BaseColor.a);
                }
                else
                {
                    l_Material.BaseColorFactor = glm::vec4(1.0f);
                }

                float l_Metallic = 1.0f;
                if (l_AssimpMaterial && l_AssimpMaterial->Get(AI_MATKEY_METALLIC_FACTOR, l_Metallic) != aiReturn_SUCCESS)
                {
                    // Legacy formats often store metallic information elsewhere, so we retain the default.
                    l_Metallic = 1.0f;
                }
                l_Material.MetallicFactor = l_Metallic;

                float l_Roughness = 1.0f;
                if (l_AssimpMaterial && l_AssimpMaterial->Get(AI_MATKEY_ROUGHNESS_FACTOR, l_Roughness) != aiReturn_SUCCESS)
                {
                    // Shininess maps can approximate roughness, but that conversion requires extra work.
                    // TODO: Convert AI_MATKEY_SHININESS into roughness for legacy Phong materials.
                    l_Roughness = 1.0f;
                }
                l_Material.RoughnessFactor = l_Roughness;

                std::vector<std::pair<aiTextureType, unsigned int>> l_PrimarySlots{};
                const auto l_RegisterPrimary = [&](aiTextureType type, unsigned int index) -> int
                    {
                        if (!l_AssimpMaterial || l_AssimpMaterial->GetTextureCount(type) <= index)
                        {
                            return -1;
                        }

                        const size_t l_PreviousCount = l_MaterialExtra.m_Textures.size();
                        const int l_Result = CaptureTextureSlot(l_AssimpMaterial, type, index, l_MaterialExtra);
                        if (l_MaterialExtra.m_Textures.size() > l_PreviousCount)
                        {
                            l_PrimarySlots.emplace_back(type, index);
                        }
                        return l_Result;
                    };

                // Texture slots come from modern PBR slots first, then fall back to classic equivalents.
                l_Material.BaseColorTextureIndex = l_RegisterPrimary(aiTextureType_BASE_COLOR, 0);
                if (l_Material.BaseColorTextureIndex < 0)
                {
                    l_Material.BaseColorTextureIndex = l_RegisterPrimary(aiTextureType_DIFFUSE, 0);
                }

                l_Material.MetallicRoughnessTextureIndex = l_RegisterPrimary(aiTextureType_METALNESS, 0);
                if (l_Material.MetallicRoughnessTextureIndex < 0)
                {
                    l_Material.MetallicRoughnessTextureIndex = l_RegisterPrimary(aiTextureType_DIFFUSE_ROUGHNESS, 0);
                }
                if (l_Material.MetallicRoughnessTextureIndex < 0)
                {
                    l_Material.MetallicRoughnessTextureIndex = l_RegisterPrimary(aiTextureType_SPECULAR, 0);
                }

                l_Material.NormalTextureIndex = l_RegisterPrimary(aiTextureType_NORMALS, 0);
                if (l_Material.NormalTextureIndex < 0)
                {
                    l_Material.NormalTextureIndex = l_RegisterPrimary(aiTextureType_HEIGHT, 0);
                }

                const aiTextureType l_AllTypes[] =
                {
                    aiTextureType_DIFFUSE,
                    aiTextureType_SPECULAR,
                    aiTextureType_AMBIENT,
                    aiTextureType_EMISSIVE,
                    aiTextureType_HEIGHT,
                    aiTextureType_NORMALS,
                    aiTextureType_SHININESS,
                    aiTextureType_OPACITY,
                    aiTextureType_DISPLACEMENT,
                    aiTextureType_LIGHTMAP,
                    aiTextureType_REFLECTION,
                    aiTextureType_BASE_COLOR,
                    aiTextureType_NORMAL_CAMERA,
                    aiTextureType_EMISSION_COLOR,
                    aiTextureType_METALNESS,
                    aiTextureType_DIFFUSE_ROUGHNESS,
                    aiTextureType_AMBIENT_OCCLUSION,
                    aiTextureType_UNKNOWN
                };

                for (aiTextureType it_Type : l_AllTypes)
                {
                    if (!l_AssimpMaterial)
                    {
                        continue;
                    }

                    const unsigned int l_TextureCount = l_AssimpMaterial->GetTextureCount(it_Type);
                    for (unsigned int it_Texture = 0; it_Texture < l_TextureCount; ++it_Texture)
                    {
                        if (std::find(l_PrimarySlots.begin(), l_PrimarySlots.end(), std::make_pair(it_Type, it_Texture)) != l_PrimarySlots.end())
                        {
                            continue;
                        }
                        CaptureTextureSlot(l_AssimpMaterial, it_Type, it_Texture, l_MaterialExtra);
                    }
                }

                if (l_AssimpMaterial)
                {
                    l_MaterialExtra.m_Properties.reserve(l_AssimpMaterial->mNumProperties);
                    for (unsigned int it_Property = 0; it_Property < l_AssimpMaterial->mNumProperties; ++it_Property)
                    {
                        aiMaterialProperty* l_Property = l_AssimpMaterial->mProperties[it_Property];
                        if (!l_Property)
                        {
                            continue;
                        }

                        Loader::MaterialProperty l_PropertyEntry{};
                        l_PropertyEntry.m_Key = l_Property->mKey.C_Str();
                        l_PropertyEntry.m_Semantic = l_Property->mSemantic;
                        l_PropertyEntry.m_Index = l_Property->mIndex;
                        l_PropertyEntry.m_Type = static_cast<int>(l_Property->mType);
                        if (l_Property->mDataLength > 0 && l_Property->mData)
                        {
                            l_PropertyEntry.m_Data.resize(l_Property->mDataLength);
                            const uint8_t* l_DataBegin = reinterpret_cast<const uint8_t*>(l_Property->mData);
                            std::copy(l_DataBegin, l_DataBegin + l_Property->mDataLength, l_PropertyEntry.m_Data.begin());
                        }

                        l_MaterialExtra.m_Properties.push_back(std::move(l_PropertyEntry));
                    }
                }

                l_ModelData.m_Materials.push_back(l_Material);
                l_ModelData.m_MaterialExtras.push_back(std::move(l_MaterialExtra));
            }

            // Lambda responsible for turning an aiMesh into Geometry::Mesh instances used by the renderer.
            const auto ConvertMesh =
                [&](const aiMesh* assimpMesh) -> std::pair<Geometry::Mesh, Loader::MeshExtra>
                {
                    Geometry::Mesh l_Mesh{};
                    Loader::MeshExtra l_MeshExtra{};
                    if (!assimpMesh)
                    {
                        return { l_Mesh, l_MeshExtra };
                    }

                    const unsigned int l_VertexCount = assimpMesh->mNumVertices;
                    l_Mesh.Vertices.resize(l_VertexCount);
                    l_MeshExtra.m_Name = assimpMesh->mName.C_Str();
                    l_MeshExtra.m_PrimitiveTypes = assimpMesh->mPrimitiveTypes;

                    for (unsigned int it_Vertex = 0; it_Vertex < l_VertexCount; ++it_Vertex)
                    {
                        Vertex l_Vertex{};

                        // Positions are mandatory for rendering; missing data defaults to the origin.
                        if (assimpMesh->HasPositions())
                        {
                            const aiVector3D& l_Position = assimpMesh->mVertices[it_Vertex];
                            l_Vertex.Position = glm::vec3(l_Position.x, l_Position.y, l_Position.z);
                        }
                        else
                        {
                            TR_CORE_WARN("Mesh is missing positions - defaulting to origin");
                            l_Vertex.Position = glm::vec3(0.0f);
                        }

                        // Normals are required for lighting; Assimp can generate them when absent.
                        if (assimpMesh->HasNormals())
                        {
                            const aiVector3D& l_Normal = assimpMesh->mNormals[it_Vertex];
                            l_Vertex.Normal = glm::vec3(l_Normal.x, l_Normal.y, l_Normal.z);
                        }
                        else
                        {
                            l_Vertex.Normal = s_DefaultNormal;
                        }

                        // Tangents/bitangents enable normal mapping, with safe defaults otherwise.
                        if (assimpMesh->HasTangentsAndBitangents())
                        {
                            const aiVector3D& l_Tangent = assimpMesh->mTangents[it_Vertex];
                            const aiVector3D& l_Bitangent = assimpMesh->mBitangents[it_Vertex];
                            l_Vertex.Tangent = glm::vec3(l_Tangent.x, l_Tangent.y, l_Tangent.z);
                            l_Vertex.Bitangent = glm::vec3(l_Bitangent.x, l_Bitangent.y, l_Bitangent.z);
                        }
                        else
                        {
                            l_Vertex.Tangent = s_DefaultTangent;
                            l_Vertex.Bitangent = s_DefaultBitangent;
                        }

                        // Vertex colors provide baked lighting or tint information.
                        if (assimpMesh->HasVertexColors(0))
                        {
                            const aiColor4D& l_Color = assimpMesh->mColors[0][it_Vertex];
                            l_Vertex.Color = glm::vec3(l_Color.r, l_Color.g, l_Color.b);
                        }
                        else
                        {
                            l_Vertex.Color = s_DefaultColor;
                        }

                        // Primary UV set feeds standard material sampling.
                        if (assimpMesh->HasTextureCoords(0))
                        {
                            const aiVector3D& l_Uv = assimpMesh->mTextureCoords[0][it_Vertex];
                            l_Vertex.TexCoord = glm::vec2(l_Uv.x, l_Uv.y);
                        }
                        else
                        {
                            l_Vertex.TexCoord = s_DefaultTexCoord;
                        }

                        l_Mesh.Vertices[it_Vertex] = l_Vertex;
                    }

                    for (unsigned int it_Channel = 1; it_Channel < AI_MAX_NUMBER_OF_TEXTURECOORDS; ++it_Channel)
                    {
                        if (!assimpMesh->HasTextureCoords(it_Channel))
                        {
                            continue;
                        }

                        std::vector<glm::vec3> l_ChannelData;
                        l_ChannelData.reserve(l_VertexCount);
                        for (unsigned int it_Vertex = 0; it_Vertex < l_VertexCount; ++it_Vertex)
                        {
                            const aiVector3D& l_Uv = assimpMesh->mTextureCoords[it_Channel][it_Vertex];
                            l_ChannelData.emplace_back(l_Uv.x, l_Uv.y, l_Uv.z);
                        }

                        l_MeshExtra.m_AdditionalTexCoords.push_back(std::move(l_ChannelData));
                        l_MeshExtra.m_TexCoordComponentCounts.push_back(assimpMesh->mNumUVComponents[it_Channel]);
                    }

                    for (unsigned int it_ColorSet = 0; it_ColorSet < AI_MAX_NUMBER_OF_COLOR_SETS; ++it_ColorSet)
                    {
                        if (!assimpMesh->HasVertexColors(it_ColorSet))
                        {
                            continue;
                        }

                        std::vector<glm::vec4> l_ColorSet;
                        l_ColorSet.reserve(l_VertexCount);
                        for (unsigned int it_Vertex = 0; it_Vertex < l_VertexCount; ++it_Vertex)
                        {
                            const aiColor4D& l_Color = assimpMesh->mColors[it_ColorSet][it_Vertex];
                            l_ColorSet.emplace_back(l_Color.r, l_Color.g, l_Color.b, l_Color.a);
                        }

                        l_MeshExtra.m_VertexColorSets.push_back(std::move(l_ColorSet));
                    }

                    if (assimpMesh->mNumAnimMeshes > 0)
                    {
                        l_MeshExtra.m_MorphTargets.reserve(assimpMesh->mNumAnimMeshes);
                        for (unsigned int it_AnimMesh = 0; it_AnimMesh < assimpMesh->mNumAnimMeshes; ++it_AnimMesh)
                        {
                            aiAnimMesh* l_AnimMesh = assimpMesh->mAnimMeshes[it_AnimMesh];
                            if (!l_AnimMesh)
                            {
                                continue;
                            }

                            Loader::MeshExtra::MorphTarget l_Target{};
                            l_Target.m_Name = l_AnimMesh->mName.length > 0 ? l_AnimMesh->mName.C_Str() : std::string{};
                            const unsigned int l_AnimVertexCount = l_AnimMesh->mNumVertices;

                            if (l_AnimMesh->HasPositions())
                            {
                                l_Target.m_Positions.reserve(l_AnimVertexCount);
                                for (unsigned int it_Vertex = 0; it_Vertex < l_AnimVertexCount; ++it_Vertex)
                                {
                                    const aiVector3D& l_Position = l_AnimMesh->mVertices[it_Vertex];
                                    l_Target.m_Positions.emplace_back(l_Position.x, l_Position.y, l_Position.z);
                                }
                            }

                            if (l_AnimMesh->HasNormals())
                            {
                                l_Target.m_Normals.reserve(l_AnimVertexCount);
                                for (unsigned int it_Vertex = 0; it_Vertex < l_AnimVertexCount; ++it_Vertex)
                                {
                                    const aiVector3D& l_Normal = l_AnimMesh->mNormals[it_Vertex];
                                    l_Target.m_Normals.emplace_back(l_Normal.x, l_Normal.y, l_Normal.z);
                                }
                            }

                            if (l_AnimMesh->HasTangentsAndBitangents())
                            {
                                l_Target.m_Tangents.reserve(l_AnimVertexCount);
                                for (unsigned int it_Vertex = 0; it_Vertex < l_AnimVertexCount; ++it_Vertex)
                                {
                                    const aiVector3D& l_Tangent = l_AnimMesh->mTangents[it_Vertex];
                                    l_Target.m_Tangents.emplace_back(l_Tangent.x, l_Tangent.y, l_Tangent.z);
                                }
                            }

                            l_MeshExtra.m_MorphTargets.push_back(std::move(l_Target));
                        }
                    }

                    if (assimpMesh->HasBones())
                    {
                        const auto a_InsertBoneWeight = [](Vertex& targetVertex, int boneIndex, float boneWeight)
                            {
                                bool l_Assigned = false;
                                for (int it_Influence = 0; it_Influence < static_cast<int>(Vertex::MaxBoneInfluences); ++it_Influence)
                                {
                                    if (targetVertex.m_BoneWeights[it_Influence] == 0.0f)
                                    {
                                        targetVertex.m_BoneIndices[it_Influence] = boneIndex;
                                        targetVertex.m_BoneWeights[it_Influence] = boneWeight;
                                        l_Assigned = true;
                                        break;
                                    }
                                }

                                if (!l_Assigned)
                                {
                                    int l_MinIndex = 0;
                                    float l_MinWeight = targetVertex.m_BoneWeights[0];
                                    for (int it_Influence = 1; it_Influence < static_cast<int>(Vertex::MaxBoneInfluences); ++it_Influence)
                                    {
                                        if (targetVertex.m_BoneWeights[it_Influence] < l_MinWeight)
                                        {
                                            l_MinWeight = targetVertex.m_BoneWeights[it_Influence];
                                            l_MinIndex = it_Influence;
                                        }
                                    }

                                    if (boneWeight > l_MinWeight)
                                    {
                                        targetVertex.m_BoneIndices[l_MinIndex] = boneIndex;
                                        targetVertex.m_BoneWeights[l_MinIndex] = boneWeight;
                                    }
                                }
                            };

                        for (unsigned int it_Bone = 0; it_Bone < assimpMesh->mNumBones; ++it_Bone)
                        {
                            aiBone* l_AssimpBone = assimpMesh->mBones[it_Bone];
                            if (!l_AssimpBone)
                            {
                                continue;
                            }

                            const glm::mat4 l_InverseBindMatrix = ConvertMatrix(l_AssimpBone->mOffsetMatrix);
                            const aiNode* l_BoneNode = nullptr;
                            if (l_Scene && l_Scene->mRootNode)
                            {
                                l_BoneNode = l_Scene->mRootNode->FindNode(l_AssimpBone->mName);
                            }

                            const int l_BoneIndex = l_AcquireBoneIndex(l_AssimpBone->mName.C_Str(), l_BoneNode, l_InverseBindMatrix, true);

                            for (unsigned int it_Weight = 0; it_Weight < l_AssimpBone->mNumWeights; ++it_Weight)
                            {
                                const aiVertexWeight& l_Weight = l_AssimpBone->mWeights[it_Weight];
                                if (l_Weight.mVertexId >= l_VertexCount)
                                {
                                    continue;
                                }

                                Vertex& l_TargetVertex = l_Mesh.Vertices[l_Weight.mVertexId];
                                a_InsertBoneWeight(l_TargetVertex, l_BoneIndex, l_Weight.mWeight);
                            }
                        }

                        for (Vertex& l_Vertex : l_Mesh.Vertices)
                        {
                            float l_TotalWeight = 0.0f;
                            for (int it_Influence = 0; it_Influence < static_cast<int>(Vertex::MaxBoneInfluences); ++it_Influence)
                            {
                                l_TotalWeight += l_Vertex.m_BoneWeights[it_Influence];
                            }

                            if (l_TotalWeight > std::numeric_limits<float>::epsilon())
                            {
                                l_Vertex.m_BoneWeights /= l_TotalWeight;
                            }
                        }

                        // TODO: Support per-vertex influence compression for hardware skinning optimisations.
                    }

                    l_Mesh.Indices.reserve(assimpMesh->mNumFaces * 3u);
                    for (unsigned int it_Face = 0; it_Face < assimpMesh->mNumFaces; ++it_Face)
                    {
                        const aiFace& l_Face = assimpMesh->mFaces[it_Face];
                        if (l_Face.mNumIndices != 3)
                        {
                            TR_CORE_WARN("Encountered non-triangle face with {} indices - skipping", l_Face.mNumIndices);
                            continue;
                        }

                        l_Mesh.Indices.push_back(static_cast<uint32_t>(l_Face.mIndices[0]));
                        l_Mesh.Indices.push_back(static_cast<uint32_t>(l_Face.mIndices[1]));
                        l_Mesh.Indices.push_back(static_cast<uint32_t>(l_Face.mIndices[2]));
                    }

                    if (assimpMesh->mMaterialIndex < l_ModelData.m_Materials.size())
                    {
                        l_Mesh.MaterialIndex = static_cast<int>(assimpMesh->mMaterialIndex);
                    }
                    else
                    {
                        TR_CORE_WARN("Mesh references out-of-range material {}", assimpMesh->mMaterialIndex);
                        l_Mesh.MaterialIndex = -1;
                    }

                    return { l_Mesh, l_MeshExtra };
                };

                const std::function<void(const aiNode*, const glm::mat4&, int)> ProcessNode =
                    [&](const aiNode* node, const glm::mat4& parentTransform, int parentIndex)
                    {
                        if (!node)
                        {
                            return;
                        }

                        // Compose the world transform by chaining the parent's matrix with the node's local transform.
                        const glm::mat4 l_LocalTransform = ConvertMatrix(node->mTransformation);
                        const glm::mat4 l_WorldTransform = parentTransform * l_LocalTransform;
                        const size_t l_NodeIndex = l_ModelData.m_SceneNodes.size();
                        l_ModelData.m_SceneNodes.emplace_back();
                        Loader::SceneNode& l_NodeData = l_ModelData.m_SceneNodes.back();
                        l_NodeData.m_Name = node->mName.C_Str();
                        l_NodeData.m_LocalTransform = l_LocalTransform;
                        l_NodeData.m_GlobalTransform = l_WorldTransform;
                        l_NodeData.m_ParentIndex = parentIndex;
                        if (parentIndex >= 0 && static_cast<size_t>(parentIndex) < l_ModelData.m_SceneNodes.size())
                        {
                            l_ModelData.m_SceneNodes[static_cast<size_t>(parentIndex)].m_Children.push_back(l_NodeIndex);
                        }
                        ExtractMetadata(node->mMetaData, l_NodeData.m_Metadata);
                        l_ModelData.m_NodeNameToIndex.emplace(l_NodeData.m_Name, l_NodeIndex);

                        // Convert meshes referenced by the current node while caching shared geometry.
                        for (unsigned int it_Mesh = 0; it_Mesh < node->mNumMeshes; ++it_Mesh)
                        {
                            const unsigned int l_AssimpMeshIndex = node->mMeshes[it_Mesh];
                            if (l_AssimpMeshIndex >= l_Scene->mNumMeshes)
                            {
                                TR_CORE_WARN("Node '{}' references out-of-range mesh index {}", node->mName.C_Str(), l_AssimpMeshIndex);
                                continue;
                            }

                            const aiMesh* l_AssimpMesh = l_Scene->mMeshes[l_AssimpMeshIndex];
                            if (!l_AssimpMesh)
                            {
                                continue;
                            }

                            size_t l_ModelMeshIndex = std::numeric_limits<size_t>::max();
                            auto a_CachedMesh = l_MeshCache.find(l_AssimpMesh);
                            if (a_CachedMesh == l_MeshCache.end())
                            {
                                auto l_ConvertedPair = ConvertMesh(l_AssimpMesh);
                                l_ModelMeshIndex = l_ModelData.m_Meshes.size();
                                l_ModelData.m_Meshes.push_back(std::move(l_ConvertedPair.first));
                                l_ModelData.m_MeshExtras.push_back(std::move(l_ConvertedPair.second));
                                l_MeshCache.emplace(l_AssimpMesh, l_ModelMeshIndex);
                            }
                            else
                            {
                                l_ModelMeshIndex = a_CachedMesh->second;
                            }

                            MeshInstance l_Instance{};
                            l_Instance.m_MeshIndex = l_ModelMeshIndex;
                            l_Instance.m_ModelMatrix = l_WorldTransform;
                            l_Instance.m_NodeName = node->mName.C_Str();
                            l_ModelData.m_MeshInstances.push_back(std::move(l_Instance));
                        }

                    // Recurse through children to cover the full scene graph.
                    for (unsigned int it_Child = 0; it_Child < node->mNumChildren; ++it_Child)
                    {
                        ProcessNode(node->mChildren[it_Child], l_WorldTransform, static_cast<int>(l_NodeIndex));
                    }
                };

                ProcessNode(l_Scene->mRootNode, glm::mat4(1.0f), -1);

            if (l_Scene->mNumAnimations > 0)
            {
                l_ModelData.m_AnimationClips.reserve(l_Scene->mNumAnimations);
                l_ModelData.m_AnimationExtras.reserve(l_Scene->mNumAnimations);

                for (unsigned int it_Animation = 0; it_Animation < l_Scene->mNumAnimations; ++it_Animation)
                {
                    const aiAnimation* l_AssimpAnimation = l_Scene->mAnimations[it_Animation];
                    if (!l_AssimpAnimation)
                    {
                        continue;
                    }

                    Animation::AnimationClip l_Clip{};
                    Loader::AnimationExtra l_AnimationExtra{};
                    if (l_AssimpAnimation->mName.length > 0)
                    {
                        l_Clip.m_Name = l_AssimpAnimation->mName.C_Str();
                    }
                    else
                    {
                        l_Clip.m_Name = "Animation_" + std::to_string(it_Animation);
                    }

                    const double l_TicksPerSecond = (l_AssimpAnimation->mTicksPerSecond > 0.0) ? l_AssimpAnimation->mTicksPerSecond : 25.0; 
                    // Default to 25fps when the asset omits an explicit tick rate.
                    l_Clip.m_TicksPerSecond = static_cast<float>(l_TicksPerSecond);
                    const double l_DurationTicks = l_AssimpAnimation->mDuration;
                    l_Clip.m_DurationSeconds = l_TicksPerSecond > 0.0 ? static_cast<float>(l_DurationTicks / l_TicksPerSecond) : 0.0f;
                    l_AnimationExtra.m_Flags = l_AssimpAnimation->mFlags;

                    l_Clip.m_Channels.reserve(l_AssimpAnimation->mNumChannels);
                    for (unsigned int it_Channel = 0; it_Channel < l_AssimpAnimation->mNumChannels; ++it_Channel)
                    {
                        aiNodeAnim* l_AssimpChannel = l_AssimpAnimation->mChannels[it_Channel];
                        if (!l_AssimpChannel)
                        {
                            continue;
                        }

                        const std::string l_NormalizedChannelName = NormalizeMixamoName(l_AssimpChannel->mNodeName.C_Str());
                        int l_BoneIndex = -1;

                        auto a_NormalizedLookup = l_Skeleton.m_NameToIndex.find(l_NormalizedChannelName);
                        if (a_NormalizedLookup != l_Skeleton.m_NameToIndex.end())
                        {
                            l_BoneIndex = a_NormalizedLookup->second;
                        }
                        else
                        {
                            auto sourceLookup = l_Skeleton.m_SourceNameToIndex.find(l_AssimpChannel->mNodeName.C_Str());
                            if (sourceLookup != l_Skeleton.m_SourceNameToIndex.end())
                            {
                                l_BoneIndex = sourceLookup->second;
                            }
                            else
                            {
                                const aiNode* l_ChannelNode = l_Scene->mRootNode ? l_Scene->mRootNode->FindNode(l_AssimpChannel->mNodeName) : nullptr;
                                l_BoneIndex = l_AcquireBoneIndex(l_AssimpChannel->mNodeName.C_Str(), l_ChannelNode, glm::mat4(1.0f), false);
                            }
                        }

                        if (l_BoneIndex < 0)
                        {
                            continue;
                        }

                        Animation::TransformChannel l_Channel{};
                        l_Channel.m_BoneIndex = l_BoneIndex;

                        l_Channel.m_TranslationKeys.reserve(l_AssimpChannel->mNumPositionKeys);
                        for (unsigned int it_Key = 0; it_Key < l_AssimpChannel->mNumPositionKeys; ++it_Key)
                        {
                            const aiVectorKey& l_Key = l_AssimpChannel->mPositionKeys[it_Key];
                            Animation::VectorKeyframe l_Keyframe{};
                            l_Keyframe.m_TimeSeconds = static_cast<float>(l_Key.mTime / l_TicksPerSecond);
                            l_Keyframe.m_Value = glm::vec3(l_Key.mValue.x, l_Key.mValue.y, l_Key.mValue.z);
                            l_Channel.m_TranslationKeys.push_back(l_Keyframe);
                        }

                        l_Channel.m_RotationKeys.reserve(l_AssimpChannel->mNumRotationKeys);
                        for (unsigned int it_Key = 0; it_Key < l_AssimpChannel->mNumRotationKeys; ++it_Key)
                        {
                            const aiQuatKey& l_Key = l_AssimpChannel->mRotationKeys[it_Key];
                            Animation::QuaternionKeyframe l_Keyframe{};
                            l_Keyframe.m_TimeSeconds = static_cast<float>(l_Key.mTime / l_TicksPerSecond);
                            l_Keyframe.m_Value = glm::quat(l_Key.mValue.w, l_Key.mValue.x, l_Key.mValue.y, l_Key.mValue.z);
                            l_Channel.m_RotationKeys.push_back(l_Keyframe);
                        }

                        l_Channel.m_ScaleKeys.reserve(l_AssimpChannel->mNumScalingKeys);
                        for (unsigned int it_Key = 0; it_Key < l_AssimpChannel->mNumScalingKeys; ++it_Key)
                        {
                            const aiVectorKey& l_Key = l_AssimpChannel->mScalingKeys[it_Key];
                            Animation::VectorKeyframe l_Keyframe{};
                            l_Keyframe.m_TimeSeconds = static_cast<float>(l_Key.mTime / l_TicksPerSecond);
                            l_Keyframe.m_Value = glm::vec3(l_Key.mValue.x, l_Key.mValue.y, l_Key.mValue.z);
                            l_Channel.m_ScaleKeys.push_back(l_Keyframe);
                        }

                        l_Clip.m_Channels.push_back(std::move(l_Channel));
                    }

                    l_AnimationExtra.m_MeshChannels.reserve(l_AssimpAnimation->mNumMeshChannels);
                    for (unsigned int it_MeshChannel = 0; it_MeshChannel < l_AssimpAnimation->mNumMeshChannels; ++it_MeshChannel)
                    {
                        const aiMeshAnim* l_MeshChannel = l_AssimpAnimation->mMeshChannels[it_MeshChannel];
                        if (!l_MeshChannel)
                        {
                            continue;
                        }

                        Loader::AnimationExtra::MeshChannel l_Channel{};
                        l_Channel.m_Name = l_MeshChannel->mName.length > 0 ? l_MeshChannel->mName.C_Str() : std::string{};
                        l_Channel.m_MeshId = l_MeshChannel->mMeshId;
                        l_Channel.m_Keys.reserve(l_MeshChannel->mNumKeys);

                        for (unsigned int it_Key = 0; it_Key < l_MeshChannel->mNumKeys; ++it_Key)
                        {
                            const aiMeshKey& l_Key = l_MeshChannel->mKeys[it_Key];
                            Loader::AnimationExtra::MeshChannelKey l_Keyframe{};
                            l_Keyframe.m_TimeSeconds = static_cast<float>(l_Key.mTime / l_TicksPerSecond);
                            l_Keyframe.m_Value = l_Key.mValue;
                            l_Channel.m_Keys.push_back(std::move(l_Keyframe));
                        }

                        l_AnimationExtra.m_MeshChannels.push_back(std::move(l_Channel));
                    }

                    l_AnimationExtra.m_MorphChannels.reserve(l_AssimpAnimation->mNumMorphMeshChannels);
                    for (unsigned int it_MorphChannel = 0; it_MorphChannel < l_AssimpAnimation->mNumMorphMeshChannels; ++it_MorphChannel)
                    {
                        const aiMeshMorphAnim* l_MorphChannel = l_AssimpAnimation->mMorphMeshChannels[it_MorphChannel];
                        if (!l_MorphChannel)
                        {
                            continue;
                        }

                        Loader::AnimationExtra::MorphChannel l_Channel{};
                        l_Channel.m_Name = l_MorphChannel->mName.length > 0 ? l_MorphChannel->mName.C_Str() : std::string{};
                        l_Channel.m_MeshId = l_MorphChannel->mMeshId;
                        l_Channel.m_Keys.reserve(l_MorphChannel->mNumKeys);

                        for (unsigned int it_Key = 0; it_Key < l_MorphChannel->mNumKeys; ++it_Key)
                        {
                            const aiMeshMorphKey& l_Key = l_MorphChannel->mKeys[it_Key];
                            Loader::AnimationExtra::MorphWeightKey l_Keyframe{};
                            l_Keyframe.m_TimeSeconds = static_cast<float>(l_Key.mTime / l_TicksPerSecond);
                            if (l_Key.mNumValuesAndWeights > 0)
                            {
                                l_Keyframe.m_Values.reserve(l_Key.mNumValuesAndWeights);
                                l_Keyframe.m_Weights.reserve(l_Key.mNumValuesAndWeights);
                                for (unsigned int it_Value = 0; it_Value < l_Key.mNumValuesAndWeights; ++it_Value)
                                {
                                    l_Keyframe.m_Values.push_back(l_Key.mValues[it_Value]);
                                    l_Keyframe.m_Weights.push_back(static_cast<float>(l_Key.mWeights[it_Value]));
                                }
                            }
                            l_Channel.m_Keys.push_back(std::move(l_Keyframe));
                        }

                        l_AnimationExtra.m_MorphChannels.push_back(std::move(l_Channel));
                    }

                    // TODO: Implement animation blending and retargeting hooks to combine multiple clips at runtime.
                    l_ModelData.m_AnimationClips.push_back(std::move(l_Clip));
                    l_ModelData.m_AnimationExtras.push_back(std::move(l_AnimationExtra));
                }
            }

            if (l_Scene->mNumCameras > 0)
            {
                l_ModelData.m_Cameras.reserve(l_Scene->mNumCameras);
                for (unsigned int it_Camera = 0; it_Camera < l_Scene->mNumCameras; ++it_Camera)
                {
                    const aiCamera* l_AssimpCamera = l_Scene->mCameras[it_Camera];
                    if (!l_AssimpCamera)
                    {
                        continue;
                    }

                    Loader::CameraData l_Camera{};
                    l_Camera.m_Name = l_AssimpCamera->mName.C_Str();
                    l_Camera.m_Position = glm::vec3(l_AssimpCamera->mPosition.x, l_AssimpCamera->mPosition.y, l_AssimpCamera->mPosition.z);
                    l_Camera.m_Up = glm::vec3(l_AssimpCamera->mUp.x, l_AssimpCamera->mUp.y, l_AssimpCamera->mUp.z);
                    l_Camera.m_LookAt = glm::vec3(l_AssimpCamera->mLookAt.x, l_AssimpCamera->mLookAt.y, l_AssimpCamera->mLookAt.z);
                    l_Camera.m_HorizontalFov = l_AssimpCamera->mHorizontalFOV;
                    l_Camera.m_Aspect = l_AssimpCamera->mAspect;
                    l_Camera.m_NearClip = l_AssimpCamera->mClipPlaneNear;
                    l_Camera.m_FarClip = l_AssimpCamera->mClipPlaneFar;
                    l_Camera.m_OrthographicWidth = l_AssimpCamera->mOrthographicWidth;

                    auto a_NodeLookup = l_ModelData.m_NodeNameToIndex.find(l_Camera.m_Name);
                    if (a_NodeLookup != l_ModelData.m_NodeNameToIndex.end())
                    {
                        l_Camera.m_NodeTransform = l_ModelData.m_SceneNodes[a_NodeLookup->second].m_GlobalTransform;
                    }

                    l_ModelData.m_Cameras.push_back(std::move(l_Camera));
                }
            }

            if (l_Scene->mNumLights > 0)
            {
                l_ModelData.m_Lights.reserve(l_Scene->mNumLights);
                for (unsigned int it_Light = 0; it_Light < l_Scene->mNumLights; ++it_Light)
                {
                    const aiLight* l_AssimpLight = l_Scene->mLights[it_Light];
                    if (!l_AssimpLight)
                    {
                        continue;
                    }

                    Loader::LightData l_Light{};
                    l_Light.m_Name = l_AssimpLight->mName.C_Str();
                    l_Light.m_Type = static_cast<int>(l_AssimpLight->mType);
                    l_Light.m_ColorDiffuse = glm::vec3(l_AssimpLight->mColorDiffuse.r, l_AssimpLight->mColorDiffuse.g, l_AssimpLight->mColorDiffuse.b);
                    l_Light.m_ColorSpecular = glm::vec3(l_AssimpLight->mColorSpecular.r, l_AssimpLight->mColorSpecular.g, l_AssimpLight->mColorSpecular.b);
                    l_Light.m_ColorAmbient = glm::vec3(l_AssimpLight->mColorAmbient.r, l_AssimpLight->mColorAmbient.g, l_AssimpLight->mColorAmbient.b);
                    l_Light.m_AttenuationConstant = l_AssimpLight->mAttenuationConstant;
                    l_Light.m_AttenuationLinear = l_AssimpLight->mAttenuationLinear;
                    l_Light.m_AttenuationQuadratic = l_AssimpLight->mAttenuationQuadratic;
                    l_Light.m_InnerConeAngle = l_AssimpLight->mAngleInnerCone;
                    l_Light.m_OuterConeAngle = l_AssimpLight->mAngleOuterCone;

                    auto a_NodeLookup = l_ModelData.m_NodeNameToIndex.find(l_Light.m_Name);
                    if (a_NodeLookup != l_ModelData.m_NodeNameToIndex.end())
                    {
                        l_Light.m_NodeTransform = l_ModelData.m_SceneNodes[a_NodeLookup->second].m_GlobalTransform;
                    }

                    l_ModelData.m_Lights.push_back(std::move(l_Light));
                }
            }

            return l_ModelData;
        }
    }
}