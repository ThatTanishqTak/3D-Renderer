#include "Loader/ModelLoader.h"

#include "Animation/AnimationSourceRegistry.h"
#include "Core/Utilities.h"

#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/pbrmaterial.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <array>
#include <filesystem>
#include <unordered_map>

namespace Trident
{
    namespace Loader
    {
        namespace
        {
            glm::mat4 ConvertMatrix(const aiMatrix4x4& matrix)
            {
                const glm::mat4 l_Temp = glm::make_mat4(&matrix.a1);
                return glm::transpose(l_Temp);
            }

            void BuildNodeLookup(const aiNode* node, std::unordered_map<std::string, const aiNode*>& lookup)
            {
                if (node == nullptr)
                {
                    return;
                }

                lookup[node->mName.C_Str()] = node;
                for (unsigned int it_Child = 0; it_Child < node->mNumChildren; ++it_Child)
                {
                    BuildNodeLookup(node->mChildren[it_Child], lookup);
                }
            }

            std::string ResolveTexturePath(const aiString& path, const ModelSource& source)
            {
                std::string l_RawPath = path.C_Str();
                if (l_RawPath.empty())
                {
                    return {};
                }

                if (!l_RawPath.empty() && l_RawPath.front() == '*')
                {
                    return source.GetIdentifier() + "::" + l_RawPath;
                }

                std::filesystem::path l_PathView{ l_RawPath };
                if (l_PathView.is_relative() && !source.GetWorkingDirectory().empty())
                {
                    l_PathView = source.GetWorkingDirectory() / l_PathView;
                }

                return Utilities::FileManagement::NormalizePath(l_PathView.string());
            }

            int RegisterTexture(const aiMaterial* material, aiTextureType type, ModelData& modelData, std::unordered_map<std::string, int>& textureLookup, const ModelSource& source)
            {
                if (material == nullptr)
                {
                    return -1;
                }

                const unsigned int l_TextureCount = material->GetTextureCount(type);
                if (l_TextureCount == 0)
                {
                    return -1;
                }

                aiString l_TexturePath;
                if (material->GetTexture(type, 0, &l_TexturePath) != aiReturn_SUCCESS)
                {
                    return -1;
                }

                const std::string l_Normalised = ResolveTexturePath(l_TexturePath, source);
                if (l_Normalised.empty())
                {
                    return -1;
                }

                auto a_Existing = textureLookup.find(l_Normalised);
                if (a_Existing != textureLookup.end())
                {
                    return a_Existing->second;
                }

                const int l_Index = static_cast<int>(modelData.m_Textures.size());
                textureLookup[l_Normalised] = l_Index;
                modelData.m_Textures.push_back(l_Normalised);
                return l_Index;
            }

            void PopulateMaterials(const aiScene* scene, const ModelSource& source, ModelData& modelData)
            {
                if (scene == nullptr)
                {
                    return;
                }

                std::unordered_map<std::string, int> l_TextureLookup{};
                l_TextureLookup.reserve(scene->mNumMaterials);

                for (unsigned int it_Material = 0; it_Material < scene->mNumMaterials; ++it_Material)
                {
                    const aiMaterial* l_AssimpMaterial = scene->mMaterials[it_Material];
                    if (l_AssimpMaterial == nullptr)
                    {
                        continue;
                    }

                    Geometry::Material l_Material{};

                    aiColor4D l_BaseColor{ 1.0f, 1.0f, 1.0f, 1.0f };
                    if (aiGetMaterialColor(l_AssimpMaterial, AI_MATKEY_BASE_COLOR, &l_BaseColor) != aiReturn_SUCCESS)
                    {
                        aiGetMaterialColor(l_AssimpMaterial, AI_MATKEY_COLOR_DIFFUSE, &l_BaseColor);
                    }
                    l_Material.BaseColorFactor = glm::vec4(l_BaseColor.r, l_BaseColor.g, l_BaseColor.b, l_BaseColor.a);

                    float l_Metallic = 1.0f;
                    if (aiGetMaterialFloat(l_AssimpMaterial, AI_MATKEY_METALLIC_FACTOR, &l_Metallic) != aiReturn_SUCCESS)
                    {
                        aiGetMaterialFloat(l_AssimpMaterial, AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_METALLIC_FACTOR, &l_Metallic);
                    }
                    l_Material.MetallicFactor = l_Metallic;

                    float l_Roughness = 1.0f;
                    if (aiGetMaterialFloat(l_AssimpMaterial, AI_MATKEY_ROUGHNESS_FACTOR, &l_Roughness) != aiReturn_SUCCESS)
                    {
                        aiGetMaterialFloat(l_AssimpMaterial, AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_ROUGHNESS_FACTOR, &l_Roughness);
                    }
                    l_Material.RoughnessFactor = l_Roughness;

                    l_Material.BaseColorTextureIndex = RegisterTexture(l_AssimpMaterial, aiTextureType_BASE_COLOR, modelData, l_TextureLookup, source);
                    if (l_Material.BaseColorTextureIndex < 0)
                    {
                        l_Material.BaseColorTextureIndex = RegisterTexture(l_AssimpMaterial, aiTextureType_DIFFUSE, modelData, l_TextureLookup, source);
                    }

                    l_Material.MetallicRoughnessTextureIndex = RegisterTexture(l_AssimpMaterial, aiTextureType_METALNESS, modelData, l_TextureLookup, source);
                    if (l_Material.MetallicRoughnessTextureIndex < 0)
                    {
                        l_Material.MetallicRoughnessTextureIndex = RegisterTexture(l_AssimpMaterial, aiTextureType_UNKNOWN, modelData, l_TextureLookup, source);
                    }

                    l_Material.NormalTextureIndex = RegisterTexture(l_AssimpMaterial, aiTextureType_NORMALS, modelData, l_TextureLookup, source);
                    if (l_Material.NormalTextureIndex < 0)
                    {
                        l_Material.NormalTextureIndex = RegisterTexture(l_AssimpMaterial, aiTextureType_HEIGHT, modelData, l_TextureLookup, source);
                    }

                    modelData.m_Materials.emplace_back(std::move(l_Material));
                }
            }

