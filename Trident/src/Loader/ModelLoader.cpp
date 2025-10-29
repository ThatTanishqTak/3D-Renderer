#include "Loader/ModelLoader.h"

#include "Core/Utilities.h"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace Trident
{
    namespace Loader
    {
        namespace
        {
            constexpr unsigned int s_AssimpFlags =
                aiProcess_Triangulate |
                aiProcess_JoinIdenticalVertices |
                aiProcess_CalcTangentSpace |
                aiProcess_GenSmoothNormals |
                aiProcess_LimitBoneWeights |
                aiProcess_ImproveCacheLocality |
                aiProcess_RemoveRedundantMaterials |
                aiProcess_SortByPType |
                aiProcess_GenUVCoords |
                aiProcess_OptimizeMeshes |
                aiProcess_ValidateDataStructure;

            glm::mat4 ConvertMatrix(const aiMatrix4x4& matrix)
            {
                glm::mat4 l_Matrix = glm::transpose(glm::make_mat4(&matrix.a1));
                return l_Matrix;
            }

            glm::vec3 ConvertVector(const aiVector3D& vector)
            {
                return { vector.x, vector.y, vector.z };
            }

            glm::vec4 ConvertColor(const aiColor4D& color)
            {
                return { color.r, color.g, color.b, color.a };
            }

            std::string NormalizeBoneName(std::string name)
            {
                auto a_IsSpace = [](unsigned char character)
                    {
                        return std::isspace(character) != 0;
                    };

                name.erase(name.begin(), std::find_if(name.begin(), name.end(), [a_IsSpace](unsigned char character)
                    {
                        return !a_IsSpace(character);
                    }));
                name.erase(std::find_if(name.rbegin(), name.rend(), [a_IsSpace](unsigned char character)
                    {
                        return !a_IsSpace(character);
                    }).base(), name.end());

                constexpr std::string_view s_MixamoPrefix = "mixamorig:";
                if (name.size() > s_MixamoPrefix.size() && name.compare(0, s_MixamoPrefix.size(), s_MixamoPrefix) == 0)
                {
                    name.erase(0, s_MixamoPrefix.size());
                }

                return name;
            }

            const aiNode* FindNode(const std::unordered_map<std::string, const aiNode*>& nodeLookup, const std::string& name)
            {
                auto a_It = nodeLookup.find(name);
                return a_It != nodeLookup.end() ? a_It->second : nullptr;
            }

            void AssignBoneWeight(Vertex& vertex, int boneIndex, float weight)
            {
                if (boneIndex < 0 || weight <= 0.0f)
                {
                    return;
                }

                for (int it_Index = 0; it_Index < static_cast<int>(Vertex::MaxBoneInfluences); ++it_Index)
                {
                    if (vertex.m_BoneWeights[it_Index] <= 0.0f)
                    {
                        vertex.m_BoneIndices[it_Index] = boneIndex;
                        vertex.m_BoneWeights[it_Index] = weight;
                        return;
                    }
                }

                int l_MinIndex = 0;
                float l_MinWeight = vertex.m_BoneWeights[0];
                for (int it_Index = 1; it_Index < static_cast<int>(Vertex::MaxBoneInfluences); ++it_Index)
                {
                    if (vertex.m_BoneWeights[it_Index] < l_MinWeight)
                    {
                        l_MinWeight = vertex.m_BoneWeights[it_Index];
                        l_MinIndex = it_Index;
                    }
                }

                if (weight > l_MinWeight)
                {
                    vertex.m_BoneIndices[l_MinIndex] = boneIndex;
                    vertex.m_BoneWeights[l_MinIndex] = weight;
                }
            }

            void NormaliseBoneWeights(Vertex& vertex)
            {
                const float l_TotalWeight = vertex.m_BoneWeights.x + vertex.m_BoneWeights.y + vertex.m_BoneWeights.z + vertex.m_BoneWeights.w;
                if (l_TotalWeight <= 0.0f)
                {
                    return;
                }

                const float l_InvTotal = 1.0f / l_TotalWeight;
                for (int it_Index = 0; it_Index < static_cast<int>(Vertex::MaxBoneInfluences); ++it_Index)
                {
                    vertex.m_BoneWeights[it_Index] *= l_InvTotal;
                }
            }

            int EnsureBoneExists(const std::string& sourceName, Animation::Skeleton& skeleton, const std::unordered_map<std::string, const aiNode*>& nodeLookup,
                std::unordered_map<std::string, int>& boneLookup)
            {
                if (sourceName.empty())
                {
                    return -1;
                }

                auto a_Existing = boneLookup.find(sourceName);
                if (a_Existing != boneLookup.end())
                {
                    return a_Existing->second;
                }

                Animation::Bone l_Bone{};
                l_Bone.m_SourceName = sourceName;
                l_Bone.m_Name = NormalizeBoneName(sourceName);
                l_Bone.m_LocalBindTransform = glm::mat4(1.0f);
                l_Bone.m_InverseBindMatrix = glm::mat4(1.0f);

                if (const aiNode* l_Node = FindNode(nodeLookup, sourceName))
                {
                    l_Bone.m_LocalBindTransform = ConvertMatrix(l_Node->mTransformation);
                }

                const int l_NewIndex = static_cast<int>(skeleton.m_Bones.size());
                skeleton.m_Bones.push_back(l_Bone);
                skeleton.m_NameToIndex[l_Bone.m_Name] = l_NewIndex;
                skeleton.m_SourceNameToIndex[l_Bone.m_SourceName] = l_NewIndex;
                boneLookup.emplace(sourceName, l_NewIndex);
                return l_NewIndex;
            }

            int ResolveTextureIndex(const aiMaterial* material, aiTextureType type, const std::filesystem::path& modelDirectory, std::vector<std::string>& textures,
                std::unordered_map<std::string, int>& textureLookup)
            {
                aiString l_Path{};
                if (material->GetTexture(type, 0, &l_Path) != AI_SUCCESS)
                {
                    return -1;
                }

                if (l_Path.length == 0)
                {
                    return -1;
                }

                std::filesystem::path l_TexturePath{ l_Path.C_Str() };
                if (l_TexturePath.empty() || l_TexturePath.string().front() == '*')
                {
                    TR_CORE_WARN("Embedded textures are not supported ({}).", l_Path.C_Str());
                    return -1;
                }

                if (!l_TexturePath.is_absolute())
                {
                    l_TexturePath = modelDirectory / l_TexturePath;
                }

                std::string l_Normalised = Utilities::FileManagement::NormalizePath(l_TexturePath.string());
                if (l_Normalised.empty())
                {
                    return -1;
                }

                auto a_Found = textureLookup.find(l_Normalised);
                if (a_Found != textureLookup.end())
                {
                    return a_Found->second;
                }

                const int l_NewIndex = static_cast<int>(textures.size());
                textures.push_back(l_Normalised);
                textureLookup.emplace(l_Normalised, l_NewIndex);
                return l_NewIndex;
            }

            void FinaliseSkeletonHierarchy(Animation::Skeleton& skeleton, const std::unordered_map<std::string, const aiNode*>& nodeLookup,
                const std::unordered_map<std::string, int>& boneLookup)
            {
                for (size_t it_Index = 0; it_Index < skeleton.m_Bones.size(); ++it_Index)
                {
                    Animation::Bone& l_Bone = skeleton.m_Bones[it_Index];
                    if (l_Bone.m_SourceName.empty())
                    {
                        continue;
                    }

                    const aiNode* l_Node = FindNode(nodeLookup, l_Bone.m_SourceName);
                    const aiNode* l_ParentNode = l_Node != nullptr ? l_Node->mParent : nullptr;

                    int l_ParentIndex = -1;
                    while (l_ParentNode != nullptr)
                    {
                        std::string l_ParentName = l_ParentNode->mName.C_Str();
                        auto a_ParentIt = boneLookup.find(l_ParentName);
                        if (a_ParentIt != boneLookup.end())
                        {
                            l_ParentIndex = a_ParentIt->second;
                            break;
                        }

                        l_ParentNode = l_ParentNode->mParent;
                    }

                    l_Bone.m_ParentIndex = l_ParentIndex;
                    if (l_ParentIndex >= 0)
                    {
                        Animation::Bone& l_Parent = skeleton.m_Bones[static_cast<size_t>(l_ParentIndex)];
                        l_Parent.m_Children.push_back(static_cast<int>(it_Index));
                    }
                    else if (skeleton.m_RootBoneIndex < 0)
                    {
                        skeleton.m_RootBoneIndex = static_cast<int>(it_Index);
                    }
                }
            }
        }

        ModelData ModelLoader::Load(const std::string& filePath)
        {
            ModelData l_ModelData{};
            if (filePath.empty())
            {
                TR_CORE_WARN("ModelLoader::Load received an empty file path.");
                return l_ModelData;
            }

            std::string l_NormalisedPath = Utilities::FileManagement::NormalizePath(filePath);
            if (l_NormalisedPath.empty())
            {
                TR_CORE_ERROR("Failed to normalise model path: {}", filePath.c_str());
                return l_ModelData;
            }

            std::filesystem::path l_ModelPath{ l_NormalisedPath };
            if (!std::filesystem::exists(l_ModelPath))
            {
                TR_CORE_ERROR("Model file not found: {}", l_NormalisedPath.c_str());
                return l_ModelData;
            }

            std::filesystem::path l_ModelDirectory = l_ModelPath.parent_path();

            Assimp::Importer l_Importer{};
            const aiScene* l_Scene = l_Importer.ReadFile(l_NormalisedPath, s_AssimpFlags);
            if (l_Scene == nullptr || (l_Scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0)
            {
                TR_CORE_ERROR("Assimp failed to import '{}': {}", l_NormalisedPath.c_str(), l_Importer.GetErrorString());
                return l_ModelData;
            }

            std::unordered_map<std::string, const aiNode*> l_NodeLookup{};
            std::function<void(const aiNode*)> l_PopulateNodeLookup = [&](const aiNode* node)
                {
                    if (node == nullptr)
                    {
                        return;
                    }

                    l_NodeLookup.emplace(node->mName.C_Str(), node);
                    for (unsigned int it_Child = 0; it_Child < node->mNumChildren; ++it_Child)
                    {
                        l_PopulateNodeLookup(node->mChildren[it_Child]);
                    }
                };
            l_PopulateNodeLookup(l_Scene->mRootNode);

            std::unordered_map<std::string, int> l_BoneLookup{};

            std::unordered_map<std::string, int> l_TextureLookup{};
            l_ModelData.m_Textures.reserve(static_cast<size_t>(l_Scene->mNumMaterials));
            l_ModelData.m_Materials.reserve(static_cast<size_t>(l_Scene->mNumMaterials));

            for (unsigned int it_Material = 0; it_Material < l_Scene->mNumMaterials; ++it_Material)
            {
                const aiMaterial* l_AssimpMaterial = l_Scene->mMaterials[it_Material];
                if (l_AssimpMaterial == nullptr)
                {
                    continue;
                }

                Geometry::Material l_Material{};

                aiColor4D l_BaseColor{ 1.0f, 1.0f, 1.0f, 1.0f };
                if (l_AssimpMaterial->Get(AI_MATKEY_BASE_COLOR, l_BaseColor) != AI_SUCCESS)
                {
                    l_AssimpMaterial->Get(AI_MATKEY_COLOR_DIFFUSE, l_BaseColor);
                }
                l_Material.BaseColorFactor = ConvertColor(l_BaseColor);

                float l_Metallic = 1.0f;
                float l_Roughness = 1.0f;
                l_AssimpMaterial->Get(AI_MATKEY_METALLIC_FACTOR, l_Metallic);
                l_AssimpMaterial->Get(AI_MATKEY_ROUGHNESS_FACTOR, l_Roughness);
                l_Material.MetallicFactor = l_Metallic;
                l_Material.RoughnessFactor = l_Roughness;

                l_Material.BaseColorTextureIndex = ResolveTextureIndex(l_AssimpMaterial, aiTextureType_BASE_COLOR, l_ModelDirectory, l_ModelData.m_Textures, l_TextureLookup);
                if (l_Material.BaseColorTextureIndex < 0)
                {
                    l_Material.BaseColorTextureIndex = ResolveTextureIndex(l_AssimpMaterial, aiTextureType_DIFFUSE, l_ModelDirectory, l_ModelData.m_Textures, l_TextureLookup);
                }

                l_Material.MetallicRoughnessTextureIndex = ResolveTextureIndex(l_AssimpMaterial, aiTextureType_METALNESS, l_ModelDirectory, l_ModelData.m_Textures, l_TextureLookup);
                if (l_Material.MetallicRoughnessTextureIndex < 0)
                {
                    l_Material.MetallicRoughnessTextureIndex = ResolveTextureIndex(l_AssimpMaterial, aiTextureType_DIFFUSE_ROUGHNESS, l_ModelDirectory, l_ModelData.m_Textures, 
                        l_TextureLookup);
                }

                l_Material.NormalTextureIndex = ResolveTextureIndex(l_AssimpMaterial, aiTextureType_NORMALS, l_ModelDirectory, l_ModelData.m_Textures, l_TextureLookup);

                l_ModelData.m_Materials.emplace_back(std::move(l_Material));
            }

            l_ModelData.m_Meshes.reserve(static_cast<size_t>(l_Scene->mNumMeshes));
            std::vector<size_t> l_MeshIndexMap(l_Scene->mNumMeshes, std::numeric_limits<size_t>::max());

            for (unsigned int it_Mesh = 0; it_Mesh < l_Scene->mNumMeshes; ++it_Mesh)
            {
                const aiMesh* l_AssimpMesh = l_Scene->mMeshes[it_Mesh];
                if (l_AssimpMesh == nullptr)
                {
                    continue;
                }

                Geometry::Mesh l_Mesh{};
                l_Mesh.MaterialIndex = l_AssimpMesh->mMaterialIndex >= 0 ? static_cast<int>(l_AssimpMesh->mMaterialIndex) : -1;
                l_Mesh.Vertices.resize(static_cast<size_t>(l_AssimpMesh->mNumVertices));

                for (unsigned int it_Vertex = 0; it_Vertex < l_AssimpMesh->mNumVertices; ++it_Vertex)
                {
                    Vertex& l_Vertex = l_Mesh.Vertices[it_Vertex];
                    if (l_AssimpMesh->HasPositions())
                    {
                        l_Vertex.Position = ConvertVector(l_AssimpMesh->mVertices[it_Vertex]);
                    }
                    if (l_AssimpMesh->HasNormals())
                    {
                        l_Vertex.Normal = ConvertVector(l_AssimpMesh->mNormals[it_Vertex]);
                    }
                    if (l_AssimpMesh->HasTangentsAndBitangents())
                    {
                        l_Vertex.Tangent = ConvertVector(l_AssimpMesh->mTangents[it_Vertex]);
                        l_Vertex.Bitangent = ConvertVector(l_AssimpMesh->mBitangents[it_Vertex]);
                    }
                    if (l_AssimpMesh->HasTextureCoords(0))
                    {
                        l_Vertex.TexCoord = { l_AssimpMesh->mTextureCoords[0][it_Vertex].x, l_AssimpMesh->mTextureCoords[0][it_Vertex].y };
                    }
                    if (l_AssimpMesh->HasVertexColors(0))
                    {
                        l_Vertex.Color = { l_AssimpMesh->mColors[0][it_Vertex].r, l_AssimpMesh->mColors[0][it_Vertex].g, l_AssimpMesh->mColors[0][it_Vertex].b };
                    }
                    else
                    {
                        l_Vertex.Color = glm::vec3(1.0f);
                    }
                }

                l_Mesh.Indices.reserve(static_cast<size_t>(l_AssimpMesh->mNumFaces) * 3);
                for (unsigned int it_Face = 0; it_Face < l_AssimpMesh->mNumFaces; ++it_Face)
                {
                    const aiFace& l_Face = l_AssimpMesh->mFaces[it_Face];
                    if (l_Face.mNumIndices != 3)
                    {
                        continue;
                    }

                    l_Mesh.Indices.push_back(l_Face.mIndices[0]);
                    l_Mesh.Indices.push_back(l_Face.mIndices[1]);
                    l_Mesh.Indices.push_back(l_Face.mIndices[2]);
                }

                if (l_AssimpMesh->HasBones())
                {
                    for (unsigned int it_Bone = 0; it_Bone < l_AssimpMesh->mNumBones; ++it_Bone)
                    {
                        const aiBone* l_AssimpBone = l_AssimpMesh->mBones[it_Bone];
                        if (l_AssimpBone == nullptr)
                        {
                            continue;
                        }

                        const std::string l_BoneName = l_AssimpBone->mName.C_Str();
                        const int l_BoneIndex = EnsureBoneExists(l_BoneName, l_ModelData.m_Skeleton, l_NodeLookup, l_BoneLookup);
                        if (l_BoneIndex < 0)
                        {
                            continue;
                        }

                        Animation::Bone& l_Bone = l_ModelData.m_Skeleton.m_Bones[static_cast<size_t>(l_BoneIndex)];
                        l_Bone.m_InverseBindMatrix = ConvertMatrix(l_AssimpBone->mOffsetMatrix);

                        for (unsigned int it_Weight = 0; it_Weight < l_AssimpBone->mNumWeights; ++it_Weight)
                        {
                            const aiVertexWeight& l_Weight = l_AssimpBone->mWeights[it_Weight];
                            if (l_Weight.mVertexId >= l_AssimpMesh->mNumVertices)
                            {
                                continue;
                            }

                            Vertex& l_Vertex = l_Mesh.Vertices[l_Weight.mVertexId];
                            AssignBoneWeight(l_Vertex, l_BoneIndex, l_Weight.mWeight);
                        }
                    }
                }

                for (Vertex& it_Vertex : l_Mesh.Vertices)
                {
                    NormaliseBoneWeights(it_Vertex);
                }

                l_MeshIndexMap[it_Mesh] = l_ModelData.m_Meshes.size();
                l_ModelData.m_Meshes.emplace_back(std::move(l_Mesh));
            }

            l_ModelData.m_MeshInstances.clear();
            l_ModelData.m_MeshInstances.reserve(static_cast<size_t>(l_Scene->mNumMeshes));

            const std::function<void(const aiNode*, const glm::mat4&)> l_VisitNode = [&](const aiNode* node, const glm::mat4& parentTransform)
                {
                    if (node == nullptr)
                    {
                        return;
                    }

                    const glm::mat4 l_Local = ConvertMatrix(node->mTransformation);
                    const glm::mat4 l_ModelMatrix = parentTransform * l_Local;

                    for (unsigned int it_MeshIndex = 0; it_MeshIndex < node->mNumMeshes; ++it_MeshIndex)
                    {
                        const unsigned int l_AssimpMeshIndex = node->mMeshes[it_MeshIndex];
                        if (l_AssimpMeshIndex >= l_MeshIndexMap.size())
                        {
                            continue;
                        }

                        const size_t l_ModelMeshIndex = l_MeshIndexMap[l_AssimpMeshIndex];
                        if (l_ModelMeshIndex == std::numeric_limits<size_t>::max())
                        {
                            continue;
                        }

                        MeshInstance l_Instance{};
                        l_Instance.m_MeshIndex = l_ModelMeshIndex;
                        l_Instance.m_ModelMatrix = l_ModelMatrix;
                        l_Instance.m_NodeName = node->mName.C_Str();
                        l_ModelData.m_MeshInstances.emplace_back(std::move(l_Instance));
                    }

                    for (unsigned int it_Child = 0; it_Child < node->mNumChildren; ++it_Child)
                    {
                        l_VisitNode(node->mChildren[it_Child], l_ModelMatrix);
                    }
                };
            l_VisitNode(l_Scene->mRootNode, glm::mat4(1.0f));

            l_ModelData.m_AnimationClips.reserve(static_cast<size_t>(l_Scene->mNumAnimations));
            for (unsigned int it_Animation = 0; it_Animation < l_Scene->mNumAnimations; ++it_Animation)
            {
                const aiAnimation* l_AssimpAnimation = l_Scene->mAnimations[it_Animation];
                if (l_AssimpAnimation == nullptr)
                {
                    continue;
                }

                Animation::AnimationClip l_Clip{};
                l_Clip.m_Name = l_AssimpAnimation->mName.C_Str();
                if (l_Clip.m_Name.empty())
                {
                    l_Clip.m_Name = "Clip" + std::to_string(it_Animation);
                }

                const double l_TicksPerSecond = l_AssimpAnimation->mTicksPerSecond > 0.0 ? l_AssimpAnimation->mTicksPerSecond : 25.0;
                l_Clip.m_TicksPerSecond = static_cast<float>(l_TicksPerSecond);
                const double l_DurationSeconds = l_TicksPerSecond > 0.0 ? l_AssimpAnimation->mDuration / l_TicksPerSecond : 0.0;
                l_Clip.m_DurationSeconds = static_cast<float>(l_DurationSeconds);

                l_Clip.m_Channels.reserve(static_cast<size_t>(l_AssimpAnimation->mNumChannels));
                for (unsigned int it_Channel = 0; it_Channel < l_AssimpAnimation->mNumChannels; ++it_Channel)
                {
                    const aiNodeAnim* l_AssimpChannel = l_AssimpAnimation->mChannels[it_Channel];
                    if (l_AssimpChannel == nullptr)
                    {
                        continue;
                    }

                    const std::string l_ChannelBoneName = l_AssimpChannel->mNodeName.C_Str();
                    const int l_BoneIndex = EnsureBoneExists(l_ChannelBoneName, l_ModelData.m_Skeleton, l_NodeLookup, l_BoneLookup);
                    if (l_BoneIndex < 0)
                    {
                        continue;
                    }

                    Animation::TransformChannel l_Channel{};
                    l_Channel.m_BoneIndex = l_BoneIndex;

                    l_Channel.m_TranslationKeys.reserve(static_cast<size_t>(l_AssimpChannel->mNumPositionKeys));
                    for (unsigned int it_Key = 0; it_Key < l_AssimpChannel->mNumPositionKeys; ++it_Key)
                    {
                        const aiVectorKey& l_Key = l_AssimpChannel->mPositionKeys[it_Key];
                        Animation::VectorKeyframe l_Keyframe{};
                        l_Keyframe.m_TimeSeconds = static_cast<float>(l_Key.mTime / l_TicksPerSecond);
                        l_Keyframe.m_Value = ConvertVector(l_Key.mValue);
                        l_Channel.m_TranslationKeys.emplace_back(std::move(l_Keyframe));
                    }

                    l_Channel.m_RotationKeys.reserve(static_cast<size_t>(l_AssimpChannel->mNumRotationKeys));
                    for (unsigned int it_Key = 0; it_Key < l_AssimpChannel->mNumRotationKeys; ++it_Key)
                    {
                        const aiQuatKey& l_Key = l_AssimpChannel->mRotationKeys[it_Key];
                        Animation::QuaternionKeyframe l_Keyframe{};
                        l_Keyframe.m_TimeSeconds = static_cast<float>(l_Key.mTime / l_TicksPerSecond);
                        const aiQuaternion& l_Quat = l_Key.mValue;
                        l_Keyframe.m_Value = glm::quat(l_Quat.w, l_Quat.x, l_Quat.y, l_Quat.z);
                        l_Channel.m_RotationKeys.emplace_back(std::move(l_Keyframe));
                    }

                    l_Channel.m_ScaleKeys.reserve(static_cast<size_t>(l_AssimpChannel->mNumScalingKeys));
                    for (unsigned int it_Key = 0; it_Key < l_AssimpChannel->mNumScalingKeys; ++it_Key)
                    {
                        const aiVectorKey& l_Key = l_AssimpChannel->mScalingKeys[it_Key];
                        Animation::VectorKeyframe l_Keyframe{};
                        l_Keyframe.m_TimeSeconds = static_cast<float>(l_Key.mTime / l_TicksPerSecond);
                        l_Keyframe.m_Value = ConvertVector(l_Key.mValue);
                        l_Channel.m_ScaleKeys.emplace_back(std::move(l_Keyframe));
                    }

                    l_Clip.m_Channels.emplace_back(std::move(l_Channel));
                }

                l_ModelData.m_AnimationClips.emplace_back(std::move(l_Clip));
            }

            FinaliseSkeletonHierarchy(l_ModelData.m_Skeleton, l_NodeLookup, l_BoneLookup);

            if (l_ModelData.m_Skeleton.m_RootBoneIndex < 0 && !l_ModelData.m_Skeleton.m_Bones.empty())
            {
                l_ModelData.m_Skeleton.m_RootBoneIndex = 0;
            }

            return l_ModelData;
        }
    }
}