            struct BoneContext
            {
                std::unordered_map<std::string, size_t> m_NameToIndex{};
                std::unordered_map<size_t, std::string> m_PendingParent{};
            };

            void BuildSkeleton(const aiScene* scene, const ModelSource& source, const std::unordered_map<std::string, const aiNode*>& nodeLookup, 
                ModelData& modelData, BoneContext& boneContext)
            {
                if (scene == nullptr)
                {
                    return;
                }

                Animation::Skeleton& l_Skeleton = modelData.m_Skeleton;
                l_Skeleton.m_SourceAssetId = source.GetIdentifier();
                l_Skeleton.m_SourceProfile = Animation::AnimationSourceRegistry::Get().ResolveProfileName(l_Skeleton.m_SourceAssetId);

                Animation::AnimationSourceRegistry& l_Registry = Animation::AnimationSourceRegistry::Get();

                for (unsigned int it_Mesh = 0; it_Mesh < scene->mNumMeshes; ++it_Mesh)
                {
                    const aiMesh* l_Mesh = scene->mMeshes[it_Mesh];
                    if (l_Mesh == nullptr)
                    {
                        continue;
                    }

                    for (unsigned int it_Bone = 0; it_Bone < l_Mesh->mNumBones; ++it_Bone)
                    {
                        const aiBone* l_Bone = l_Mesh->mBones[it_Bone];
                        if (l_Bone == nullptr)
                        {
                            continue;
                        }

                        const std::string l_SourceName = l_Bone->mName.C_Str();
                        if (l_SourceName.empty())
                        {
                            continue;
                        }

                        if (boneContext.m_NameToIndex.find(l_SourceName) != boneContext.m_NameToIndex.end())
                        {
                            continue;
                        }

                        Animation::Bone l_NewBone{};
                        l_NewBone.m_SourceName = l_SourceName;
                        l_NewBone.m_Name = l_Registry.NormaliseBoneName(l_SourceName, source.GetIdentifier());
                        if (l_NewBone.m_Name.empty())
                        {
                            l_NewBone.m_Name = l_SourceName;
                        }

                        auto a_NodeIt = nodeLookup.find(l_SourceName);
                        if (a_NodeIt != nodeLookup.end() && a_NodeIt->second != nullptr)
                        {
                            l_NewBone.m_LocalBindTransform = ConvertMatrix(a_NodeIt->second->mTransformation);
                            if (a_NodeIt->second->mParent != nullptr)
                            {
                                boneContext.m_PendingParent[static_cast<size_t>(l_Skeleton.m_Bones.size())] = a_NodeIt->second->mParent->mName.C_Str();
                            }
                        }
                        else
                        {
                            l_NewBone.m_LocalBindTransform = glm::mat4(1.0f);
                        }

                        l_NewBone.m_InverseBindMatrix = ConvertMatrix(l_Bone->mOffsetMatrix);

                        const size_t l_Index = l_Skeleton.m_Bones.size();
                        l_Skeleton.m_Bones.emplace_back(std::move(l_NewBone));
                        l_Skeleton.m_NameToIndex[l_Skeleton.m_Bones.back().m_Name] = static_cast<int>(l_Index);
                        l_Skeleton.m_NameToIndex[l_Skeleton.m_Bones.back().m_SourceName] = static_cast<int>(l_Index);
                        boneContext.m_NameToIndex[l_SourceName] = l_Index;
                    }
                }

                for (size_t it_Index = 0; it_Index < l_Skeleton.m_Bones.size(); ++it_Index)
                {
                    Animation::Bone& l_Bone = l_Skeleton.m_Bones[it_Index];
                    auto a_PendingParent = boneContext.m_PendingParent.find(it_Index);
                    if (a_PendingParent == boneContext.m_PendingParent.end())
                    {
                        l_Bone.m_ParentIndex = -1;
                        continue;
                    }

                    auto a_ParentIt = boneContext.m_NameToIndex.find(a_PendingParent->second);
                    if (a_ParentIt != boneContext.m_NameToIndex.end())
                    {
                        l_Bone.m_ParentIndex = static_cast<int>(a_ParentIt->second);
                        Animation::Bone& l_Parent = l_Skeleton.m_Bones[a_ParentIt->second];
                        l_Parent.m_Children.push_back(static_cast<int>(it_Index));
                    }
                    else
                    {
                        l_Bone.m_ParentIndex = -1;
                    }
                }

                l_Skeleton.m_RootBoneIndex = -1;
                for (size_t it_Index = 0; it_Index < l_Skeleton.m_Bones.size(); ++it_Index)
                {
                    if (l_Skeleton.m_Bones[it_Index].m_ParentIndex < 0)
                    {
                        l_Skeleton.m_RootBoneIndex = static_cast<int>(it_Index);
                        break;
                    }
                }
            }

            void PopulateMeshes(const aiScene* scene, const BoneContext& boneContext, ModelData& modelData)
            {
                if (scene == nullptr)
                {
                    return;
                }

                modelData.m_Meshes.reserve(scene->mNumMeshes);

                for (unsigned int it_MeshIndex = 0; it_MeshIndex < scene->mNumMeshes; ++it_MeshIndex)
                {
                    const aiMesh* l_Mesh = scene->mMeshes[it_MeshIndex];
                    if (l_Mesh == nullptr)
                    {
                        continue;
                    }

                    Geometry::Mesh l_MeshResult{};
                    l_MeshResult.MaterialIndex = l_Mesh->mMaterialIndex >= 0 ? static_cast<int>(l_Mesh->mMaterialIndex) : -1;
                    l_MeshResult.Vertices.reserve(l_Mesh->mNumVertices);
                    l_MeshResult.Indices.reserve(l_Mesh->mNumFaces * 3);

                    const std::array<int, Vertex::MaxBoneInfluences> l_DefaultIndices{ -1, -1, -1, -1 };
                    const std::array<float, Vertex::MaxBoneInfluences> l_DefaultWeights{ 0.0f, 0.0f, 0.0f, 0.0f };
                    std::vector<std::array<int, Vertex::MaxBoneInfluences>> l_BoneIndices(l_Mesh->mNumVertices, l_DefaultIndices);
                    std::vector<std::array<float, Vertex::MaxBoneInfluences>> l_BoneWeights(l_Mesh->mNumVertices, l_DefaultWeights);

                    const auto a_AddBoneWeight = [&](unsigned int vertexID, int bookIndex, float weight)
                        {
                            if (vertexID >= l_BoneIndices.size())
                            {
                                return;
                            }

                            std::array<int, Vertex::MaxBoneInfluences>& l_Indices = l_BoneIndices[vertexID];
                            std::array<float, Vertex::MaxBoneInfluences>& l_Weights = l_BoneWeights[vertexID];

                            for (uint32_t it_Slot = 0; it_Slot < Vertex::MaxBoneInfluences; ++it_Slot)
                            {
                                if (l_Weights[it_Slot] == 0.0f)
                                {
                                    l_Indices[it_Slot] = bookIndex;
                                    l_Weights[it_Slot] = weight;
                                    return;
                                }
                            }

                            uint32_t l_MinIndex = 0;
                            for (uint32_t it_Slot = 1; it_Slot < Vertex::MaxBoneInfluences; ++it_Slot)
                            {
                                if (l_Weights[it_Slot] < l_Weights[l_MinIndex])
                                {
                                    l_MinIndex = it_Slot;
                                }
                            }

                            if (weight > l_Weights[l_MinIndex])
                            {
                                l_Indices[l_MinIndex] = bookIndex;
                                l_Weights[l_MinIndex] = weight;
                            }
                        };

                    for (unsigned int it_Bone = 0; it_Bone < l_Mesh->mNumBones; ++it_Bone)
                    {
                        const aiBone* l_Bone = l_Mesh->mBones[it_Bone];
                        if (l_Bone == nullptr)
                        {
                            continue;
                        }

                        auto bookIndexIt = boneContext.m_NameToIndex.find(l_Bone->mName.C_Str());
                        if (bookIndexIt == boneContext.m_NameToIndex.end())
                        {
                            continue;
                        }

                        const int l_BoneIndex = static_cast<int>(bookIndexIt->second);
                        for (unsigned int it_Weight = 0; it_Weight < l_Bone->mNumWeights; ++it_Weight)
                        {
                            const aiVertexWeight& l_Weight = l_Bone->mWeights[it_Weight];
                            a_AddBoneWeight(l_Weight.mVertexId, l_BoneIndex, l_Weight.mWeight);
                        }
                    }

                    for (unsigned int it_Vertex = 0; it_Vertex < l_Mesh->mNumVertices; ++it_Vertex)
                    {
                        Vertex l_Vertex{};

                        const aiVector3D& l_Position = l_Mesh->mVertices[it_Vertex];
                        l_Vertex.Position = glm::vec3(l_Position.x, l_Position.y, l_Position.z);

                        if (l_Mesh->HasNormals())
                        {
                            const aiVector3D& l_Normal = l_Mesh->mNormals[it_Vertex];
                            l_Vertex.Normal = glm::vec3(l_Normal.x, l_Normal.y, l_Normal.z);
                        }
                        else
                        {
                            l_Vertex.Normal = glm::vec3(0.0f);
                        }

                        if (l_Mesh->HasTangentsAndBitangents())
                        {
                            const aiVector3D& l_Tangent = l_Mesh->mTangents[it_Vertex];
                            const aiVector3D& l_Bitangent = l_Mesh->mBitangents[it_Vertex];
                            l_Vertex.Tangent = glm::vec3(l_Tangent.x, l_Tangent.y, l_Tangent.z);
                            l_Vertex.Bitangent = glm::vec3(l_Bitangent.x, l_Bitangent.y, l_Bitangent.z);
                        }
                        else
                        {
                            l_Vertex.Tangent = glm::vec3(0.0f);
                            l_Vertex.Bitangent = glm::vec3(0.0f);
                        }

                        if (l_Mesh->HasTextureCoords(0))
                        {
                            const aiVector3D& l_TexCoord = l_Mesh->mTextureCoords[0][it_Vertex];
                            l_Vertex.TexCoord = glm::vec2(l_TexCoord.x, l_TexCoord.y);
                        }
                        else
                        {
                            l_Vertex.TexCoord = glm::vec2(0.0f);
                        }

                        if (l_Mesh->HasVertexColors(0))
                        {
                            const aiColor4D& l_Color = l_Mesh->mColors[0][it_Vertex];
                            l_Vertex.Color = glm::vec3(l_Color.r, l_Color.g, l_Color.b);
                        }
                        else
                        {
                            l_Vertex.Color = glm::vec3(1.0f);
                        }

                        const std::array<int, Vertex::MaxBoneInfluences>& l_VertexIndices = l_BoneIndices[it_Vertex];
                        const std::array<float, Vertex::MaxBoneInfluences>& l_VertexWeights = l_BoneWeights[it_Vertex];
                        float l_TotalWeight = 0.0f;
                        for (float l_Value : l_VertexWeights)
                        {
                            l_TotalWeight += l_Value;
                        }

                        if (l_TotalWeight > 0.0f)
                        {
                            const float l_Inv = 1.0f / l_TotalWeight;
                            for (uint32_t it_Slot = 0; it_Slot < Vertex::MaxBoneInfluences; ++it_Slot)
                            {
                                const int l_IndexValue = l_VertexIndices[it_Slot];
                                l_Vertex.m_BoneIndices[it_Slot] = l_IndexValue >= 0 ? l_IndexValue : 0;
                                l_Vertex.m_BoneWeights[it_Slot] = l_VertexWeights[it_Slot] * l_Inv;
                            }
                        }
                        else
                        {
                            for (uint32_t it_Slot = 0; it_Slot < Vertex::MaxBoneInfluences; ++it_Slot)
                            {
                                l_Vertex.m_BoneIndices[it_Slot] = 0;
                                l_Vertex.m_BoneWeights[it_Slot] = 0.0f;
                            }
                        }

                        l_MeshResult.Vertices.emplace_back(l_Vertex);
                    }

                    for (unsigned int it_Face = 0; it_Face < l_Mesh->mNumFaces; ++it_Face)
                    {
                        const aiFace& l_Face = l_Mesh->mFaces[it_Face];
                        for (unsigned int it_Index = 0; it_Index < l_Face.mNumIndices; ++it_Index)
                        {
                            l_MeshResult.Indices.push_back(l_Face.mIndices[it_Index]);
                        }
                    }

                    modelData.m_Meshes.emplace_back(std::move(l_MeshResult));
                }
            }

            void TraverseNodes(const aiNode* node, const glm::mat4& parentTransform, ModelData& modelData)
            {
                if (node == nullptr)
                {
                    return;
                }

                const glm::mat4 l_Local = ConvertMatrix(node->mTransformation);
                const glm::mat4 l_World = parentTransform * l_Local;

                for (unsigned int it_Mesh = 0; it_Mesh < node->mNumMeshes; ++it_Mesh)
                {
                    MeshInstance l_Instance{};
                    l_Instance.m_MeshIndex = static_cast<size_t>(node->mMeshes[it_Mesh]);
                    l_Instance.m_ModelMatrix = l_World;
                    l_Instance.m_NodeName = node->mName.C_Str();
                    if (l_Instance.m_MeshIndex < modelData.m_Meshes.size())
                    {
                        modelData.m_MeshInstances.emplace_back(std::move(l_Instance));
                    }
                }

                for (unsigned int it_Child = 0; it_Child < node->mNumChildren; ++it_Child)
                {
                    TraverseNodes(node->mChildren[it_Child], l_World, modelData);
                }
            }

            void PopulateAnimations(const aiScene* scene, const ModelSource& source, ModelData& modelData)
            {
                if (scene == nullptr || scene->mNumAnimations == 0)
                {
                    return;
                }

                Animation::Skeleton& l_Skeleton = modelData.m_Skeleton;
                Animation::AnimationSourceRegistry& l_Registry = Animation::AnimationSourceRegistry::Get();

                for (unsigned int it_Animation = 0; it_Animation < scene->mNumAnimations; ++it_Animation)
                {
                    const aiAnimation* l_Animation = scene->mAnimations[it_Animation];
                    if (l_Animation == nullptr)
                    {
                        continue;
                    }

                    Animation::AnimationClip l_Clip{};
                    std::string l_Name = l_Animation->mName.C_Str();
                    if (l_Name.empty())
                    {
                        l_Name = "Clip" + std::to_string(it_Animation);
                    }
                    l_Clip.m_Name = l_Name;

                    float l_TicksPerSecond = static_cast<float>(l_Animation->mTicksPerSecond);
                    if (l_TicksPerSecond <= 0.0f)
                    {
                        l_TicksPerSecond = 25.0f;
                    }
                    l_Clip.m_TicksPerSecond = l_TicksPerSecond;
                    l_Clip.m_DurationSeconds = static_cast<float>(l_Animation->mDuration) / l_TicksPerSecond;

                    for (unsigned int it_Channel = 0; it_Channel < l_Animation->mNumChannels; ++it_Channel)
                    {
                        const aiNodeAnim* l_Channel = l_Animation->mChannels[it_Channel];
                        if (l_Channel == nullptr)
                        {
                            continue;
                        }

                        std::string l_SourceName = l_Channel->mNodeName.C_Str();
                        if (l_SourceName.empty())
                        {
                            continue;
                        }

                        std::string l_Normalised = l_Registry.NormaliseBoneName(l_SourceName, source.GetIdentifier());
                        if (l_Normalised.empty())
                        {
                            l_Normalised = l_SourceName;
                        }

                        auto bookIndexIt = l_Skeleton.m_NameToIndex.find(l_Normalised);
                        if (bookIndexIt == l_Skeleton.m_NameToIndex.end())
                        {
                            continue;
                        }

                        Animation::TransformChannel l_ChannelData{};
                        l_ChannelData.m_BoneIndex = bookIndexIt->second;
                        l_ChannelData.m_SourceBoneName = l_SourceName;

                        for (unsigned int it_Key = 0; it_Key < l_Channel->mNumPositionKeys; ++it_Key)
                        {
                            const aiVectorKey& l_Key = l_Channel->mPositionKeys[it_Key];
                            Animation::VectorKeyframe l_Frame{};
                            l_Frame.m_TimeSeconds = static_cast<float>(l_Key.mTime) / l_TicksPerSecond;
                            l_Frame.m_Value = glm::vec3(l_Key.mValue.x, l_Key.mValue.y, l_Key.mValue.z);
                            l_ChannelData.m_TranslationKeys.push_back(l_Frame);
                        }

                        for (unsigned int it_Key = 0; it_Key < l_Channel->mNumRotationKeys; ++it_Key)
                        {
                            const aiQuatKey& l_Key = l_Channel->mRotationKeys[it_Key];
                            Animation::QuaternionKeyframe l_Frame{};
                            l_Frame.m_TimeSeconds = static_cast<float>(l_Key.mTime) / l_TicksPerSecond;
                            l_Frame.m_Value = glm::quat(l_Key.mValue.w, l_Key.mValue.x, l_Key.mValue.y, l_Key.mValue.z);
                            l_ChannelData.m_RotationKeys.push_back(l_Frame);
                        }

                        for (unsigned int it_Key = 0; it_Key < l_Channel->mNumScalingKeys; ++it_Key)
                        {
                            const aiVectorKey& l_Key = l_Channel->mScalingKeys[it_Key];
                            Animation::VectorKeyframe l_Frame{};
                            l_Frame.m_TimeSeconds = static_cast<float>(l_Key.mTime) / l_TicksPerSecond;
                            l_Frame.m_Value = glm::vec3(l_Key.mValue.x, l_Key.mValue.y, l_Key.mValue.z);
                            l_ChannelData.m_ScaleKeys.push_back(l_Frame);
                        }

                        l_Clip.m_Channels.push_back(std::move(l_ChannelData));
                    }

                    modelData.m_AnimationClips.push_back(std::move(l_Clip));
                }
            }
        }

        ModelData ModelLoader::Load(const std::string& filePath)
        {
            ModelSource l_Source = ModelSource::FromFile(filePath);
            return Load(l_Source);
        }

        ModelData ModelLoader::Load(const ModelSource& source)
        {
            ModelData l_ModelData{};
            l_ModelData.m_SourceIdentifier = source.GetIdentifier();

            Assimp::Importer l_Importer{};
            l_Importer.SetPropertyInteger(AI_CONFIG_PP_LBW_MAX_WEIGHTS, Vertex::MaxBoneInfluences);

            constexpr unsigned int s_AssimpFlags = aiProcess_Triangulate
                | aiProcess_GenSmoothNormals
                | aiProcess_CalcTangentSpace
                | aiProcess_JoinIdenticalVertices
                | aiProcess_LimitBoneWeights
                | aiProcess_ImproveCacheLocality
                | aiProcess_RemoveRedundantMaterials
                | aiProcess_SortByPType
                | aiProcess_GenUVCoords;

            const aiScene* l_Scene = nullptr;
            if (source.GetType() == ModelSource::SourceType::File)
            {
                l_Scene = l_Importer.ReadFile(source.GetIdentifier(), s_AssimpFlags);
            }
            else if (source.HasBuffer())
            {
                const std::string l_Hint = source.GetFormatHint();
                const char* l_HintPtr = l_Hint.empty() ? nullptr : l_Hint.c_str();
                l_Scene = l_Importer.ReadFileFromMemory(source.GetBuffer().data(), source.GetBuffer().size(), s_AssimpFlags, l_HintPtr);
            }

            if (l_Scene == nullptr || l_Scene->mRootNode == nullptr)
            {
                TR_CORE_ERROR("ModelLoader failed to import '{}': {}", source.GetIdentifier().c_str(), l_Importer.GetErrorString());
                return l_ModelData;
            }

            std::unordered_map<std::string, const aiNode*> l_NodeLookup{};
            BuildNodeLookup(l_Scene->mRootNode, l_NodeLookup);

            PopulateMaterials(l_Scene, source, l_ModelData);

            BoneContext l_BoneContext{};
            BuildSkeleton(l_Scene, source, l_NodeLookup, l_ModelData, l_BoneContext);
            PopulateMeshes(l_Scene, l_BoneContext, l_ModelData);
            PopulateAnimations(l_Scene, source, l_ModelData);

            TraverseNodes(l_Scene->mRootNode, glm::mat4(1.0f), l_ModelData);

            return l_ModelData;
        }
    }
}